#ifndef _AXI_RESPONDER_HH
#define _AXI_RESPONDER_HH

#include <queue>
#include <map>

#ifdef DEBUG
#define AXILog(...) Log(__VA_ARGS__)
#else
#define AXILog(...)
#endif

#define GENMASK(h, l) \
        (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (sizeof(unsigned long) * 8 - 1 - (h))))

template <int DATA_W, typename Taddr, typename Tstrb, typename Tid>
class AXIResponder {
public:
    void (*mem_read)(Taddr addr, uint8_t len, void *buf);
    void (*mem_write)(Taddr addr, uint8_t len, void *buf);

	struct connections {
		uint8_t *aw_awvalid;
		uint8_t *aw_awready;
		uint8_t *aw_awid;
		uint8_t *aw_awlen;
		uint8_t *aw_awsize;
		uint8_t *aw_awburst;
		Taddr *aw_awaddr;
		
		uint8_t *w_wvalid;
		uint8_t *w_wready;
		void *w_wdata;
		Tstrb *w_wstrb;
		uint8_t *w_wlast;
		
		uint8_t *b_bvalid;
		uint8_t *b_bready;
		uint8_t *b_bid;
		uint8_t *b_bresp;
		
		uint8_t *ar_arvalid;
		uint8_t *ar_arready;
		uint8_t *ar_arid;
		uint8_t *ar_arlen;
		uint8_t *ar_arsize;
		uint8_t *ar_arburst;
		Taddr *ar_araddr;
		
		uint8_t *r_rvalid;
		uint8_t *r_rready;
		uint8_t *r_rid;
		uint8_t *r_rlast;
		uint8_t *r_rresp;
		void *r_rdata;
	};

private:
	const static int AXI_R_LATENCY = 32;
	const static int AXI_R_DELAY = 0;

	struct axi_r_txn {
		int rvalid;
		int rlast;
		uint8_t rdata[DATA_W / 8];
		uint8_t rid;
	};
	std::queue<axi_r_txn> r_fifo;
	std::queue<axi_r_txn> r0_fifo;
	
	struct axi_aw_txn {
		uint8_t awid;
		Taddr awaddr;
		uint8_t awlen;
	};
	std::queue<axi_aw_txn> aw_fifo;
	
	struct axi_w_txn {
		uint8_t wdata[DATA_W / 8];
		Tstrb wstrb;
		uint8_t wlast;
	};
	std::queue<axi_w_txn> w_fifo;
	
	struct axi_b_txn {
		uint8_t bid;
	};
	std::queue<axi_b_txn> b_fifo;
	
	struct connections dla;
	const char *name;

