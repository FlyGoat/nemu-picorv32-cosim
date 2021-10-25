#include <stdlib.h>
#include <stdint.h>
#include <isa.h>
#include <memory/paddr.h>
#include <monitor/monitor.h>

#include "axi_responder.hh"

#include "Vpicorv32_axi.h"
#include "verilated_vcd_c.h"

Vpicorv32_axi *tb;
VerilatedVcdC *m_trace;

AXIResponder<32, uint32_t, uint8_t, uint8_t> *axi;
AXIResponder<32, uint32_t, uint8_t, uint8_t>::connections conn;

uint8_t dummy_out8;
uint8_t const_zero8 = 0;
uint32_t const_zero32 = 0;
uint8_t const_four8 = 4;

static uint64_t trace_tick = 0;

void mem_read(uint32_t addr, uint8_t len, void *buf) {
    uint8_t mylen = len;
    uint32_t myaddr = addr;
    uint32_t *buf_u32 = (uint32_t *)buf;
    uint8_t *buf_u8 = (uint8_t *)buf;

    /* Workaround: It won't allow read >= word_t */
    while (mylen >= 4)
    {
        buf_u32[(myaddr - addr) / 4] = paddr_read(myaddr, 4);
        myaddr += 4;
        mylen -= 4;
    }

    while (mylen > 0)
    {
        buf_u8[(myaddr - addr)] = paddr_read(myaddr, 1);
        myaddr += 1;
        mylen -= 1;
    }
}
void mem_write(uint32_t addr, uint8_t len, void *buf) {
    uint8_t mylen = len;
    uint32_t myaddr = addr;
    uint32_t *buf_u32 = (uint32_t *)buf;
    uint8_t *buf_u8 = (uint8_t *)buf;

    if (len <= 4) {
        paddr_write(addr, *buf_u32, len);
        return;
    }

    /* Workaround: It won't allow write >= word_t */
    while (mylen >= 4)
    {
        paddr_write(myaddr, (word_t)buf_u32[(myaddr - addr) / 4], 4);
        myaddr += 4;
        mylen -= 4;
    }

    while (mylen > 0)
    {
        paddr_write(myaddr, (word_t)buf_u32[(myaddr - addr) / 4], 1);
        myaddr += 1;
        mylen -= 1;
    }
}


inline void trace_eval() {
    tb->eval();
    if (m_trace) {
        m_trace->dump(trace_tick);
        m_trace->flush();
        trace_tick++;
    }
}

extern "C" int Vinit(int argc, char **argv) {
    int i;
    // Initialize Verilators variables
    Verilated::commandArgs(argc, argv);
    tb = new Vpicorv32_axi;

    if (trace_file) {
        Verilated::traceEverOn(true);
        m_trace = new VerilatedVcdC;
        tb->trace(m_trace, 99);
        m_trace->open(trace_file);
    }

    tb->mem_axi_awvalid = 0;

	conn = {
		.aw_awvalid = &tb->mem_axi_awvalid,
		.aw_awready = &tb->mem_axi_awready,
		.aw_awaddr = &tb->mem_axi_awaddr,
		
		.w_wvalid = &tb->mem_axi_wvalid,
		.w_wready = &tb->mem_axi_wready,
		.w_wdata = &tb->mem_axi_wdata,
		.w_wstrb = &tb->mem_axi_wstrb,
		
		.b_bvalid = &tb->mem_axi_bvalid,
		.b_bready = &tb->mem_axi_bready,

		.ar_arvalid = &tb->mem_axi_arvalid,
		.ar_arready = &tb->mem_axi_arready,
		.ar_araddr = &tb->mem_axi_araddr,
	
		.r_rvalid = &tb->mem_axi_rvalid,
		.r_rready = &tb->mem_axi_rready,
		.r_rdata = &tb->mem_axi_rdata,
	};

    axi = new AXIResponder<32, uint32_t, uint8_t, uint8_t>(conn, "main_mem");
    axi->mem_read = mem_read;
    axi->mem_write = mem_write;

    tb->resetn = 0;
    // Reset CPU for a while
    for (i = 0; i < 200; i++) {
       tb->clk = !tb->clk;
       trace_eval();
    }

    tb->resetn = 1;

    return 0;
}

void check_trap() {
    if (tb->trap) {
        nemu_state.state = NEMU_ABORT;
        nemu_state.halt_pc = tb->mem_axi_araddr;
        printf("picorv32 trapped (pc is last mem rw addr)\n");
    }
}

void signal_processing() {
    check_trap();
    axi->eval();
}

int Vtick_cpu() {
    if (Verilated::gotFinish()) {
        nemu_state.state = NEMU_ABORT; 
        printf("verilator report finished\n");
    }

    tb->clk = 1;
    trace_eval();
    signal_processing();
    tb->clk = 0;
    trace_eval();


    return 0;
}


static uint32_t redirect [] = {
  0x000002b7,  // lui  t0,0x00000
  0x00028067,  // addi t0, 0
  0x0002a503,  // jr   (t0)
};

static const uint32_t img [] = {
  0x000012b7,  // lui t0,0x00000
  0x0002a503,  // lw  a0,0(t0)
  0x0002a023,  // sw  zero,0(t0)
  0x0000006b,  // nemu_trap
};

void build_entry(int32_t entry) {
    uint32_t hi20, lo12;
    hi20 = (entry + 0x800) & 0xfffff000;
    lo12 = (entry - hi20) & 0xfff;
    redirect[0] |= hi20;
    redirect[1] |= lo12 << 20;
}

extern "C" void init_isa() {
  build_entry(PMEM_BASE + IMAGE_START);
  /* Load redirecting section. */
  memcpy(guest_to_host(0x00000000), redirect, sizeof(redirect));
  /* Load built-in image. */
  memcpy(guest_to_host(IMAGE_START), img, sizeof(img));
  Vinit(0, NULL);
}

extern "C" vaddr_t isa_exec_once() {
    Vtick_cpu();
    return 0;
}

extern "C" void isa_reg_display() {
}

extern "C" word_t isa_reg_str2val(const char *s, bool *success) {
  return 0;
}

