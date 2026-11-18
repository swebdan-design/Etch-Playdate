/*
 * ETCH CRANK -- by Synth Centric
 * The ETCH DS layers and the Playdate-only layer, on the host, through the
 * real pump, engine and wraps:
 *
 *   silent without a 4th group   bound but off: the output is ETCH DS's
 *   the answering generator      deterministic per code, different per seed,
 *                                never touches the pattern or CORROSION
 *   RUST, METAL, NOISE           they sound; their mutes silence them
 *   ratchets, lengths            counted hits
 *   sound evolution              bounded around the anchor; locks hold
 *   CRANK BITE                   docked: CORROSION as it is; out: its own
 *                                intensity argument, from the filter
 *   SPEAKER                      mono
 *   the sidecar                  saves and loads the layers
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "ipc.h"
#include "audio.h"
#include "app_glue.h"
#include "corrosion.h"
#include "storage.h"
#include "pump.h"
#include "crank.h"
#include "cpl.h"
#include "layers.h"
#include "ui.h"

uint16_t hw_noise(void) { return 0x2468; }
double plat_tick_hz(void) { return 1e9; }
double plat_ui_ms(void) { return 1; }
extern void (*layers_trigger_spy)(uint8_t lanes);

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
                           printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t hits[8];
static void spy(uint8_t lanes) { int l; for (l = 0; l < 8; l++) if (lanes & (1u << l)) hits[l]++; }

static void boot(void)
{
    audio_engine_init();
    foundry_build();
    shared_init();
    cpl_init();
    pump_init(SH);
    crank_ui_init(acid_p.cutoff, acid_p.res, 1);
    memset(hits, 0, sizeof hits);
    layers_trigger_spy = spy;
}

/* n engine blocks, with the update loop every 4 (as the consistency test) */
static uint64_t run(uint32_t n, double *energy_lr_diff, double *energy)
{
    int16_t l[AUDIO_BLOCK], r[AUDIO_BLOCK];
    uint64_t h = 1469598103934665603ull;
    uint32_t b, i;
    double e = 0, d = 0;
    for (b = 0; b < n; b++) {
        if ((b & 3) == 0) { cpl_frame(b * 4, 0, 1); layers_frame(); evo_frame(); }
        pump_block(l, r);
        for (i = 0; i < AUDIO_BLOCK; i++) {
            h = (h ^ (uint16_t)l[i]) * 1099511628211ull;
            h = (h ^ (uint16_t)r[i]) * 1099511628211ull;
            e += (double)l[i] * l[i];
            d += (double)(l[i] - r[i]) * (l[i] - r[i]);
        }
    }
    if (energy) *energy = e;
    if (energy_lr_diff) *energy_lr_diff = d;
    return h;
}

/* cfg 1: the CPL and the layers bound and running, no 4th group.
 * cfg 0: ETCH DS: no layers (LD NULL), no CPL frame */
static uint64_t render_cfg(int cfg)
{
    int16_t l[AUDIO_BLOCK], r[AUDIO_BLOCK];
    uint64_t h = 1469598103934665603ull;
    uint32_t b, k;
    boot();
    if (!cfg) LD = NULL;
    evo_seed(0xC0DE);
    app_send(CMD(CMD_START, 0xC0DE));
    for (b = 0; b < 3000; b++) {
        if ((b & 3) == 0) { if (cfg) { cpl_frame(b * 4, 0, 1); layers_frame(); } evo_frame(); }
        pump_block(l, r);
        for (k = 0; k < AUDIO_BLOCK; k++) {
            h = (h ^ (uint16_t)l[k]) * 1099511628211ull;
            h = (h ^ (uint16_t)r[k]) * 1099511628211ull;
        }
    }
    return h;
}

static uint64_t in_child(int cfg)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd)) return 0;
    pid = fork();
    if (pid == 0) {
        h = render_cfg(cfg);
        if (write(fd[1], &h, sizeof h) != sizeof h) _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h) h = 0;
    close(fd[0]);
    waitpid(pid, NULL, 0);
    return h;
}

