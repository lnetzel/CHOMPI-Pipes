/** @file audio_routing_test.cpp
 *  @brief Native tests for AudioRouting.h - the CHOMPI-PIPES output
 *  routing selection (Normal monitoring vs. an external send-FX loop):
 *  the physical black-key mapping, the boot default and selection
 *  persistence, plus OrganEngine integration tests for the per-output
 *  distribution and the record tap in both routings.
 *
 *  Build & run: make -C code/tests test
 */
#include "AudioRouting.h"
#include "OrganEngine.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace chompi;

/* SwId values (hardware.h): the 25 playable keys are 7..31; the sixth
 * and seventh physical black keys are 22 (C#4, KEY_21) and 23 (D#4,
 * KEY_22). chompi_main.cpp static_asserts the routing-key table
 * against Hardware::SwId. */

static void test_boot_default()
{
    assert(kRoutingDefault == kRoutingNormal);
    assert(kRoutingNormal == 0 && kRoutingSendFx == 1);
    assert(kNumRoutings == 2);
    assert(kRoutingKeySw[0] == 22 && kRoutingKeySw[1] == 23);
}

static void test_key_mapping()
{
    assert(RoutingAfterKey(kRoutingSendFx, 22) == kRoutingNormal); /* C#4 */
    assert(RoutingAfterKey(kRoutingNormal, 23) == kRoutingSendFx); /* D#4 */
    assert(RoutingAfterKey(kRoutingNormal, 22) == kRoutingNormal);
    assert(RoutingAfterKey(kRoutingSendFx, 23) == kRoutingSendFx);

    /* every other SwId selects nothing: white keys, the other black
     * keys (octave keys 7/12, preset keys 29/30/31, mod-destination
     * keys, black key 10), encoders, SHIFT, PLAY, LOOP */
    for(int swid = 0; swid < 40; swid++)
    {
        if(swid == 22 || swid == 23)
            continue;
        assert(RoutingAfterKey(kRoutingNormal, swid) == kRoutingNormal);
        assert(RoutingAfterKey(kRoutingSendFx, swid) == kRoutingSendFx);
    }
    /* out-of-range values select nothing either */
    assert(RoutingAfterKey(kRoutingSendFx, -1) == kRoutingSendFx);
    assert(RoutingAfterKey(kRoutingSendFx, 40) == kRoutingSendFx);
}

static void test_key_for_routing()
{
    assert(RoutingKeySw(kRoutingNormal) == 22);   /* Black 6, C#4 */
    assert(RoutingKeySw(kRoutingSendFx) == 23);   /* Black 7, D#4 */
    assert(RoutingKeySw(99) == 22);               /* fallback: Normal */
    /* inverse of RoutingAfterKey on the selection keys */
    assert(RoutingAfterKey(kRoutingSendFx, RoutingKeySw(kRoutingNormal))
           == kRoutingNormal);
    assert(RoutingAfterKey(kRoutingNormal, RoutingKeySw(kRoutingSendFx))
           == kRoutingSendFx);
}

static void test_indicator_color()
{
    /* the selection indicator is pink: full red, half blue, no green -
     * distinct from the preset/destination purple (.55, 0, 1) */
    float r, g, b;
    RoutingColor(r, g, b);
    assert(r == 1.f && g == 0.f && b == .5f);
}

/* ------------------------------------------------------------------ */
/* OrganEngine integration (DaisySP is host-compilable)               */
/* ------------------------------------------------------------------ */

static constexpr float  kSr    = 48000.f;
static constexpr size_t kBlock = 24; /* firmware block size */

struct Rig
{
    OrganEngine      engine;
    PlateReverb::T * plate_mem;
    float *          delay_l;
    float *          delay_r;
    float            buf[4][kBlock];
    float *          out[4];
    float            tap_l[kBlock];
    float            tap_r[kBlock];
    float            aux_l[kBlock];
    float            aux_r[kBlock];

    void Init()
    {
        plate_mem = new PlateReverb::T[PlateReverb::kDelayBufferSize];
        delay_l   = new float[StereoBbdDelay::kBufferSize];
        delay_r   = new float[StereoBbdDelay::kBufferSize];
        engine.Init(kSr, plate_mem, delay_l, delay_r);
        engine.SetRecordTap(tap_l, tap_r);
        for(int c = 0; c < 4; c++)
            out[c] = buf[c];
        SetAux(0.f);
    }

