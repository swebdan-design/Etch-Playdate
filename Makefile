# ETCH CRANK -- by Synth Centric -- for Playdate
#
#   make            check corrosion/ and engine/ against their checksums, then
#                   build ETCH_CRANK.pdx (device pdex.elf + Simulator pdex.so)
#   make device     the device binary only (arm-none-eabi-gcc, Cortex-M7)
#   make simulator  the Simulator library only
#   make test       host tests: ETCH's 44 CORROSION tests, ETCH DS's
#                   sequencer and sound tests against engine/, the sound
#                   consistency test (ETCH DS vs this port), crank, resampler
#   make arm-test   the consistency test again with the device's compiler and
#                   flags, run under qemu-arm (tools/arm_parity.sh)
#
# corrosion/ is CORROSION from ETCH, and engine/ is ETCH DS's engine, both
# copied unmodified (tools/sync_*.sh).  They are never edited here.

HEAP_SIZE  = 8388208
STACK_SIZE = 61800
PRODUCT    = ETCH_CRANK.pdx

SDK = ${PLAYDATE_SDK_PATH}
ifeq ($(SDK),)
	SDK = $(shell egrep '^\s*SDKRoot' ~/.Playdate/config 2>/dev/null | head -n 1 | cut -c9-)
endif

HOSTCC ?= cc

ENGINE_SRC = engine/acid.arm.c engine/foundry.arm.c engine/fx.arm.c engine/render.arm.c \
             engine/dsp_tables.c engine/seq_core.c engine/demo.c engine/evolve.c \
             engine/edit.c engine/storage.c
CORR_SRC   = corrosion/corrosion.c corrosion/pattern.c corrosion/names.c
PLAT_SRC   = platform/main.c platform/screen.c platform/pump.c platform/crank.c \
             platform/crank_audio.c platform/resample_tab.c platform/app_glue.c \
             platform/syscalls.c

SRC     = $(PLAT_SRC) $(ENGINE_SRC) $(CORR_SRC)
UINCDIR = platform engine corrosion
UDEFS   = -DETCH_CPU_METER=1
VPATH  += platform:engine:corrosion

.PHONY: check test arm-test tables
all: check

ifneq ($(SDK),)
include $(SDK)/C_API/buildsupport/common.mk
# the crank rides into ETCH DS's ACID through one link-time wrap
LDFLAGS     += -Wl,--wrap=acid_render
DYLIB_FLAGS += -O2 -Wl,--wrap=acid_render -DETCH_CPU_METER=1
else
all:
	@echo "Playdate SDK not found: set PLAYDATE_SDK_PATH (make test works without it)"
endif

# CORROSION and ETCH DS's engine must stay byte-identical: refuse to build otherwise
check:
	@echo "  CHECK   corrosion/SHA256SUMS (CORROSION from ETCH, unmodified)"
	@cd corrosion && sha256sum --quiet -c SHA256SUMS
	@echo "  CHECK   engine/SHA256SUMS (ETCH DS's engine, unmodified)"
	@cd engine && sha256sum --quiet -c SHA256SUMS

device_bin simulator_bin $(OBJDIR)/pdex.elf: check

test: check
	@$(MAKE) --no-print-directory -C tests HOSTCC=$(HOSTCC)

arm-test: check
	@tools/arm_parity.sh

tables:
	python3 tools/gen_resampler.py