static uint32_t hash_layers(void)
{
    const uint8_t *b = (const uint8_t *)LD->pat;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof LD->pat; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

int main(void)
{
    uint64_t h_off, h_ds;
    uint32_t lh1, lh2, lh3;
    double e1, e2, d;
    int i;

    /* 1. without a 4th group the layers are silent: ETCH DS's output.  Each
     * configuration runs in its own process (fork), from the same fresh state:
     * ETCH DS's engine keeps static state, so two renders in one process differ */
    h_off = in_child(1);
    h_ds = in_child(0);
    CHECK(h_off == h_ds, "layers bound but off must sound exactly as ETCH DS (%016llx vs %016llx)",
          (unsigned long long)h_off, (unsigned long long)h_ds);

    /* 2. the answering generator: deterministic, per seed, hands off CORROSION */
    boot();
    {
        pattern_t before;
        memcpy(&before, &SH->proj.pat[0], sizeof before);
        memcpy(cplp.ext, "A1B2", 4); cplp.has_ext = 1;
        layers_ext_seed = cpl_ext_seed();
        layers_answer_all();
        lh1 = hash_layers();
        CHECK(!memcmp(&before, &SH->proj.pat[0], sizeof before), "answering leaves CORROSION's pattern alone");
        layers_answer_all();
        lh2 = hash_layers();
        CHECK(lh1 == lh2, "the same code answers the same way");
        memcpy(cplp.ext, "A1B3", 4);
        layers_ext_seed = cpl_ext_seed();
        layers_answer_all();
        lh3 = hash_layers();
        CHECK(lh3 != lh1, "another 4th group answers differently");
        {
            const layer_pat_t *L = &LD->pat[0];
            int notes = 0;
            for (i = 0; i < 16; i++) notes += (L->flags[i] & RF_ON) != 0;
            printf("  pattern 01 answered (A1B3): %d RUST notes, METAL %04X, NOISE %04X\n", notes, L->metal, L->noise);
            CHECK(notes > 0, "RUST answers");
        }
        /* CORROSION's next composition is the same with or without the layers */
        {
            pattern_t a, b2;
            gen_t g = SH->proj.gen;
            pattern_clear(&a); corr_generate(&a, &g, GEN_ALL);
            layers_answer_all();
            pattern_clear(&b2); corr_generate(&b2, &g, GEN_ALL);
            CHECK(!memcmp(&a, &b2, sizeof a), "CORROSION's output does not depend on the layers");
        }
    }

    /* 3. the layers sound, and their mutes silence them */
    boot();
    memcpy(cplp.ext, "A1B2", 4); cplp.has_ext = 1;
    evo_seed(0xC0DE);
    SH->proj.gen.mode = EVO_STABLE;
    app_send(CMD(CMD_START, 0xC0DE));
    run(4, NULL, NULL);
    run(2000, NULL, &e1);
    {
        const layer_pat_t *L = &LD->pat[SH->cur & 31];
        uint32_t want = 0, s;
        for (s = 0; s < 16; s++) want += ((L->metal >> s) & 1) + ((L->noise >> s) & 1);
        printf("  A1B2, pattern %02u: METAL %04X NOISE %04X; hits in 2000 blocks (~4 bars): METAL %u, NOISE %u\n",
               SH->cur + 1, L->metal, L->noise, hits[4], hits[5]);
        CHECK(hits[4] + hits[5] >= want * 3 && hits[4] + hits[5] <= want * 5, "METAL / NOISE play their answer (%u hits, %u a bar)",
              hits[4] + hits[5], want);
    }
    app_send(CMD(CMD_MUTES, 0xE0));                /* RUST, METAL, NOISE muted */
    run(4, NULL, NULL);
    memset(hits, 0, sizeof hits);
    run(2000, NULL, &e2);
    CHECK(hits[4] == 0 && hits[5] == 0, "muted: no METAL / NOISE hits");
    printf("  energy with the layers %.3g, muted %.3g\n", e1, e2);
    CHECK(e1 != e2, "the layers change the sound");

    /* 4. ratchets and lengths */
    boot();
    cplp.has_ext = 0;
    LD->manual = 1;                                /* hand-written layers play */
    SH->proj.gen.mode = EVO_STABLE;
    for (i = 0; i < NUM_PATTERNS; i++) LD->pat[i].lock = 1;
    memset(&LD->pat[0], 0, sizeof LD->pat[0]);
    LD->pat[0].rust_len = 16; LD->pat[0].noise_len = 16;
    LD->pat[0].metal = 1;                          /* step 1 only ... */
    LD->pat[0].metal_len = 3;                      /* ... of a 3-step loop */
    LD->pat[0].lock = 1;
    {
        uint8_t s;
        for (s = 0; s < 16; s++) if ((SH->proj.pat[0].drum[LANE_CH] >> s) & 1) LD->pat[0].ratchet[s] = (uint8_t)(3 << (LANE_CH * 2));
    }
    evo_seed(1);
    app_send(CMD(CMD_START, 1));
    run(4, NULL, NULL);
    memset(hits, 0, sizeof hits);
    {
        uint32_t blocks = (uint32_t)(4 * 60.0 / 124 * 32728.5 / 128);   /* one bar */
        uint32_t hats = 0, s;
        for (s = 0; s < 16; s++) hats += (SH->proj.pat[0].drum[LANE_CH] >> s) & 1;
        run(blocks * 2, NULL, NULL);
        printf("  two bars: METAL (1 step of 3) %u hits; HAT %u hits for %u hat steps x 4 (ratchet)\n", hits[4], hits[2], hats * 2);
        CHECK(hits[4] >= 10 && hits[4] <= 11, "METAL on a 3-step loop: 32 / 3 hits (%u)", hits[4]);
        CHECK(hits[2] >= hats * 2 * 4 - 4 && hits[2] <= hats * 2 * 4, "ratchet x4 on the hats (%u)", hits[2]);
    }

    /* 5. sound evolution: bounded, locks hold */
    boot();
    LD->evo_on = 1;
    LD->evo_lock = 1u << EK_DECAY;
    SH->proj.gen.mode = EVO_CORRODE;
    layers_evo_anchor();
    {
        uint8_t env0 = base_acid.envmod, dec0 = base_acid.decay, moved = 0;
        for (i = 0; i < 60; i++) {
            SH->evo_commits++;
            layers_frame();
            if (base_acid.envmod != env0) moved = 1;
            CHECK(base_acid.envmod >= env0 - 20 && base_acid.envmod <= env0 + 20, "ENV MOD within +-20 of its anchor");
        }
        CHECK(moved, "CORRODE moves a free knob");
        CHECK(base_acid.decay == dec0, "a locked knob stays");
    }

    /* 6. CRANK BITE: CORROSION's own intensity argument */
    boot();
    {
        pattern_t src, anchor, a, b2, c;
        gen_t g = SH->proj.gen;
        memcpy(&src, &SH->proj.pat[0], sizeof src);
        memcpy(&anchor, &src, sizeof src);
        cpl_set.bite = 1;
        crank_ui.docked = 1;
        corr_mutate(&a, &src, &anchor, &g, EVO_CORRODE, 3, 0x1111);
        cpl_set.bite = 0;
        corr_mutate(&b2, &src, &anchor, &g, EVO_CORRODE, 3, 0x1111);
        CHECK(!memcmp(&a, &b2, sizeof a), "BITE with the crank docked: CORROSION unchanged");
        cpl_set.bite = 1;
        crank_ui.docked = 0;
        crank_ui.cut_q8 = 127 << 8;
        corr_mutate(&c, &src, &anchor, &g, EVO_CORRODE, 3, 0x1111);
        cpl_set.bite = 0;
        {
            pattern_t d15;
            corr_mutate(&d15, &src, &anchor, &g, EVO_CORRODE, 15, 0x1111);
            CHECK(!memcmp(&c, &d15, sizeof c), "BITE, filter fully open: intensity 15");
        }
    }

    /* 7. SPEAKER: mono */
    boot();
    evo_seed(3);
    app_send(CMD(CMD_START, 3));
    cpl_set.speaker = SPK_ON;
    run(600, &d, NULL);
    CHECK(d == 0, "SPEAKER: left = right");
    cpl_set.speaker = SPK_OFF;
    run(600, &d, NULL);
    CHECK(d > 0, "SPEAKER off: stereo");

    /* 8. the sidecar keeps the layers */
    remove("etch_crank_ext.sav");
    boot();
    memcpy(cplp.ext, "C0DE", 4); cplp.has_ext = 1;
    layers_ext_seed = cpl_ext_seed();
    layers_answer_all();
    LD->rust.cutoff = 99;
    lh1 = hash_layers();
    CHECK(cpl_save_slot(3), "sidecar saved");
    cpl_new_project();
    CHECK(hash_layers() != lh1, "a new project has fresh layers");
    CHECK(cpl_load_slot(3), "sidecar loads");
    CHECK(hash_layers() == lh1 && LD->rust.cutoff == 99 && cplp.has_ext, "the layers come back");

    printf("test_layers: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
