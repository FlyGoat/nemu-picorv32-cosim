#include <stdlib.h>
#include <stdint.h>
#include <isa.h>
#include <memory/paddr.h>
#include <monitor/monitor.h>

#include "Vpicorv32.h"
#include "verilated_vcd_c.h"

Vpicorv32 *tb;
VerilatedVcdC	*m_trace;

static uint64_t trace_tick = 0;

inline void trace_eval() {
    tb->eval();
    if (m_trace) {
        m_trace->dump(trace_tick);
        trace_tick++;
    }
}

extern "C" int Vinit(int argc, char **argv) {
    int i;
    // Initialize Verilators variables
    Verilated::commandArgs(argc, argv);
    tb = new Vpicorv32;

    if (trace_file) {
        Verilated::traceEverOn(true);
        m_trace = new VerilatedVcdC;
        tb->trace(m_trace, 99);
        m_trace->open(trace_file);
    }

    tb->resetn = 0;
    // Reset CPU for a while
    for (i = 0; i < 200; i++) {
       tb->clk = !tb->clk;
       trace_eval();
    }

    tb->resetn = 1;
    tb->mem_ready = 1;

    return 0;
}

void process_bus() {
    if (!(tb->mem_ready && tb->mem_valid))
        return;

    if (tb->mem_wstrb) {
        int shift;
        int length;
        /* is_write */
        switch (tb->mem_wstrb) {
        case 0xf: /* 4'b1111 */
            shift = 0;
            length = 4;
            break;
        case 0x1: /* 4'b0001 */
            shift = 0;
            length = 1;
            break;
        case 0x2: /* 4'b0010 */
            shift = 1;
            length = 1;
            break;
        case 0x4: /* 4'b0100 */
            shift = 2;
            length = 1;
            break;
        case 0x8: /* 4'b1000 */
            shift = 3;
            length = 1;
            break;
        case 0x3: /* 4'b0011 */
            shift = 0;
            length = 2;
            break;
        case 0xc: /* 4'b1100 */
            shift = 2;
            length = 2;
            break;
        default:
            assert(0);
            break;
        }
        paddr_write(tb->mem_addr + shift, tb->mem_wdata >> (shift*8), length);
    } else {
        /* is_read */
        tb->mem_rdata = paddr_read(tb->mem_addr, 4);
    }
}

void check_trap() {
    if (tb->trap) {
        nemu_state.state = NEMU_ABORT;
        nemu_state.halt_pc = tb->mem_addr;
        printf("picorv32 trapped (pc is last mem rw addr)\n");
    }
}

void signal_processing() {
    check_trap();
    process_bus();
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

    if (m_trace)
      m_trace->flush();

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

