//
//  Log.h
//  SimpleMtkBtFw
//
//  Created by laobamac on 2026/5/30.
//  Copyright © 2026 laobamac. All rights reserved.
//

#ifndef Log_h
#define Log_h

#include <IOKit/IOLib.h>
#include <stdarg.h>
#include <kern/clock.h>

/*
 * IOLog output from early boot is usually lost, so every message is also
 * kept in a small ring buffer that is published as the "MtkBtLog" property
 * on the USB device (see SimpleMtkBtFw::publishLog).
 */
#define MTKBT_LOG_SIZE 8192

inline char *mtkbt_log_buf() { static char buf[MTKBT_LOG_SIZE]; return buf; }
inline size_t &mtkbt_log_len() { static size_t len; return len; }

inline void mtkbt_log(const char *fmt, ...)
{
    char line[256];
    uint64_t ns;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    int n = snprintf(line, sizeof(line), "[%llu.%03llu] ", ns / 1000000000ULL, (ns / 1000000ULL) % 1000);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - n, fmt, ap);
    va_end(ap);
    size_t l = strlen(line);
    char *buf = mtkbt_log_buf();
    size_t &len = mtkbt_log_len();
    if (len + l + 1 > MTKBT_LOG_SIZE) {
        /* drop the older half */
        size_t keep = len / 2;
        memmove(buf, buf + len - keep, keep);
        len = keep;
    }
    memcpy(buf + len, line, l);
    len += l;
    buf[len] = '\0';
}

#define XYLog(fmt, x...)\
do\
{\
	IOLog("%s: " fmt, "SimpleMtkBtFw", ##x);\
	mtkbt_log(fmt, ##x);\
}while(0)

#endif /* Log_h */
