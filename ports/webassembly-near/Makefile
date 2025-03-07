################################################################################
# Initial setup of Makefile environment.

# If the build directory is not given, make it reflect the variant name.
BUILD ?= build

include ../../py/mkenv.mk

# Use the default frozen manifest, variants may override this.
FROZEN_MANIFEST ?= manifest.py

OUTPUT_WASM ?= $(BUILD)/contract.wasm

# Qstr definitions (must come before including py.mk).
QSTR_DEFS = qstrdefsport.h

# Include py core make definitions.
include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

################################################################################
# Project specific settings and compiler/linker flags.

CC = emcc
LD = emcc

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)

CFLAGS += -std=c99 -Wall -Werror -Wdouble-promotion -Wfloat-conversion 
CFLAGS += -fdata-sections -ffunction-sections -sSUPPORT_LONGJMP=0 -mno-bulk-memory -mno-nontrapping-fptoint
CFLAGS += -Oz -DNDEBUG
# CFLAGS += -DMICROPY_DEBUG_VERBOSE
CFLAGS += $(INC)

LDFLAGS += -Wl,--gc-sections -sSTANDALONE_WASM=1 -sALLOW_MEMORY_GROWTH=0 -sWASM_BIGINT=1 -sSUPPORT_LONGJMP=0 -sERROR_ON_UNDEFINED_SYMBOLS=0
LDFLAGS += -sEXPORTED_FUNCTIONS="$(EXPORTED_FUNCTIONS)"

################################################################################
# Source files and libraries.

SRC_SHARED = $(addprefix shared/,\
	runtime/interrupt_char.c \
	runtime/stdout_helpers.c \
	runtime/sys_stdio_mphal.c \
	runtime/pyexec.c \
	readline/readline.c \
	timeutils/timeutils.c \
	)

SRC_C += \
	main.c \
	mphalport.c \
	modnear.c \
	modrandom.c \
	cmp.c \
	modmsgpack.c

# List of sources for qstr extraction.
SRC_QSTR += $(SRC_C) $(SRC_SHARED)

OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_SHARED:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(SRC_C_GENERATED:.c=.o)

################################################################################
# Main targets.

.PHONY: all repl min test test_min

all: $(OUTPUT_WASM)

$(OUTPUT_WASM): $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)emcc $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ)

################################################################################
# Remaining make rules.

include $(TOP)/py/mkrules.mk
