#include <common.h>

#include "axi_responder.hh"

#ifdef DEBUG
#define AXILog(...) Log(__VA_ARGS__)
#else
#define AXILog(...)
#endif

#define GENMASK(h, l) \
        (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (sizeof(unsigned long) * 8 - 1 - (h))))

bool find_strb_transation(uint32_t strb, int *offset, int *len)
{
    if (!strb)
        return false;
    
    *offset = __builtin_ffs(strb) - 1;
    *len = __builtin_ctz(~(strb >> *offset));

    return true;
}

AXIResponder::AXIResponder(struct connections _dla, const char *_name) {
    dla = _dla;
    
    *dla.aw_awready = 1;
    *dla.w_wready = 1;
    *dla.b_bvalid = 0;
    *dla.ar_arready = 1;
    *dla.r_rvalid = 0;
    
    name = _name;
    
    /* add some latency... */
    for (int i = 0; i < AXI_R_LATENCY; i++) {
        axi_r_txn txn;
        
        txn.rvalid = 0;
        txn.rvalid = 0;
        txn.rid = 0;
        txn.rlast = 0;
        for (int i = 0; i < AXI_WIDTH / AXI_WDATA_TYLEN; i++) {
            txn.rdata[i] = (AXI_WDATA_TYPE)0xAAAAAAAAAAAAAAAAULL;
        }
        
        r0_fifo.push(txn);
    }
}

void AXIResponder::eval() {
    /* write request */
    if (*dla.aw_awvalid && *dla.aw_awready) {
        AXILog("%s: write request from dla, addr %08lx id %d\n", name, (uint64_t) *dla.aw_awaddr, *dla.aw_awid);
        
        axi_aw_txn txn;
        
        txn.awid = *dla.aw_awid;
        txn.awaddr = *dla.aw_awaddr & ~(AXI_ADDR_TYPE)(AXI_WIDTH / 8 - 1);
        txn.awlen = *dla.aw_awlen;
        aw_fifo.push(txn);
        
        *dla.aw_awready = 0;
    } else
        *dla.aw_awready = 1;
    
    /* write data */
    if (*dla.w_wvalid) {
        AXILog("%s: write data from dla (%08lx...)\n", name, (uint64_t) dla.w_wdata[0]);
        
        axi_w_txn txn;
        
        for (int i = 0; i < AXI_WIDTH / AXI_WDATA_TYLEN; i++)
            txn.wdata[i] = dla.w_wdata[i];
        txn.wstrb = *dla.w_wstrb;
        txn.wlast = *dla.w_wlast;
        w_fifo.push(txn);
    }
    
    /* read request */
    if (*dla.ar_arvalid && *dla.ar_arready) {
        AXI_ADDR_TYPE addr = *dla.ar_araddr & ~(AXI_ADDR_TYPE)(AXI_WIDTH / 8 - 1);
        uint8_t len = *dla.ar_arlen;

        AXILog("%s: read request from dla, addr %08lx burst %d id %d\n", name, (uint64_t) *dla.ar_araddr, *dla.ar_arlen, *dla.ar_arid);
        
        do {
            axi_r_txn txn;

            txn.rvalid = 1;
            txn.rlast = len == 0;
            txn.rid = *dla.ar_arid;

            /* FIXME: correct transation length */
            mem_read(addr, AXI_WIDTH / 8, &txn.rdata);

            r_fifo.push(txn);
            
            /* This doesn't seems to be correct */
            addr += AXI_WIDTH / 8;
        } while (len--);
        
        axi_r_txn txn;
        
        txn.rvalid = 0;
        txn.rid = 0;
        txn.rlast = 0;
        memset(&txn.rdata, 0x5, (AXI_WIDTH/8));
        
        for (int i = 0; i < AXI_R_DELAY; i++)
            r_fifo.push(txn);
        
        *dla.ar_arready = 0;
    } else
        *dla.ar_arready = 1;
    
    /* now handle the write FIFOs ... */
    if (!aw_fifo.empty() && !w_fifo.empty()) {
        int woff, wlen;
        uint32_t strb;

        axi_aw_txn &awtxn = aw_fifo.front();
        axi_w_txn &wtxn = w_fifo.front();
        
        if (wtxn.wlast != (awtxn.awlen == 0)) {
            AXILog("%s: wlast / awlen mismatch\n", name);
        }

        strb = wtxn.wstrb;
        while (find_strb_transation(strb, &woff, &wlen)) {
            mem_write(awtxn.awaddr + woff, wlen, (uint8_t *)(&wtxn.wdata) + woff);
            strb &= ~GENMASK(woff + wlen - 1, woff);
        }

        if (wtxn.wlast) {
            AXILog("%s: write, last tick\n", name);
            aw_fifo.pop();

            axi_b_txn btxn;
            btxn.bid = awtxn.awid;
            b_fifo.push(btxn);
        } else {
            AXILog("%s: write, ticks remaining\n", name);

            awtxn.awlen--;
            awtxn.awaddr += AXI_WIDTH / 8;
        }
        
        w_fifo.pop();
    }
    
    /* read response */
    if (!r_fifo.empty()) {
        axi_r_txn &txn = r_fifo.front();
        
        r0_fifo.push(txn);
        r_fifo.pop();
    } else {
        axi_r_txn txn;
        
        txn.rvalid = 0;
        txn.rid = 0;
        txn.rlast = 0;
        for (int i = 0; i < AXI_WIDTH / AXI_WDATA_TYLEN; i++) {
            txn.rdata[i] = 0xAAAAAAAA;
        }
        
        r0_fifo.push(txn);
    }

    *dla.r_rvalid = 0;
    if (*dla.r_rready && !r0_fifo.empty()) {
        axi_r_txn &txn = r0_fifo.front();
        
        *dla.r_rvalid = txn.rvalid;
        *dla.r_rid = txn.rid;
        *dla.r_rlast = txn.rlast;
        for (int i = 0; i < AXI_WIDTH / AXI_WDATA_TYLEN; i++) {
            dla.r_rdata[i] = txn.rdata[i];
        }
        
        if (txn.rvalid)
            AXILog("%s: read push: id %d, da %08lx\n",
                name, txn.rid, (uint64_t) txn.rdata[0]);
        
        r0_fifo.pop();
    }
    
    /* write response */
    *dla.b_bvalid = 0;
    if (*dla.b_bready && !b_fifo.empty()) {
        *dla.b_bvalid = 1;
        
        axi_b_txn &txn = b_fifo.front();
        *dla.b_bid = txn.bid;
        b_fifo.pop();
    }
}
