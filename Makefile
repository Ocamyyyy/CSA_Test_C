CROSS_COMPILE ?= aarch64-linux-gnu-
ifeq ($(origin CC),default)
CC := $(CROSS_COMPILE)gcc
endif
HOSTCC ?= gcc
CPPFLAGS += -Iinclude -D_FILE_OFFSET_BITS=64
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wformat=2
STATIC ?= 1
ifeq ($(STATIC),1)
LDFLAGS += -static
endif
M2_SOURCES := src/m2.c src/m2_gpio.c src/axis_switch.c
CHAIN_SOURCES := src/fft_m2.c src/fft_m2_orchestrator.c src/sha256.c
SOURCES := $(CHAIN_SOURCES) $(M2_SOURCES) src/batch_json.c src/batch_manifest.c src/batch.c src/main.c src/common.c src/dma_mmio.c src/udmabuf.c src/gpio_ctrl.c
TARGET ?= build/ffttest
.PHONY: all test host elf-check clean
all: $(TARGET)
$(TARGET): $(SOURCES) include/ffttest.h include/m2.h include/fft_m2.h
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@
host:
	$(MAKE) CC=$(HOSTCC) TARGET=build/ffttest-host STATIC=0 all
build/test_core: tests/test_core.c src/common.c src/dma_mmio.c src/udmabuf.c include/ffttest.h
	mkdir -p build
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) tests/test_core.c src/common.c src/dma_mmio.c src/udmabuf.c -o $@
build/test_gpio: tests/test_gpio.c src/gpio_ctrl.c src/common.c include/ffttest.h
	mkdir -p build
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) tests/test_gpio.c src/gpio_ctrl.c src/common.c -o $@
build/main-access.o: src/main.c include/ffttest.h include/m2.h include/fft_m2.h
	mkdir -p build
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) -Dmain=ffttest_entry -c src/main.c -o $@
build/test_access: tests/test_access.c build/main-access.o src/common.c src/batch.c src/batch_json.c src/batch_manifest.c include/ffttest.h
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) tests/test_access.c build/main-access.o src/common.c src/batch.c src/batch_json.c src/batch_manifest.c -o $@
build/test_batch: tests/batch_mock.c src/batch.c src/batch_json.c src/batch_manifest.c src/common.c src/dma_mmio.c src/udmabuf.c include/ffttest.h
	mkdir -p build
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) tests/batch_mock.c src/batch.c src/batch_json.c src/batch_manifest.c src/common.c src/dma_mmio.c src/udmabuf.c -Wl,--wrap=dma_unbound -Wl,--wrap=dma_open -Wl,--wrap=dma_close -Wl,--wrap=ubuf_info -Wl,--wrap=ubuf_open -Wl,--wrap=ubuf_close -Wl,--wrap=ubuf_sync -Wl,--wrap=gpio_discover -Wl,--wrap=gpio_acquire -Wl,--wrap=gpio_release -Wl,--wrap=fft_configure -Wl,--wrap=write -Wl,--wrap=read -Wl,--wrap=fsync -Wl,--wrap=now_ms -o $@
build/test_batch_json: tests/test_batch_json.c src/batch_json.c src/common.c include/ffttest.h
	mkdir -p build
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) tests/test_batch_json.c src/batch_json.c src/common.c -o $@
build/manifest_probe: tests/manifest_probe.c $(filter-out src/main.c,$(SOURCES)) include/ffttest.h
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) tests/manifest_probe.c $(filter-out src/main.c,$(SOURCES)) -o $@
build/test_m2: tests/m2_mock.c $(M2_SOURCES) src/common.c src/batch_json.c src/dma_mmio.c src/udmabuf.c include/m2.h include/ffttest.h
	mkdir -p build
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) tests/m2_mock.c $(M2_SOURCES) src/common.c src/batch_json.c src/dma_mmio.c src/udmabuf.c -Wl,--wrap=dma_unbound -Wl,--wrap=dma_open -Wl,--wrap=dma_close -Wl,--wrap=mmio_no_driver -Wl,--wrap=mmio_map_shared -Wl,--wrap=ubuf_info -Wl,--wrap=ubuf_open -Wl,--wrap=ubuf_close -Wl,--wrap=ubuf_sync -Wl,--wrap=ubuf_write_bytes -Wl,--wrap=ubuf_read_bytes -Wl,--wrap=m2_gpio_discover -Wl,--wrap=m2_gpio_open -Wl,--wrap=m2_gpio_close -Wl,--wrap=write -Wl,--wrap=now_ms -o $@
build/test_m2_layout: tests/test_m2_layout.c $(filter-out src/main.c,$(SOURCES)) include/m2.h include/ffttest.h
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) tests/test_m2_layout.c $(filter-out src/main.c,$(SOURCES)) -o $@
build/test_m2_gpio_api: tests/test_m2_gpio_api.c src/m2_gpio.c src/common.c include/m2.h include/ffttest.h
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) tests/test_m2_gpio_api.c src/m2_gpio.c src/common.c -Wl,--wrap=open -Wl,--wrap=open64 -Wl,--wrap=ioctl -Wl,--wrap=close -o $@
build/test_fft_m2: tests/fft_m2_mock.c src/fft_m2_orchestrator.c src/common.c include/fft_m2.h include/m2.h include/ffttest.h
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) tests/fft_m2_mock.c src/fft_m2_orchestrator.c src/common.c -Wl,--wrap=fft_chain_stage -Wl,--wrap=m2_execute -o $@
build/test_axis_switch: tests/test_axis_switch.c src/axis_switch.c src/common.c include/m2.h include/ffttest.h
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) tests/test_axis_switch.c src/axis_switch.c src/common.c -o $@
test: host build/test_core build/test_gpio build/test_access build/test_batch build/test_batch_json build/manifest_probe build/test_m2 build/test_m2_layout build/test_m2_gpio_api build/test_fft_m2 build/test_axis_switch
	./build/test_batch_json
	./build/test_core
	./build/test_gpio
	python3 tests/test_cli.py
	python3 tests/test_references.py
	python3 tests/test_batch.py
	python3 tests/test_manifest_compat.py
	./build/test_m2_gpio_api
	./build/test_m2_layout
	python3 tests/test_m2.py
	python3 tests/test_m2_references.py
	python3 tests/test_fft_m2.py
	./build/test_axis_switch
	./build/test_access --inspect
	FFTTEST_COMMAND=build/test_access python3 tests/test_cli.py
elf-check: $(TARGET)
	file $(TARGET)
	readelf -h $(TARGET)
	readelf -l $(TARGET)
	readelf -d $(TARGET)
clean:
	$(RM) build/ffttest build/ffttest-host build/test_core build/test_gpio build/test_batch build/test_access build/main-access.o build/test_batch_json build/manifest_probe build/test_m2 build/test_m2_layout build/test_m2_gpio_api build/test_fft_m2 build/test_axis_switch
