# Host tests for the hardware-free modules of the STM32 firmware: make -C stm32/firmware/test/host
CJSON := ../../third_party/cjson
CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined \
          -I../../components/cfgmodel/include -I../../components/proto/include -I$(CJSON)
BUILD := build

all: $(BUILD)/test_cfgmodel $(BUILD)/test_proto
	$(BUILD)/test_cfgmodel
	$(BUILD)/test_proto

$(BUILD)/test_cfgmodel: test_cfgmodel.c ../../components/cfgmodel/cfgmodel.c ../../components/proto/ipacl.c $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_proto: test_proto.c $(wildcard ../../components/proto/*.c) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
