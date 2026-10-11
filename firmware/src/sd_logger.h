#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>
#include "config.h"

void initSdLogger();
void sdLogRecord(const char* line);
void sdQueueBacklog(const char* body);
void sdFlushBacklog();

#endif