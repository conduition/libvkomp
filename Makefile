HDR := $(wildcard src/*.h) $(wildcard include/*.h)
SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)

CC     ?= cc
CFLAGS += -O3 -Wall -Wextra -Werror=pedantic -Werror=vla -Iinclude

libvkomp.a: $(OBJ)
	ar rcs -o $@ $(OBJ)

%.o: %.c $(HDR)
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJ) libvkomp.a
