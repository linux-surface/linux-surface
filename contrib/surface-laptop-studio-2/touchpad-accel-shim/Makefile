CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror -fPIC
LDFLAGS ?= -shared -ldl -lm

all: touchpad-accel.so

touchpad-accel.so: touchpad-accel.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f touchpad-accel.so
