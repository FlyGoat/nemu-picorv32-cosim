#ifndef __MONITOR_MONITOR_H__
#define __MONITOR_MONITOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <common.h>

enum { NEMU_STOP, NEMU_RUNNING, NEMU_END, NEMU_ABORT, NEMU_QUIT };

typedef struct {
  int state;
  vaddr_t halt_pc;
  uint32_t halt_ret;
} NEMUState;

extern NEMUState nemu_state;

void display_inv_msg(vaddr_t pc);

#endif

#ifdef __cplusplus
}
#endif
