#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct {
        uint8_t second;
        uint8_t minute;
        uint8_t hour;
        uint8_t day;
        uint8_t month;
        uint8_t year;
    } rtc_time_t;

    int rtc_read(rtc_time_t *t);

#ifdef __cplusplus
}
#endif
