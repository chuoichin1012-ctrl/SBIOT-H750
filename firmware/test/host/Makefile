# Host tests for the hardware-free modules of the STM32 firmware: make -C stm32/firmware/test/host
CJSON := ../../third_party/cjson
TLSF := ../../third_party/tlsf
INC := -I../../components/cfgmodel/include -I../../components/proto/include -I../../components/arena/include -I$(CJSON) -I$(TLSF) -I../../components/board/include -I../../boot -I../../components/ota/include -I.
# UBSan findings stop the test (-fno-sanitize-recover), so a NULL dereference can never pass unnoticed.
CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(INC)
BUILD := build
CFG := ../../components/cfgmodel/cfgmodel.c ../../components/proto/ipacl.c ../../components/proto/jsonscan.c
ARENA := ../../components/arena/arena.c $(TLSF)/tlsf.c

all: $(BUILD)/test_cfgmodel $(BUILD)/test_proto $(BUILD)/test_jsonscan $(BUILD)/test_arena $(BUILD)/test_text_small $(BUILD)/test_edge_text $(BUILD)/test_oom $(BUILD)/test_board $(BUILD)/test_ota $(BUILD)/mkimage_sample.img $(BUILD)/test_install $(BUILD)/bind_sample.bin $(BUILD)/test_bind $(BUILD)/test_store $(BUILD)/test_store_small $(BUILD)/test_heap $(BUILD)/test_ip101 $(BUILD)/test_netcfg $(BUILD)/test_dhcp_dns $(BUILD)/test_rng $(BUILD)/test_clock $(BUILD)/test_web_core $(BUILD)/test_serial_frame $(BUILD)/test_io_core $(BUILD)/test_adc_core $(BUILD)/test_reload $(BUILD)/test_bridge_core $(BUILD)/test_io_mb $(BUILD)/test_tls_core $(BUILD)/test_tlsconn $(BUILD)/test_edge_core $(BUILD)/test_mqtt_wire $(BUILD)/test_mqtt_transport $(BUILD)/test_mqtt_outbox $(BUILD)/test_mqtt_session
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
	$(BUILD)/test_ip101
	$(BUILD)/test_netcfg
	$(BUILD)/test_dhcp_dns
	$(BUILD)/test_rng
	$(BUILD)/test_clock
	$(BUILD)/test_web_core
	$(BUILD)/test_serial_frame
	$(BUILD)/test_io_core
	$(BUILD)/test_adc_core
	$(BUILD)/test_reload
	$(BUILD)/test_bridge_core
	$(BUILD)/test_io_mb
	$(BUILD)/test_tls_core
	$(BUILD)/test_tlsconn
	$(BUILD)/test_edge_core
	$(BUILD)/test_mqtt_wire
	$(BUILD)/test_mqtt_transport
	$(BUILD)/test_mqtt_outbox
	$(BUILD)/test_mqtt_session

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

NET := ../../components/netmgr

$(BUILD)/test_ip101: test_ip101.c $(NET)/ip101.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(NET)/include $^ -o $@

$(BUILD)/test_netcfg: test_netcfg.c $(NET)/netcfg.c ../../components/ota/sha256.c $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(NET)/include $^ -lm -o $@

# lwIP's own DHCP client and DNS on the host (NO_SYS), with the SBIOT patch. EXTRA adds flags (RED runs).
LWIP := ../../third_party/lwip/src
LWIP_HOST := $(addprefix $(LWIP)/core/,init.c def.c dns.c inet_chksum.c ip.c mem.c memp.c netif.c pbuf.c stats.c sys.c timeouts.c udp.c) \
  $(addprefix $(LWIP)/core/ipv4/,acd.c dhcp.c etharp.c icmp.c ip4.c ip4_addr.c ip4_frag.c) $(LWIP)/netif/ethernet.c

$(BUILD)/test_dhcp_dns: test_dhcp_dns.c $(LWIP_HOST) lwip/lwipopts.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Wno-address -Ilwip -I$(LWIP)/include $(EXTRA) $(filter %.c,$^) -o $@

$(BUILD)/test_rng: test_rng.c ../../components/board/rng_core.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_clock: test_clock.c ../../components/sysmgr/clock.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -I../../components/sysmgr/include $^ -o $@

$(BUILD)/test_web_core: test_web_core.c ../../components/webapi/web_core.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../../components/webapi/include $^ -o $@

$(BUILD)/test_serial_frame: test_serial_frame.c ../../components/serial/serial_frame.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../../components/serial/include $^ -o $@

$(BUILD)/test_io_core: test_io_core.c ../../components/iomgr/io_core.c $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../../components/iomgr/include $^ -o $@