    void SetAux(float v)
    {
        for(size_t i = 0; i < kBlock; i++)
        {
            aux_l[i] = v;
            aux_r[i] = v;
        }
    }

    void Block()
    {
        engine.Prepare(kBlock);
        engine.Process(out, kBlock, aux_l, aux_r);
    }

    void RunMs(float ms)
    {
        const int blocks = static_cast<int>(ms * .001f * kSr / kBlock);
        for(int b = 0; b < blocks; b++)
            Block();
    }
};

/** The engine boots in Normal routing with the master gain settled at
 *  exactly 0.5 (boot step 16, no slew) - the exact-equality checks
 *  below rely on that. */
static void test_engine_boot_state()
{
    Rig r;
    r.Init();
    assert(r.engine.GetRouting() == kRoutingNormal);
    assert(r.engine.GetMasterGain() == .5f);
}

/** Normal routing: all four outputs carry the identical signal and the
 *  record tap captures the bus (INCLUDING AUX IN) at the fixed -6 dB
 *  level - with the boot master gain exactly 0.5 the tap is
 *  bit-identical to the outputs. */
static void test_normal_routing()
{
    Rig r;
    r.Init();
    r.SetAux(.25f);
    r.engine.NoteOn(48, 1.f);
    r.RunMs(200.f); /* voice settled, delay/reverb live */

    /* headphones == line out, sample for sample (L vs L, R vs R) */
    float peak = 0.f;
    for(size_t i = 0; i < kBlock; i++)
    {
        assert(r.buf[0][i] == r.buf[2][i]);
        assert(r.buf[1][i] == r.buf[3][i]);
        const float a = fabsf(r.buf[0][i]);
        if(a > peak)
            peak = a;
        /* record tap == outputs at the coincidental 0.5/0.5 gains */
        assert(r.tap_l[i] == r.buf[0][i]);
        assert(r.tap_r[i] == r.buf[1][i]);
    }
    assert(peak > .01f); /* the note is audible */
}

/** Normal routing: AUX IN reaches the line output. */
static void test_normal_routing_aux_reaches_line_out()
{
    Rig dry, auxd;
    dry.Init();
    auxd.Init();
    dry.engine.NoteOn(48, 1.f);
    auxd.engine.NoteOn(48, 1.f);
    dry.SetAux(0.f);
    auxd.SetAux(.25f);
    dry.RunMs(200.f);
    auxd.RunMs(200.f);
    bool differs = false;
    for(size_t i = 0; i < kBlock; i++)
        if(dry.buf[2][i] != auxd.buf[2][i])
            differs = true;
    assert(differs);
}

/** Send FX routing: the headphones carry only AUX IN x master gain
 *  (exactly, at the 0.5 boot gain), the record tap carries only AUX IN
 *  x -6 dB, and the line out carries the dry pre-master-gain bus. */
static void test_sendfx_routing()
{
    Rig r;
    r.Init();
    r.engine.SetRouting(kRoutingSendFx);
    assert(r.engine.GetRouting() == kRoutingSendFx);
    r.SetAux(.25f);
    r.engine.NoteOn(48, 1.f);
    r.RunMs(200.f);

    float line_peak = 0.f;
    bool  line_varies = false;
    for(size_t i = 0; i < kBlock; i++)
    {
        /* headphones: the constant AUX return only, x 0.5 exactly */
        assert(r.buf[0][i] == .25f * .5f);
        assert(r.buf[1][i] == .25f * .5f);
        /* record tap: AUX IN alone at the fixed -6 dB level */
        assert(r.tap_l[i] == .25f * OrganEngine::kRecordTapGain);
        assert(r.tap_r[i] == .25f * OrganEngine::kRecordTapGain);
        const float a = fabsf(r.buf[2][i]);
        if(a > line_peak)
            line_peak = a;
        if(r.buf[2][i] != r.buf[2][0])
            line_varies = true;
    }
    /* the line out carries the dry organ: the note is audible there
     * and the send is not the constant AUX return */
    assert(line_peak > .01f);
    assert(line_varies);
}

