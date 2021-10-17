#ifndef __ISA_PCIORV32_H__
#define __ISA_PICORV32_H__

#include <common.h>

#include <common.h>

// memory
#define picorv32_IMAGE_START 0x10000
#define picorv32_PMEM_BASE 0x00000000

// reg

typedef struct {
  struct {
    rtlreg_t _32;
  } gpr[32];

  vaddr_t pc;
} picorv32_CPU_state;

// decode
typedef struct {
} picorv32_ISADecodeInfo;

#define isa_vaddr_check(vaddr, type, len) (MEM_RET_OK)
#define picorv32_has_mem_exception() (false)

#endif
