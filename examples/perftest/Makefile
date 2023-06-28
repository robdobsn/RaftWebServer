ROOTDIR = $(realpath $(CURDIR)/../../..)
WORKING_DIR = $(realpath $(CURDIR))
DOCKER ?= docker run --rm $(DA) -v $(ROOTDIR):$(ROOTDIR) -w $(WORKING_DIR) espressif/idf
CMD ?= idf.py build
COM_PORT ?= COM3

build:
	$(DOCKER) $(CMD)

mongoose:
	$(DOCKER) $(CMD) -D USE_MONGOOSE=1

.PHONY: build mongoose

clean:
	$(DOCKER) rm -rf build sdkconfig || true

flashwsl:
	python.exe -m esptool --chip esp32 -p ${COM_PORT} -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 2MB --flash_freq 40m 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/spiffs.bin 0x100000 build/RaftWebServerExample.bin
	python.exe $(WORKING_DIR)/build/_deps/raftcore-src/scripts/SerialMonitor.py $(COM_PORT) -g

buildflash: build flashwsl

interact: 
	docker run -i -t -v $(ROOTDIR):$(ROOTDIR) -w $(WORKING_DIR) espressif/idf
