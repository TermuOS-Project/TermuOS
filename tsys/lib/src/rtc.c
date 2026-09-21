#include <rtc.h>
#include <syscall.h>

int rtc_read(rtc_time_t *t)
{
    if (!t)
        return -1;
    return (int)__syscall1(SYS_RTC_READ, (long)t);
}
