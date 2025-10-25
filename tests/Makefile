TEST_RUNNER := runner

HDR   := $(wildcard *.h)
SRC   := $(filter-out $(TEST_RUNNER).c,$(wildcard *.c))
TESTS := $(SRC:.c=.test)

CC     ?= cc
CFLAGS += -O3 -Wall -Wextra -Werror=pedantic -Werror=vla -I../include

GLSLC      ?= glslangValidator
GLSLCFLAGS += --target-env vulkan1.2

SHADERS    := $(wildcard shaders/*.comp)
SPV        := $(SHADERS:.comp=.spv)
SHADER_HDR := $(SPV:.spv=.h)

$(TEST_RUNNER): $(TEST_RUNNER).c $(TESTS) $(HDR)
	$(CC) $(CFLAGS) -o $@ $<

run: $(TEST_RUNNER) $(TESTS)
	./$(TEST_RUNNER)

%.test: %.c $(SHADER_HDR) $(HDR)
	$(CC) $(CFLAGS) -o $@ $< -L../lib -lvkomp -lvulkan

%.spv: %.comp
	$(GLSLC) $(GLSLCFLAGS) -o $@ $<

shaders/%.h: shaders/%.spv
	cd shaders && xxd -i $*.spv $*.h

.PHONY: clean
clean:
	rm -f *.test $(TEST_RUNNER) shaders/*.spv shaders/*.h
