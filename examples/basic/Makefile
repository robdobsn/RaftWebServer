THISDIR = $(realpath $(CURDIR))
ROOTDIR = $(realpath $(CURDIR)/../../..)
DOCKER ?= docker run --rm $(DA) -v $(ROOTDIR):$(ROOTDIR) -w $(THISDIR) espressif/idf
CMD ?= idf.py build

build: Makefile $(wildcard main/*)
	$(DOCKER) $(CMD)

.PHONY: build

clean:
	$(DOCKER) rm -rf build sdkconfig || true

flashwsl: build
	python.exe -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 2MB --flash_freq 40m 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/spiffs.bin 0x100000 build/RaftWebServerExample.bin
