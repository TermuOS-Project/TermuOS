#include "Kernel.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg)
{
    __asm__ volatile("outb %0, %1" :: "a"(reg), "Nd"((uint16_t)CMOS_ADDR));
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"((uint16_t)CMOS_DATA));
    return val;
}

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

static void rtc_wait(void)
{
    while (cmos_read(0x0A) & 0x80);
}

void rtc_read(rtc_time_t *t)
{
    rtc_wait();

    uint8_t second = cmos_read(0x00);
    uint8_t minute = cmos_read(0x02);
    uint8_t hour   = cmos_read(0x04);
    uint8_t day    = cmos_read(0x07);
    uint8_t month  = cmos_read(0x08);
    uint8_t year   = cmos_read(0x09);

    uint8_t status_b = cmos_read(0x0B);

    // Convert BCD into binary if bit 2 of status B is not set
    if (!(status_b & 0x04))
    {
        second = bcd_to_bin(second);
        minute = bcd_to_bin(minute);
        hour   = bcd_to_bin(hour & 0x7F); // Mask AM/PM bit
        day    = bcd_to_bin(day);
        month  = bcd_to_bin(month);
        year   = bcd_to_bin(year);
    }

    // 12-hour to 24-hour
    if (!(status_b & 0x02) && (cmos_read(0x04) & 0x80))
        hour = (hour + 12) % 24;

    t->second = second;
    t->minute = minute;
    t->hour   = hour;
    t->day    = day;
    t->month  = month;
    t->year   = 2000 + year;
}

