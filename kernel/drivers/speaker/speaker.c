#include "speaker.h"
#include "../driver.h"
#include "../../lib/printf.h"
#include <stdint.h>

extern volatile uint64_t timer_ticks;

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0,%1" ::"a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

static void speaker_on(unsigned freq_hz)
{
    if (freq_hz < 20)
        freq_hz = 20;
    if (freq_hz > 20000)
        freq_hz = 20000;

    uint32_t div = 1193182u / freq_hz;
    if (div == 0)
        div = 1;

    outb(0x43, 0xB6); /* ch2, lobyte/hibyte, square wave */
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)((div >> 8) & 0xFF));

    uint8_t t = inb(0x61);
    outb(0x61, t | 0x03); /* gate + data enable */
}

static void speaker_off(void)
{
    uint8_t t = inb(0x61);
    outb(0x61, t & 0xFC);
}

static void sleep_ms(unsigned ms)
{
    uint64_t need = (uint64_t)ms / 10;
    if (need == 0)
        need = 1;
    uint64_t start = timer_ticks;
    while (timer_ticks - start < need)
        __asm__ volatile("pause");
}

void speaker_beep(unsigned freq_hz, unsigned ms)
{
    speaker_on(freq_hz);
    sleep_ms(ms);
    speaker_off();
    sleep_ms(20);
}

enum {
    N_REST = 0,
    N_C4 = 262, N_D4 = 294, N_E4 = 330, N_F4 = 349,
    N_G4 = 392, N_A4 = 440, N_B4 = 494,
    N_C5 = 523, N_D5 = 587, N_E5 = 659, N_F5 = 698,
    N_G5 = 784, N_A5 = 880, N_B5 = 988,
    N_C6 = 1047
};

typedef struct {
    unsigned freq;
    unsigned ms;
} note_t;

static void play_notes(const note_t *seq, int n)
{
    for (int i = 0; i < n; i++) {
        if (seq[i].freq == N_REST)
            sleep_ms(seq[i].ms);
        else
            speaker_beep(seq[i].freq, seq[i].ms);
    }
}

void speaker_jingle(void)
{
    static const note_t a[] = {
        { N_C5, 90 }, { N_E5, 90 }, { N_G5, 90 }, { N_C6, 140 },
        { N_REST, 40 },
        { N_G5, 70 }, { N_C6, 180 },
    };

    static const note_t b[] = {
        { N_REST, 50 },
        { N_A5, 80 }, { N_G5, 80 }, { N_E5, 80 }, { N_C5, 100 },
        { N_REST, 30 },
        { N_E5, 70 }, { N_G5, 70 }, { N_C6, 200 },
    };

    static const note_t c[] = {
        { N_REST, 40 },
        { N_G5, 60 }, { N_A5, 60 }, { N_B5, 60 },
        { N_C6, 120 }, { N_G5, 80 }, { N_C6, 220 },
    };

    play_notes(a, (int)(sizeof a / sizeof a[0]));
    play_notes(b, (int)(sizeof b / sizeof b[0]));
    play_notes(c, (int)(sizeof c / sizeof c[0]));

    speaker_off();
}

static int speaker_init(void)
{
    speaker_off();
    speaker_jingle();
    kprintf("speaker: jingle done\n");
    return 0;
}

void speaker_driver_register(void)
{
    static const driver_t d = {
        .name = "pc-speaker",
        .init = speaker_init,
        .priority = 90
    };
    driver_register(&d);
}
