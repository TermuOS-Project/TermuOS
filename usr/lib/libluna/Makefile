TERMUOS_ROOT ?= $(abspath ../../..)
CC = gcc
CFLAGS = -static -nostdlib -ffreestanding -fno-stack-protector \
		 -O2 -Iinclude -I$(TERMUOS_ROOT)/usr/lib/tlibc/include \
		 -I$(TERMUOS_ROOT)/usr/lib/libtermuos/include

OBJS = src/luna_client.o
OUT = libluna.a

all: $(OUT)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT): $(OBJS)
	ar rcs $@ $(OBJS)

clean:
	rm -f $(OBJS) $(OUT)
