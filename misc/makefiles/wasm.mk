#-----------------------------
# Configurable flags and names
#-----------------------------
# Builds a runtime-agnostic wasm32 module - see src/wasm/ and misc/wasm/README.md
# Requires wasi-sdk (https://github.com/WebAssembly/wasi-sdk) for a freestanding
#  libc/libm - however the resulting module makes no use of WASI itself, and
#  imports nothing from the host (see misc/wasm/README.md for why/how)
WASI_SDK ?= /opt/wasi-sdk

SOURCE_DIRS := src src/wasm
BUILD_DIR	:= build/wasm

WASM_FLAGS := --target=wasm32-wasip1 --sysroot=$(WASI_SDK)/share/wasi-sysroot -DPLAT_WASM

CFLAGS	:= -g $(WASM_FLAGS) -ffunction-sections -fdata-sections
LDFLAGS	:= -g $(WASM_FLAGS) -mexec-model=reactor -Wl,--gc-sections -Wl,-z,stack-size=1048576 -Wl,--initial-memory=16777216
LIBS 	:=
include misc/makefiles/common_config.mk


CC      := $(WASI_SDK)/bin/clang
LINK    := $(WASI_SDK)/bin/clang
OEXT    := .wasm
include misc/makefiles/common_build.mk
