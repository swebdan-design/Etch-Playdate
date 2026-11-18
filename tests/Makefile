# ETCH CRANK -- by Synth Centric
# Host tests.  Run from the top level with `make test`.
#
#   ETCH's 44 CORROSION tests (unmodified) against corrosion/
#   ETCH DS's sequencer and sound-engine tests (unmodified) against engine/
#   the sound consistency test: ETCH DS vs this port, 16 bars, bit for bit.
#     DS=/path/to/ETCH-DS builds the ETCH DS side from that checkout's own
#     sources; without it, from engine/ (checksum-identical to ETCH DS)
#   test_crank     the crank: relative, 540 degrees, hard stops, resonance,
#                  docking home over a beat, B + dock, per-sample glide
#   test_resample  32 728.5 -> 44 100 Hz: gain, images, the exact rate
HOSTCC ?= cc
ROOT   := ..
CFLAGS := -O2 -Wall -Wno-unused-function -Wno-unused-result \
          -I$(ROOT)/engine -I$(ROOT)/corrosion -I$(ROOT)/platform -I$(ROOT)/layers
CORR   := $(ROOT)/corrosion/corrosion.c $(ROOT)/corrosion/pattern.c $(ROOT)/corrosion/names.c
ENGINE := $(ROOT)/engine/acid.arm.c $(ROOT)/engine/foundry.arm.c $(ROOT)/engine/fx.arm.c \
          $(ROOT)/engine/render.arm.c $(ROOT)/engine/dsp_tables.c $(ROOT)/engine/seq_core.c \
          $(ROOT)/engine/demo.c $(ROOT)/engine/evolve.c
APP    := $(ROOT)/engine/edit.c $(ROOT)/engine/storage.c
PORT   := $(ROOT)/platform/pump.c $(ROOT)/platform/crank.c $(ROOT)/platform/crank_audio.c \
          $(ROOT)/platform/resample_tab.c $(ROOT)/platform/app_glue.c $(ROOT)/platform/foundry_guard.c
UI     := $(ROOT)/platform/gfx1.c $(ROOT)/platform/font_ui.c $(ROOT)/platform/ui.c \
          $(ROOT)/platform/scr_live.c $(ROOT)/platform/scr_track.c $(ROOT)/platform/scr_synth.c \
          $(ROOT)/platform/scr_gen.c $(ROOT)/platform/scr_song.c $(ROOT)/platform/scr_file.c \
          $(ROOT)/platform/cpl.c $(ROOT)/platform/proj.c $(ROOT)/platform/demos.c $(ROOT)/layers/layers.c
WRAP   := -Wl,--wrap=acid_render -Wl,--wrap=foundry_trigger -Wl,--wrap=foundry_build
UIWRAP := $(WRAP) -Wl,--wrap=fx_limit -Wl,--wrap=corr_mutate
OUT    := $(ROOT)/build/host

# the ETCH DS side of the consistency test
ifneq ($(DS),)
DS_INC := -I$(DS)/common -I$(DS)/corrosion -I$(DS)/arm9/source
DS_SRC := $(DS)/arm9/source/acid.arm.c $(DS)/arm9/source/foundry.arm.c $(DS)/arm9/source/fx.arm.c \
          $(DS)/arm9/source/render.arm.c $(DS)/arm9/source/dsp_tables.c $(DS)/common/seq_core.c \
          $(DS)/arm9/source/demo.c $(DS)/arm9/source/evolve.c $(DS)/arm9/source/edit.c \
          $(DS)/arm9/source/storage.c $(DS)/corrosion/corrosion.c $(DS)/corrosion/pattern.c \
          $(DS)/corrosion/names.c
DS_FROM := ETCH-DS checkout $(DS)
else
DS_INC := -I$(ROOT)/engine -I$(ROOT)/corrosion
DS_SRC := $(ENGINE) $(APP) $(CORR)
DS_FROM := engine/ (checksum-identical to ETCH DS; DS=/path/to/ETCH-DS to build from a checkout)
endif

.PHONY: all FORCE
all: $(OUT)/test_generator $(OUT)/test_seq $(OUT)/test_dsp $(OUT)/consist_ds $(OUT)/consist_port \
     $(OUT)/compare $(OUT)/test_crank $(OUT)/test_resample $(OUT)/test_ui
	@echo "== ETCH's CORROSION tests (unmodified) against corrosion/ =="
	@$(OUT)/test_generator | tail -1
	@echo "== ETCH DS's sequencer tests (unmodified) against engine/ =="
	@$(OUT)/test_seq
	@echo "== ETCH DS's sound-engine tests (unmodified) against engine/ =="
	@cd $(OUT) && ./test_dsp | tail -1
	@echo "== sound consistency: ETCH DS vs ETCH CRANK, 16 bars, crank docked =="
	@echo "  ETCH DS side built from $(DS_FROM)"
	@$(OUT)/consist_ds $(OUT)/consist_ds.raw
	@$(OUT)/consist_port $(OUT)/consist_port.raw $(OUT)/consist_port_44k.raw aligned
	@$(OUT)/compare $(OUT)/consist_ds.raw $(OUT)/consist_port.raw
	@echo "  -- the same, with the update loop free-running at 50 Hz (information) --"
	@$(OUT)/consist_port $(OUT)/consist_port50.raw - 50hz
	@$(OUT)/compare $(OUT)/consist_ds.raw $(OUT)/consist_port50.raw || true
	@echo "== crank =="
	@cd $(OUT) && ./test_crank
	@echo "== resampler =="
	@$(OUT)/test_resample
	@echo "== the interface (scripted buttons and crank) =="
	@cd $(OUT) && ./test_ui

$(OUT)/test_generator: $(ROOT)/corrosion/tests/test_generator.c $(CORR)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^

$(OUT)/test_seq: $(ROOT)/engine/tests/test_seq.c $(ROOT)/engine/seq_core.c $(CORR)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^

$(OUT)/test_dsp: $(ROOT)/engine/tests/test_dsp.c $(ENGINE) $(CORR)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^ -lm

$(OUT)/consist_ds: consistency.c $(DS_SRC) FORCE
	@mkdir -p $(OUT)
	$(HOSTCC) -O2 -w $(DS_INC) -o $@ consistency.c $(DS_SRC) -lm

$(OUT)/consist_port: consistency.c $(ENGINE) $(APP) $(CORR) $(PORT)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -DCONSIST_PORT -o $@ $^ -lm $(WRAP)

$(OUT)/compare: compare.c
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^

$(OUT)/test_crank: test_crank.c $(ENGINE) $(APP) $(CORR) $(PORT)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^ -lm $(WRAP)

$(OUT)/test_resample: test_resample.c $(ENGINE) $(APP) $(CORR) $(PORT)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^ -lm $(WRAP)

FORCE:

$(OUT)/test_ui: test_ui.c $(ENGINE) $(APP) $(CORR) $(PORT) $(UI)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^ -lm $(UIWRAP)
