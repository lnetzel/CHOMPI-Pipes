# CHOMPI-PIPES

TARGET = CHOMPI_PIPES_1.1

DEBUG=0
OPT=-O3

APP_TYPE=BOOT_SRAM
# `make program-boot` installs CHOMPI's own bootloader (bin/ in this repo)
BOOT_BIN = ../../bin/CHOMPI_Bootloader_V6_2_0.bin
LDSCRIPT = chompi_sram.lds

# Sources
CPP_SOURCES = chompi_main.cpp \
	encoder.cpp

# SD-card presets: FatFS + coreJSON (vendored in code/libs)
USE_FATFS = 1
C_SOURCES = ../libs/coreJSON/source/core_json.c
C_INCLUDES += -I../libs/coreJSON/source/include

# Libs: vendored libDaisy/DaisySP in this repo (prebuilt .a committed)
LIBDAISY_DIR = ../libs/libDaisy
DAISYSP_DIR = ../libs/DaisySP

# B4: optional audio-CPU meter on PTH LED 0 (green < 70%, amber 70-85%,
# red > 85% peak): build with `make PIPES_CPU_METER=1`
ifeq ($(PIPES_CPU_METER),1)
C_DEFS += -DPIPES_CPU_METER
endif

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

.PHONY: check-toolchain
check-toolchain:
	@command -v arm-none-eabi-gcc >/dev/null || { echo "arm-none-eabi-gcc is missing from PATH"; exit 1; }
	@command -v arm-none-eabi-g++ >/dev/null || { echo "arm-none-eabi-g++ is missing from PATH"; exit 1; }
	@command -v make >/dev/null || { echo "make is missing from PATH"; exit 1; }
	@arm-none-eabi-gcc --version | head -n 1
	@arm-none-eabi-gcc --version | grep -q '10\.3\.1' || { echo "This firmware requires Arm GCC 10.3.1"; exit 1; }
