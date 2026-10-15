# Host tests for the hardware-free modules of the STM32 firmware: make -C stm32/firmware/test/host
CJSON := ../../third_party/cjson
TLSF := ../../third_party/tlsf
INC := -I../../components/cfgmodel/include -I../../components/proto/include -I../../components/arena/include -I$(CJSON) -I$(TLSF) -I../../components/board/include -I../../boot -I../../components/ota/include -I.
# UBSan findings stop the test (-fno-sanitize-recover), so a NULL dereference can never pass unnoticed.
CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(INC)
BUILD := build
CFG := ../../components/cfgmodel/cfgmodel.c ../../components/proto/ipacl.c ../../components/proto/jsonscan.c
ARENA := ../../components/arena/arena.c $(TLSF)/tlsf.c

all: $(BUILD)/test_cfgmodel $(BUILD)/test_proto $(BUILD)/test_jsonscan $(BUILD)/test_arena $(BUILD)/test_text_small $(BUILD)/test_edge_text $(BUILD)/test_oom $(BUILD)/test_board $(BUILD)/test_ota $(BUILD)/mkimage_sample.img $(BUILD)/test_install $(BUILD)/bind_sample.bin $(BUILD)/test_bind $(BUILD)/test_store $(BUILD)/test_store_small $(BUILD)/test_heap
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
	$(BUILD)/test_bind $(BUILD)/bind_sample.bin
	$(BUILD)/test_store
	STORE_CUTS=1 ASAN_OPTIONS=detect_leaks=0 $(BUILD)/test_store
	$(BUILD)/test_store_small
	$(BUILD)/test_heap

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

$(BUILD)/test_board: test_board.c ../../components/board/tick64.c ../../components/board/crc32.c
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

MONO := ../../third_party/monocypher
BIND := ../../components/ota/bind.c $(MONO)/monocypher.c $(MONO)/monocypher-ed25519.c ../../components/board/crc32.c

$(BUILD)/test_bind: test_bind.c nor_sim.c $(BIND)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(MONO) $^ -o $@

# tools/test_provision.py checks provision.py and writes the records test_bind checks with Monocypher.
$(BUILD)/bind_sample.bin: ../../tools/provision.py ../../tools/test_provision.py
	mkdir -p $(BUILD)
	python3 ../../tools/test_provision.py $@ > /dev/null

FDB := ../../third_party/flashdb
STORE_INC := -I../../components/store/include -I../../components/store/port -I$(FDB)/inc -I$(FDB)/port/fal/inc
FDB_SRC := $(FDB)/src/fdb.c $(FDB)/src/fdb_kvdb.c $(FDB)/src/fdb_tsdb.c $(FDB)/src/fdb_utils.c \
           $(FDB)/port/fal/src/fal.c $(FDB)/port/fal/src/fal_flash.c $(FDB)/port/fal/src/fal_partition.c

# FlashDB is upstream code: built once with our sanitizers but without -Werror on its own warnings.
$(BUILD)/libflashdb.a: $(FDB_SRC) ../../components/store/port/fdb_cfg.h ../../components/store/port/fal_cfg.h
	mkdir -p $(BUILD)/fdb
	for f in $(FDB_SRC); do $(CC) $(filter-out -Werror,$(CFLAGS)) -w $(STORE_INC) -c $$f -o $(BUILD)/fdb/$$(basename $$f .c).o || exit 1; done
	ar rcs $@ $(BUILD)/fdb/*.o

# Two passes: the cases with leak detection, then (STORE_CUTS=1) a power cut at every operation of
# an edge save that collects garbage and of a small save, plus 150 runs with three cuts.
$(BUILD)/test_store: test_store.c nor_sim.c ../../components/store/store.c ../../components/store/fal_nor.c $(CFG) $(CJSON)/cJSON.c $(BUILD)/libflashdb.a
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(STORE_INC) $(filter %.c,$^) $(BUILD)/libflashdb.a -lm -o $@

# The same with a 3800-byte text cache: the "does not fit" path (test_cache_full).
$(BUILD)/test_store_small: test_store.c nor_sim.c ../../components/store/store.c ../../components/store/fal_nor.c $(CFG) $(CJSON)/cJSON.c $(BUILD)/libflashdb.a
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DSTORE_CACHE_SIZE=3800u $(STORE_INC) $(filter %.c,$^) $(BUILD)/libflashdb.a -lm -o $@

$(BUILD)/test_heap: test_heap.c ../../components/board/heap_wrap.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