/** Send FX routing: AUX IN never reaches the line output - the send is
 *  bit-exact identical with and without the return patched in, so the
 *  external loop cannot feed back through the firmware. */
static void test_sendfx_line_out_has_no_aux()
{
    Rig dry, auxd;
    dry.Init();
    auxd.Init();
    dry.engine.SetRouting(kRoutingSendFx);
    auxd.engine.SetRouting(kRoutingSendFx);
    dry.engine.NoteOn(48, 1.f);
    auxd.engine.NoteOn(48, 1.f);
    dry.SetAux(0.f);
    auxd.SetAux(.25f);
    dry.RunMs(200.f);
    auxd.RunMs(200.f);
    for(size_t i = 0; i < kBlock; i++)
    {
        assert(dry.buf[2][i] == auxd.buf[2][i]);
        assert(dry.buf[3][i] == auxd.buf[3][i]);
    }
}

/** Send FX with nothing patched into AUX IN: the headphones and the
 *  record tap are exactly silent (by design); the line out still sends
 *  the dry organ. */
static void test_sendfx_unpatched_silence()
{
    Rig r;
    r.Init();
    r.engine.SetRouting(kRoutingSendFx);
    r.SetAux(0.f);
    r.engine.NoteOn(48, 1.f);
    r.RunMs(200.f);
    float line_peak = 0.f;
    for(size_t i = 0; i < kBlock; i++)
    {
        assert(r.buf[0][i] == 0.f);
        assert(r.buf[1][i] == 0.f);
        assert(r.tap_l[i] == 0.f);
        assert(r.tap_r[i] == 0.f);
        const float a = fabsf(r.buf[2][i]);
        if(a > line_peak)
            line_peak = a;
    }
    assert(line_peak > .01f);
}

/** SetRouting clamps anything that is not kRoutingSendFx to Normal,
 *  and Send FX without valid AUX pointers falls back to the Normal
 *  distribution inside Process() (no null dereference). */
static void test_routing_fallbacks()
{
    Rig r;
    r.Init();
    r.engine.SetRouting(99); /* out of enum range */
    assert(r.engine.GetRouting() == kRoutingNormal);

    r.engine.SetRouting(kRoutingSendFx);
    r.engine.NoteOn(48, 1.f);
    for(int b = 0; b < 100; b++)
    {
        r.engine.Prepare(kBlock);
        r.engine.Process(r.out, kBlock); /* aux defaults to nullptr */
    }
    for(size_t i = 0; i < kBlock; i++)
    {
        assert(r.buf[0][i] == r.buf[2][i]);
        assert(r.buf[1][i] == r.buf[3][i]);
    }
}

/** Switching the routing mid-render is instantaneous: the very next
 *  block follows the new distribution and the note keeps sounding. */
static void test_switch_mid_render()
{
    Rig r;
    r.Init();
    r.SetAux(.25f);
    r.engine.NoteOn(48, 1.f);
    r.RunMs(200.f);

    /* settled Normal block: AUX is in the bus, all outputs equal */
    r.Block();
    for(size_t i = 0; i < kBlock; i++)
        assert(r.buf[0][i] == r.buf[2][i]);

    /* the next block after SetRouting follows Send FX distribution */
    r.engine.SetRouting(kRoutingSendFx);
    r.Block();
    for(size_t i = 0; i < kBlock; i++)
    {
        assert(r.buf[0][i] == .25f * .5f);
        assert(r.tap_l[i] == .25f * OrganEngine::kRecordTapGain);
    }

    /* and back */
    r.engine.SetRouting(kRoutingNormal);
    r.Block();
    for(size_t i = 0; i < kBlock; i++)
        assert(r.buf[0][i] == r.buf[2][i]);
}

int main()
{
    test_boot_default();
    test_key_mapping();
    test_key_for_routing();
    test_indicator_color();
    test_engine_boot_state();
    test_normal_routing();
    test_normal_routing_aux_reaches_line_out();
    test_sendfx_routing();
    test_sendfx_line_out_has_no_aux();
    test_sendfx_unpatched_silence();
    test_routing_fallbacks();
    test_switch_mid_render();
    printf("audio_routing_test: all tests passed\n");
    return 0;
}
