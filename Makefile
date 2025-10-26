HDR := $(wildcard src/*.h) $(wildcard include/*.h)
SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)

CC     ?= cc
CFLAGS += -O3 -Wall -Wextra -Werror=pedantic -Werror=vla -Iinclude -std=c99

lib/libvkomp.a: lib $(OBJ)
	ar rcs -o $@ $(OBJ)

lib:
	@mkdir -p lib

%.o: %.c $(HDR)
	$(CC) $(CFLAGS) -c -o $@ $<

test: lib/libvkomp.a
	make -C tests run

.PHONY: clean
clean:
	rm -rf $(OBJ) lib
	make -C tests clean
	make -C example clean
