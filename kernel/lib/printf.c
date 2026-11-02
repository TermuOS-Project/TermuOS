#include "Kernel.h"

static void default_putchar(char c)
{
    terminal_putchar(c);
    serial_putchar(c);
}

static void (*_putchar_fn)(char) = default_putchar;

static void null_putchar(char c) { (void)c; }

void kprintf_set_output(void (*putchar_fn)(char))
{
    _putchar_fn = putchar_fn ? putchar_fn : null_putchar;
}

static void print_uint(uint64_t n, int base)
{
    static const char digits[] = "0123456789abcdef";
    char buf[64];
    int i = 0;
    if (n == 0)
    {
        _putchar_fn('0');
        return;
    }
    while (n)
    {
        buf[i++] = digits[n % base];
        n /= base;
    }
    while (i--)
        _putchar_fn(buf[i]);
}

void kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    for (; *fmt; fmt++)
    {
        if (*fmt != '%')
        {
            _putchar_fn(*fmt);
            continue;
        }
        fmt++;

        /* flags */
        int zero_pad = 0;
        if (*fmt == '0')
        {
            zero_pad = 1;
            fmt++;
        }

        /* width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
        {
            width = width * 10 + (*fmt++ - '0');
        }

        switch (*fmt)
        {
        case 'c':
            _putchar_fn((char)va_arg(args, int));
            break;
        case 's':
        {
            const char *s = va_arg(args, const char *);
            while (s && *s)
                _putchar_fn(*s++);
            break;
        }
        case 'd':
        {
            int64_t n = va_arg(args, int64_t);
            if (n < 0)
            {
                _putchar_fn('-');
                n = -n;
            }
            print_uint((uint64_t)n, 10);
            break;
        }
        case 'u':
            print_uint(va_arg(args, uint64_t), 10);
            break;
        case 'x':
        case 'X':
        {
            uint64_t n = va_arg(args, uint64_t);
            if (width > 0)
            {
                /* format into tmp buf then pad */
                static const char digits[] = "0123456789abcdef";
                char buf[16];
                int i = 0;
                uint64_t tmp = n;
                if (tmp == 0)
                    buf[i++] = '0';
                while (tmp)
                {
                    buf[i++] = digits[tmp % 16];
                    tmp /= 16;
                }
                char pad = zero_pad ? '0' : ' ';
                for (int p = i; p < width; p++)
                    _putchar_fn(pad);
                while (i--)
                    _putchar_fn(buf[i]);
            }
            else
            {
                print_uint(n, 16);
            }
            break;
        }
        case 'p':
            _putchar_fn('0');
            _putchar_fn('x');
            print_uint((uint64_t)va_arg(args, void *), 16);
            break;
        case '%':
            _putchar_fn('%');
            break;
        default:
            _putchar_fn('?');
            break;
        }
    }

    va_end(args);
}