	bool find_strb_transation(uint32_t strb, int *offset, int *len) {
		if (!strb)
			return false;
		
		*offset = __builtin_ffs(strb) - 1;
		*len = __builtin_ctz(~(strb >> *offset));

		return true;
	}

public:
	AXIResponder(struct connections _dla, const char *_name) {
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
			memset(&txn.rdata, 0xA, sizeof(txn.rdata));
			
			r0_fifo.push(txn);
		}
	};

	void eval() {
		/* write request */
		if (*dla.aw_awvalid && *dla.aw_awready) {
			axi_aw_txn txn;
			txn.awid = dla.aw_awid ? *dla.aw_awid : 0;
			txn.awaddr = *dla.aw_awaddr & ~(Taddr)(DATA_W / 8 - 1);
			txn.awlen = dla.aw_awlen ? *dla.aw_awlen : 0;
			AXILog("%s: write request from dla, addr %08lx id %d\n", name, (uint64_t)txn.awaddr, txn.awid);

			if (dla.aw_awburst && *dla.aw_awburst != 0)
				panic("Unsupported burst type: %d", *dla.ar_arburst);

			if (dla.aw_awsize && *dla.aw_awsize != DATA_W / 8)
				panic("Unsupported narrow transfer: %d", *dla.ar_arsize);

			aw_fifo.push(txn);
			
			*dla.aw_awready = 0;
		} else {
			*dla.aw_awready = 1;
		}

		/* write data */
		if (*dla.w_wvalid) {
			axi_w_txn txn;
			AXILog("%s: write data from dla (%08lx...)\n", name, *(uint32_t*)dla.w_wdata);
			memcpy(txn.wdata, dla.w_wdata, sizeof(dla.w_wdata));
			txn.wstrb = *dla.w_wstrb;
			txn.wlast = dla.w_wlast ? *dla.w_wlast : 1;
			w_fifo.push(txn);
		}
		
		/* read request */
		if (*dla.ar_arvalid && *dla.ar_arready) {
			Taddr addr = *dla.ar_araddr & ~(Taddr)(DATA_W / 8 - 1);
			Tid arid = dla.ar_arid ? *dla.ar_arid : 0;
			uint8_t len = dla.ar_arlen ? *dla.ar_arlen : 0;

			AXILog("%s: read request from dla, addr %08lx burst %d id %d\n", name, (uint64_t) *dla.ar_araddr, *dla.ar_arlen, *dla.ar_arid);
			
			if (dla.ar_arburst && *dla.ar_arburst != 0)
				panic("Unsupported burst type: %d", *dla.ar_arburst);

			if (dla.ar_arsize && *dla.ar_arsize != DATA_W / 8)
				panic("Unsupported narrow transfer: %d", *dla.ar_arsize);

			do {
				axi_r_txn txn;

				txn.rvalid = 1;
				txn.rlast = len == 0;
				txn.rid = dla.ar_arid ? *dla.ar_arid : 0;

				/* FIXME: correct transation length */
				mem_read(addr, DATA_W / 8, &txn.rdata);
				r_fifo.push(txn);
				
				addr += DATA_W / 8;
			} while (len--);
			
			if (AXI_R_DELAY) {
				axi_r_txn txn;
				txn.rvalid = 0;
				txn.rid = 0;
				txn.rlast = 0;
				memset(&txn.rdata, 0x5, sizeof(txn.rdata));
				
				for (int i = 0; i < AXI_R_DELAY; i++)
					r_fifo.push(txn);
			}

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
				awtxn.awaddr += DATA_W / 8;
			}
			
			w_fifo.pop();
		}
		
		/* read response */
		if (!r_fifo.empty()) {
			axi_r_txn &txn = r_fifo.front();
			
			r0_fifo.push(txn);
			r_fifo.pop();
		} else {
			/* TODO: These logic should be removed */
			axi_r_txn txn;
			
			txn.rvalid = 0;
			txn.rid = 0;
			txn.rlast = 0;
			memset(&txn.rdata, 0xA, sizeof(txn.rdata));

			r0_fifo.push(txn);
		}

		*dla.r_rvalid = 0;
		if (*dla.r_rready && !r0_fifo.empty()) {
			axi_r_txn &txn = r0_fifo.front();
			
			*dla.r_rvalid = txn.rvalid;
			if (dla.r_rid)
				*dla.r_rid = txn.rid;
			if (dla.r_rlast)
				*dla.r_rlast = txn.rlast;
			if (dla.r_rresp)
				*dla.r_rresp = 0; // OK
			memcpy(dla.r_rdata, txn.rdata, sizeof(txn.rdata));
			
			if (txn.rvalid)
				AXILog("%s: read push: id %d, da %08lx\n",
					name, txn.rid, (uint32_t) *txn.rdata);
			
			r0_fifo.pop();
		}
		
		/* write response */
		*dla.b_bvalid = 0;
		if (*dla.b_bready && !b_fifo.empty()) {
			*dla.b_bvalid = 1;
			
			axi_b_txn &txn = b_fifo.front();
			if (dla.b_bid)
				*dla.b_bid = txn.bid;
			if (dla.b_bresp)
				*dla.b_bresp = 0; // OK

			b_fifo.pop();
		}
	};
};
#endif
