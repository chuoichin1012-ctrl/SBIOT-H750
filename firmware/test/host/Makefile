# Host tests for the hardware-free modules of the STM32 firmware: make -C stm32/firmware/test/host
CJSON := ../../third_party/cjson
TLSF := ../../third_party/tlsf
INC := -I../../components/cfgmodel/include -I../../components/proto/include -I../../components/arena/include -I$(CJSON) -I$(TLSF)
# UBSan findings stop the test (-fno-sanitize-recover), so a NULL dereference can never pass unnoticed.
CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(INC)
BUILD := build
CFG := ../../components/cfgmodel/cfgmodel.c ../../components/proto/ipacl.c
ARENA := ../../components/arena/arena.c $(TLSF)/tlsf.c

all: $(BUILD)/test_cfgmodel $(BUILD)/test_proto $(BUILD)/test_arena
	$(BUILD)/test_cfgmodel
	$(BUILD)/test_proto
	$(BUILD)/test_arena

$(BUILD)/test_cfgmodel: test_cfgmodel.c $(CFG) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_proto: test_proto.c $(wildcard ../../components/proto/*.c) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_arena: test_arena.c $(ARENA) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