$(BUILD)/test_adc_core: test_adc_core.c ../../components/iomgr/adc_core.c ../../components/board/crc32.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../../components/iomgr/include $^ -lm -o $@

$(BUILD)/test_reload: test_reload.c ../../components/sysmgr/reload.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../../components/sysmgr/include $^ -o $@

$(BUILD)/test_bridge_core: test_bridge_core.c ../../components/services/bridge_core.c ../../components/proto/ipacl.c ../../components/proto/modbus.c $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../../components/services/include $^ -o $@

$(BUILD)/test_io_mb: test_io_mb.c ../../components/iomgr/io_mb.c ../../components/proto/modbus.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../../components/iomgr/include $^ -o $@

# mbedTLS 3.6 (Plan 08b): the library once, as the board builds it (tls_config.h) plus the test server
# (TLS_HOST_TEST), without sanitizers; our own TLS code keeps them. Fixtures from mkpki.py.
MBED := ../../third_party/mbedtls
MBED_INC := -I$(MBED)/include -I../../components/services/include -DMBEDTLS_CONFIG_FILE='"tls_config.h"' -DTLS_HOST_TEST
MBED_OBJ := $(patsubst $(MBED)/library/%.c,$(BUILD)/mbedtls/%.o,$(wildcard $(MBED)/library/*.c))
$(BUILD)/mbedtls/%.o: $(MBED)/library/%.c ../../components/services/include/tls_config.h
	mkdir -p $(BUILD)/mbedtls
	$(CC) -std=c11 -O1 -g -D_DEFAULT_SOURCE $(MBED_INC) -I$(MBED)/library -c $< -o $@
$(BUILD)/libmbedtls_host.a: $(MBED_OBJ)
	rm -f $@
	ar rcs $@ $^
$(BUILD)/pki/ca.pem: mkpki.py
	python3 mkpki.py $(BUILD)/pki --ip 127.0.0.1 --dns localhost

$(BUILD)/test_tls_core: test_tls_core.c tls_test_port.c ../../components/services/tls_core.c ../../components/sysmgr/clock.c $(BUILD)/libmbedtls_host.a $(BUILD)/pki/ca.pem
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE $(MBED_INC) -I../../components/services/include -I../../components/sysmgr/include $(filter %.c %.a,$^) -o $@

$(BUILD)/test_tlsconn: test_tlsconn.c tls_test_port.c ../../components/services/tlsconn.c ../../components/services/tls_core.c ../../components/services/bridge_core.c ../../components/sysmgr/clock.c ../../components/proto/ipacl.c ../../components/proto/modbus.c $(CJSON)/cJSON.c $(BUILD)/libmbedtls_host.a $(BUILD)/pki/ca.pem
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE $(MBED_INC) -I../../components/sysmgr/include $(filter %.c %.a,$^) -lpthread -Wl,--wrap=calloc -o $@

$(BUILD)/test_edge_core: test_edge_core.c ../../components/services/edge_core.c ../../components/proto/edgecore.c ../../components/proto/modbus.c ../../components/sysmgr/clock.c $(CFG) $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../../components/services/include -I../../components/sysmgr/include $^ -lm -Wl,--wrap=malloc -Wl,--wrap=calloc -o $@

# coreMQTT 2.3.1 with the SBIOT patch (Plan 09), built without our warnings as upstream code.
MQTT := ../../third_party/coremqtt/source
MQTT_INC := -I$(MQTT)/include -I$(MQTT)/interface -I../../components/services/include
MQTT_SRC := $(MQTT)/core_mqtt.c $(MQTT)/core_mqtt_serializer.c $(MQTT)/core_mqtt_state.c
$(BUILD)/test_mqtt_wire: test_mqtt_wire.c $(MQTT_SRC)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -Wno-error $(MQTT_INC) $^ -o $@

$(BUILD)/test_mqtt_transport: test_mqtt_transport.c ../../components/services/mqtt_transport.c ../../components/services/bridge_core.c ../../components/proto/ipacl.c ../../components/proto/modbus.c $(CJSON)/cJSON.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE $(MQTT_INC) $^ -o $@

$(BUILD)/test_mqtt_outbox: test_mqtt_outbox.c ../../components/services/mqtt_outbox.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../../components/services/include $^ -o $@

$(BUILD)/test_mqtt_session: test_mqtt_session.c ../../components/services/mqtt_sess.c ../../components/services/mqtt_outbox.c ../../components/services/mqtt_transport.c ../../components/services/bridge_core.c ../../components/proto/ipacl.c ../../components/proto/modbus.c $(CJSON)/cJSON.c $(MQTT_SRC)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -Wno-error $(MQTT_INC) $^ -lpthread -o $@
