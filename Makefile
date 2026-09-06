SHELL := /usr/bin/env bash
.DEFAULT_GOAL := help

IDF_PATH ?= $(HOME)/esp/esp-idf-v5.5.3
IDF_TOOLS_PATH ?= $(HOME)/.espressif
IDF_PYTHON_ENV_PATH ?= $(firstword $(wildcard $(IDF_TOOLS_PATH)/python_env/idf5.5_py3.*_env))
PORT ?=
BAUD ?= 460800

DEVICE_BUILD_DIR := build-device
QEMU_BUILD_DIR := build-qemu
DEVICE_FULL_BIN := build/FoloToy-AI-Passport-full.bin
QEMU_FULL_BIN := $(QEMU_BUILD_DIR)/FoloToy-AI-Passport-full.bin
DIST_DIR := dist
DIST_FIRMWARE := $(DIST_DIR)/FoloToy-AI-Passport-full.bin
DIST_WEB_ZIP := $(DIST_DIR)/AI-Passport-Sync-macos.zip
QEMU_ARGS := -B $(QEMU_BUILD_DIR) \
	-D "SDKCONFIG=$(QEMU_BUILD_DIR)/sdkconfig.qemu" \
	-D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;tools/passport_emulator/sdkconfig.qemu.defaults" \
	-D "EXTRA_COMPONENT_DIRS=tools/passport_emulator/qemu_components/esp_lcd_qemu_rgb"

define idf_env
test -f "$(IDF_PATH)/export.sh" || { echo "ESP-IDF 5.5.3 not found: $(IDF_PATH)" >&2; exit 1; }; \
test -x "$(IDF_PYTHON_ENV_PATH)/bin/python" || { echo "ESP-IDF Python environment not found. Run $(IDF_PATH)/install.sh esp32c3" >&2; exit 1; }; \
export IDF_TOOLS_PATH="$(IDF_TOOLS_PATH)"; \
export IDF_PYTHON_ENV_PATH="$(IDF_PYTHON_ENV_PATH)"; \
source "$(IDF_PATH)/export.sh" >/dev/null
endef

.PHONY: help build-device build-device-dev build-qemu run-qemu \
	flash-device flash-device-full dist test check clean-device clean-qemu

help:
	@printf '%s\n' \
	  'AI Passport build targets:' \
	  '  make build-device       Build and verify the production Full Bin.' \
	  '  make build-device-dev   Incrementally build the production app.' \
	  '  make build-qemu         Build the isolated QEMU Work Assistant image.' \
	  '  make run-qemu           Build, flash virtual 8 MB Flash, and start QEMU.' \
	  '  make flash-device PORT=/dev/cu.usbmodem...'
	@printf '%s\n' \
	  '                         Safely use segmented idf.py flash on a real device.' \
	  '  make flash-device-full PORT=... FULL_FLASH=1'
	@printf '%s\n' \
	  '                         Write the verified Full Bin at 0x0; only for a confirmed-safe device.' \
	  '  make dist               Package the macOS Web launcher ZIP and verified Full Bin in dist/.' \
	  '  make test               Run static and host checks.' \
	  '  make check              Run all repository gates.' \
	  '' \
	  'Variables: IDF_PATH, IDF_TOOLS_PATH, IDF_PYTHON_ENV_PATH, PORT, BAUD.'

build-device:
	@$(idf_env); ./tools/validate.sh --firmware

build-device-dev:
	@$(idf_env); \
	idf.py -B "$(DEVICE_BUILD_DIR)" \
	  -D "SDKCONFIG=$(DEVICE_BUILD_DIR)/sdkconfig" \
	  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults" build

build-qemu:
	@rm -f "$(QEMU_BUILD_DIR)/sdkconfig.qemu"; \
	$(idf_env); \
	idf.py $(QEMU_ARGS) build; \
	idf.py $(QEMU_ARGS) merge-bin -o FoloToy-AI-Passport-full.bin

run-qemu: build-qemu
	@$(idf_env); idf.py $(QEMU_ARGS) qemu --graphics

flash-device: build-device-dev
	@test -n "$(PORT)" || { echo "Set PORT, for example: make flash-device PORT=/dev/cu.usbmodem1101" >&2; exit 2; }
	@$(idf_env); idf.py -B "$(DEVICE_BUILD_DIR)" -p "$(PORT)" -b "$(BAUD)" flash

flash-device-full: build-device
	@test "$(FULL_FLASH)" = "1" || { echo "Refusing full 0x0 flash. Re-run with FULL_FLASH=1 after confirming the device is safe." >&2; exit 2; }
	@test -n "$(PORT)" || { echo "Set PORT, for example: make flash-device-full PORT=/dev/cu.usbmodem1101 FULL_FLASH=1" >&2; exit 2; }
	@$(idf_env); \
	python -m esptool --chip esp32c3 -b "$(BAUD)" --before default_reset --after hard_reset \
	  write-flash --flash_mode dio --flash_freq 80m --flash_size 8MB \
	  0x0 "$(DEVICE_FULL_BIN)"

dist: build-device
	@./tools/package_assistant_web.sh
	@install -m 0644 "$(DEVICE_FULL_BIN)" "$(DIST_FIRMWARE)"
	@printf 'Created %s and %s\n' "$(DIST_WEB_ZIP)" "$(DIST_FIRMWARE)"

test:
	@./tools/validate.sh --static

check: test build-device

clean-device:
	@test ! -d "$(DEVICE_BUILD_DIR)" || { $(idf_env); idf.py -B "$(DEVICE_BUILD_DIR)" fullclean; }

clean-qemu:
	@test ! -d "$(QEMU_BUILD_DIR)" || { $(idf_env); idf.py $(QEMU_ARGS) fullclean; }
