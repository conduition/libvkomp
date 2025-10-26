CC ?= cc
GLSLC ?= glslangValidator

WORK_GROUP_COUNT := 16

example: main.c shader.h
	$(CC) -o example main.c -I../include -L../lib -lvkomp -lvulkan -DWORK_GROUP_COUNT=$(WORK_GROUP_COUNT)

shader.h: shader.spv
	xxd -i shader.spv shader.h

shader.spv: shader.comp
	$(GLSLC) --target-env vulkan1.2 -DWORK_GROUP_COUNT=$(WORK_GROUP_COUNT) -o shader.spv shader.comp

.PHONY: clean
clean:
	rm -f example shader.spv shader.h
