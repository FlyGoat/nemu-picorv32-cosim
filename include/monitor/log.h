#ifndef __MONITOR_LOG_H__
#define __MONITOR_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <common.h>
extern bool log_enable();

#ifdef DEBUG
extern FILE* log_fp;
#	define log_write(...) \
  do { \
    if (log_fp != NULL && log_enable()) { \
      fprintf(log_fp, __VA_ARGS__); \
      fflush(log_fp); \
    } \
  } while (0)
#else
#	define log_write(...)
#endif

#define _Log(...) \
  do { \
    printf(__VA_ARGS__); \
    log_write(__VA_ARGS__); \
  } while (0)

void strcatf(char *buf, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
