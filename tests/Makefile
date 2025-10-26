TEST_RUNNER := runner

HDR   := $(wildcard *.h)
SRC   := $(filter-out $(TEST_RUNNER).c,$(wildcard *.c))
TESTS := $(SRC:.c=.test)

CC     ?= cc
CFLAGS += -O3 -Wall -Wextra -Werror=pedantic -Werror=vla -I../include

# Print test debug info
ifeq ($(DEBUG), 1)
	CFLAGS += -DDEBUG=1
endif

GLSLC      ?= glslangValidator
GLSLCFLAGS += --target-env vulkan1.2

# glslangValidator prints file names when compiling unless we give it the --quiet flag.
ifeq ($(GLSLC), glslangValidator)
	GLSLCFLAGS += --quiet
endif

SHADERS    := $(wildcard shaders/*.comp)
SPV        := $(SHADERS:.comp=.spv)
SHADER_HDR := $(SPV:.spv=.h)

DEPS := ../lib/libvkomp.a ../include/vkomp.h

$(TEST_RUNNER): $(TEST_RUNNER).c $(TESTS) $(HDR) $(DEPS)
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: run
run: $(TEST_RUNNER) $(TESTS)
	./$(TEST_RUNNER)

%.test: %.c $(SHADER_HDR) $(HDR) $(DEPS)
	$(CC) $(CFLAGS) -o $@ $< -L../lib -lvkomp -lvulkan

%.spv: %.comp
	$(GLSLC) $(GLSLCFLAGS) -o $@ $<

shaders/%.h: shaders/%.spv
	cd shaders && xxd -i $*.spv $*.h

.PHONY: clean
clean:
	rm -f *.test $(TEST_RUNNER) shaders/*.spv shaders/*.h
