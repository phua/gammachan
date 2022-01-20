#pragma once
#ifndef FLOD_H
#define FLOD_H

#include <stdio.h>

typedef FILE *Log;

typedef enum
{
  ALL, TRACE, DEBUG, INFO, WARN, ERROR, FATAL
} LogLevel;

int  log_open(Log *, const char *);
void log_close(Log);

int  log_openDefault();
void log_closeDefault();
void log_default(const char *, ...);

void log_trace(Log, const char *, ...);
void log_debug(Log, const char *, ...);
void log_info (Log, const char *, ...);
void log_warn (Log, const char *, ...);
void log_error(Log, const char *, ...);
void log_fatal(Log, const char *, ...);

#endif
