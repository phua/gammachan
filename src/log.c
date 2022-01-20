#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#include "../include/log.h"

#define LOG_DEFAULT_FILENAME "log/gammachan.log"

static Log log_defaultLog = NULL;

int log_open(Log *streamp, const char *filename)
{
  if (!*streamp) {
    *streamp = fopen(filename, "w+");
    if (!*streamp) {
      perror("fopen");
      return -1;
    }
  }
  return 0;
}

void log_close(Log stream)
{
  if (stream) {
    if (fclose(stream) == EOF) {
      perror("fclose");
    }
    stream = NULL;
  }
}

int log_openDefault()
{
  return log_open(&log_defaultLog, LOG_DEFAULT_FILENAME);
}

void log_closeDefault()
{
  log_close(log_defaultLog);
  log_defaultLog = NULL;
}

static const char *strlevel(LogLevel level)
{
  switch (level) {
  case ALL:   return "ALL";
  case TRACE: return "TRACE";
  case DEBUG: return "DEBUG";
  case INFO:  return "INFO";
  case WARN:  return "WARN";
  case ERROR: return "ERROR";
  case FATAL: return "FATAL";
  default:    return "UNKNOWN";
  }
}

static void log_vabase(Log stream, LogLevel level, const char *fmt, va_list ap)
{
  struct timespec ts;
  timespec_get(&ts, TIME_UTC);
  struct tm *tm = gmtime(&ts.tv_sec);

  char buf[24];
  strftime(buf, sizeof(buf), "%F %T", tm);
  fprintf(stream, "%s.%09ld - %s ", buf, ts.tv_nsec, strlevel(level));

  vfprintf(stream, fmt, ap);
}

void log_default(const char *fmt, ...)
{
  if (!log_defaultLog) {
    if (log_openDefault() == 0) {
      atexit(log_closeDefault);
    }
  }
  va_list ap;
  va_start(ap, fmt);
  log_vabase(log_defaultLog, INFO, fmt, ap);
  va_end(ap);
}

#define X_LOG_LEVELS                            \
  X_LOG_LEVEL(TRACE, trace)                     \
  X_LOG_LEVEL(DEBUG, debug)                     \
  X_LOG_LEVEL(INFO , info )                     \
  X_LOG_LEVEL(WARN , warn )                     \
  X_LOG_LEVEL(ERROR, error)                     \
  X_LOG_LEVEL(FATAL, fatal)

#define X_LOG_LEVEL(LEVEL, NAME)                    \
  void log_##NAME(Log stream, const char *fmt, ...) \
  {                                                 \
    va_list ap;                                     \
    va_start(ap, fmt);                              \
    log_vabase(stream, LEVEL, fmt, ap);             \
    va_end(ap);                                     \
  }
X_LOG_LEVELS
#undef X_LOG_LEVEL
