# Host tests for the hardware-free modules of the STM32 firmware: make -C stm32/firmware/test/host
CJSON := ../../third_party/cjson
TLSF := ../../third_party/tlsf
INC := -I../../components/cfgmodel/include -I../../components/proto/include -I../../components/arena/include -I$(CJSON) -I$(TLSF) -I../../components/board/include -I../../boot -I../../components/ota/include -I.
# UBSan findings stop the test (-fno-sanitize-recover), so a NULL dereference can never pass unnoticed.
CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(INC)
BUILD := build
CFG := ../../components/cfgmodel/cfgmodel.c ../../components/proto/ipacl.c ../../components/proto/jsonscan.c
ARENA := ../../components/arena/arena.c $(TLSF)/tlsf.c

all: $(BUILD)/test_cfgmodel $(BUILD)/test_proto $(BUILD)/test_jsonscan $(BUILD)/test_arena $(BUILD)/test_text_small $(BUILD)/test_edge_text $(BUILD)/test_oom $(BUILD)/test_board $(BUILD)/test_ota $(BUILD)/mkimage_sample.img $(BUILD)/test_install
	$(BUILD)/test_cfgmodel
	$(BUILD)/test_proto
	$(BUILD)/test_jsonscan
	ASAN_OPTIONS=detect_invalid_pointer_pairs=2 $(BUILD)/test_arena
	$(BUILD)/test_text_small
	$(BUILD)/test_edge_text
	$(BUILD)/test_oom
	$(BUILD)/test_board
	$(BUILD)/test_ota $(BUILD)/mkimage_sample.img
	$(BUILD)/test_install

$(BUILD)/test_cfgmodel: test_cfgmodel.c $(CFG) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_proto: test_proto.c $(wildcard ../../components/proto/*.c) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_jsonscan: test_jsonscan.c ../../components/proto/jsonscan.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

# pointer-compare: ownership checks must not compare pointers into different objects (C11 6.5.8p5).
$(BUILD)/test_arena: test_arena.c $(ARENA) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=pointer-compare $^ -lm -o $@

$(BUILD)/test_text_small: test_text_small.c $(CFG) $(ARENA) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_edge_text: test_edge_text.c $(CFG) $(ARENA) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_oom: test_oom.c $(CFG) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD)/test_board: test_board.c ../../components/board/tick64.c ../../components/board/crc32.c ../../boot/jumpcheck.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

OTA := ../../components/ota/sha256.c ../../components/ota/ota_image.c ../../components/board/crc32.c

$(BUILD)/test_ota: test_ota.c nor_sim.c $(OTA)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

# tools/test_mkimage.py checks mkimage.py and writes the sample image test_ota checks in C.
$(BUILD)/mkimage_sample.img: ../../tools/mkimage.py ../../tools/test_mkimage.py
	mkdir -p $(BUILD)
	python3 ../../tools/test_mkimage.py $@

INSTALL := ../../boot/install.c ../../components/ota/ota_app.c

# Under ASan: three matrices cut power at every operation of an install, a restore and a seeding, plus
# 300 runs with up to three cuts each. OTA_QUICK=1 skips the matrices (mutants_ota.py uses it).
$(BUILD)/test_install: test_install.c nor_sim.c $(INSTALL) $(OTA)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../../boot $^ -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
