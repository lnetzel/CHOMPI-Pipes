/** @file percussion_test.cpp
 *  @brief Native tests for Percussion.h (Hammond-style percussion for
 *  ORGAN) - helper-level timing/endpoint tests plus actual OrganEngine
 *  integration tests (DaisySP is host-compilable): single-trigger
 *  rearm semantics, the intended 2 2/3' harmonic in rendered output,
 *  independence from Character/pipe controls, gate behavior across
 *  release/steal/toggle, the 28.5-unit mix normalization and exact
 *  settled-off equivalence.
 *
 *  Build & run: make -C firmware/chompi-pipes/code/tests test
 */
#include "Percussion.h"
#include "OrganEngine.h"
#include "OrganPreset.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using chompi::Percussion;
using chompi::OrganEngine;

static constexpr float kPi = 3.14159265358979f;

/* ------------------------------------------------------------------ */
/* Helper-level tests                                                  */
/* ------------------------------------------------------------------ */

static void test_init_settled()
{
    Percussion p;
    p.Init(48000.f);
    assert(!p.IsEnabled());
    assert(p.EnableRamp() == 0.f);
    assert(p.Decay() == 0.f);
    assert(!p.PathActive());
    assert(p.Process() == 0.f); /* exact zero, no work */
}

static void test_enable_ramp_endpoints(float sr)
{
    Percussion p;
    p.Init(sr);
    p.SetEnabled(true);
    assert(p.IsEnabled());
    assert(p.PathActive());
    const int fade_samples = static_cast<int>(Percussion::kFadeSeconds * sr);
    float last = 0.f;
    for(int i = 0; i < fade_samples + 4; i++)
    {
        p.Process();
        const float r = p.EnableRamp();
        assert(r >= last); /* monotonic up */
        last = r;
    }
    assert(p.EnableRamp() == 1.f); /* exact endpoint */

    p.SetEnabled(false);
    last = 1.f;
    for(int i = 0; i < fade_samples + 4; i++)
    {
        p.Process();
        const float r = p.EnableRamp();
        assert(r <= last); /* monotonic down */
        last = r;
    }
    assert(p.EnableRamp() == 0.f); /* exact endpoint */
    assert(!p.PathActive());       /* settled: bypass again */
}

static void test_trigger_attack_and_half_life(float sr)
{
    Percussion p;
    p.Init(sr);
    p.SetEnabled(true);
    /* settle the enable ramp first so only the envelope is measured */
    for(int i = 0; i < static_cast<int>(.02f * sr); i++)
        p.Process();
    assert(p.Process() == 0.f); /* no trigger: no output */

    p.Trigger();
    /* attack smoothing: ~1 ms linear rise; decay barely moves in 1 ms */
    const int attack_samples = static_cast<int>(Percussion::kAttackSeconds * sr);
    float lvl = 0.f;
    for(int i = 0; i < attack_samples; i++)
        lvl = p.Process();
    assert(lvl > .85f && lvl <= 1.0001f); /* nearly full after 1 ms */

    /* half-life: 60 ms after the trigger the level is ~0.5, at 120 ms
     * ~0.25 (sample-rate independent) */
    const int to_60 = static_cast<int>(.060f * sr) - attack_samples;
    for(int i = 0; i < to_60; i++)
        lvl = p.Process();
    assert(fabsf(lvl - .5f) < .02f);
    const int to_120 = static_cast<int>(.060f * sr);
    for(int i = 0; i < to_120; i++)
        lvl = p.Process();
    assert(fabsf(lvl - .25f) < .015f);

    /* settle: approximately -60 dB after 600 ms, then EXACT zero */
    bool saw_exact_zero = false;
    for(int i = 0; i < static_cast<int>(.700f * sr); i++)
    {
        lvl = p.Process();
        if(lvl == 0.f)
        {
            saw_exact_zero = true;
            break;
        }
    }
    assert(saw_exact_zero);
    /* stays exactly zero forever: no denormals, no residual work */
    for(int i = 0; i < 100000; i++)
        assert(p.Process() == 0.f);
}

static void test_retrigger_continuity(float sr)
{
    Percussion p;
    p.Init(sr);
    p.SetEnabled(true);
    for(int i = 0; i < static_cast<int>(.02f * sr); i++)
        p.Process();
    p.Trigger();
    for(int i = 0; i < static_cast<int>(.200f * sr); i++)
        p.Process(); /* mid-tail */
    const float pre = p.Process();
    assert(pre > .05f && pre < .2f);
    p.Trigger(); /* restart mid-tail */
    /* the 1 ms attack restart keeps every per-sample step tiny
     * (click-free) and climbs back toward the peak */
    float prev = 0.f;
    const float max_step = 1.2f / (Percussion::kAttackSeconds * sr);
    for(int i = 0; i < static_cast<int>(.002f * sr); i++)
    {
        const float l = p.Process();
        assert(fabsf(l - prev) <= max_step);
        prev = l;
    }
    assert(prev > .8f); /* back near the peak after the 1 ms ramp */
}

static void test_gate_rates(float sr)
{
    Percussion p;
    p.Init(sr);
    /* 1 ms up while sounding */
    float g = 0.f;
    const int up = static_cast<int>(Percussion::kAttackSeconds * sr);
    for(int i = 0; i < up + 4; i++)
        g = p.AdvanceGate(g, true);
    assert(g == 1.f);
    assert(p.AdvanceGate(1.5f, true) == 1.f); /* clamped */
    /* 5 ms down on release/steal */
    const int down = static_cast<int>(Percussion::kFadeSeconds * sr);
    for(int i = 0; i < down + 4; i++)
        g = p.AdvanceGate(g, false);
    assert(g == 0.f);
    assert(p.AdvanceGate(-.5f, false) == 0.f); /* clamped */
}

/* ------------------------------------------------------------------ */
/* OrganEngine integration tests                                       */
/* ------------------------------------------------------------------ */

static constexpr float kSr      = 48000.f;
static constexpr int   kBlock   = 24;
static constexpr int   kNoteC3  = 48; /* key_map KEY_1 */

/** Pipe-2 (2 2/3') frequency for a MIDI note, matching the engine's
 *  NoteToHz x PipeRatio(2). */
static float Pipe2Hz(int note)
{
    return 440.f * powf(2.f, (static_cast<float>(note) - 69.f) / 12.f)
           * 2.9966142f;
}

struct Rig
{
    OrganEngine           engine;
    chompi::PlateReverb::T *plate_mem;
    float *     delay_l;
    float *     delay_r;
    float       buf[4][kBlock];
    float *     out[4];

    void Init()
    {
        plate_mem = new chompi::PlateReverb::T[chompi::PlateReverb::kDelayBufferSize];
        delay_l   = new float[chompi::StereoBbdDelay::kBufferSize];
        delay_r   = new float[chompi::StereoBbdDelay::kBufferSize];
        engine.Init(kSr, plate_mem, delay_l, delay_r);
        for(int c = 0; c < 4; c++)
            out[c] = buf[c];
    }

    void Block()
    {
        engine.Prepare(kBlock);
        engine.Process(out, kBlock);
    }

    void RunMs(float ms)
    {
        const int blocks = static_cast<int>(ms * .001f * kSr / kBlock);
        for(int b = 0; b < blocks; b++)
            Block();
    }

    /** Render `ms` and return the peak |out[0]|. */
    float PeakMs(float ms)
    {
        float peak      = 0.f;
        const int blocks = static_cast<int>(ms * .001f * kSr / kBlock);
        for(int b = 0; b < blocks; b++)
        {
            Block();
            for(int i = 0; i < kBlock; i++)
            {
                const float a = fabsf(buf[0][i]);
                if(a > peak)
                    peak = a;
            }
        }
        return peak;
    }

    /** Render `ms` into a captured vector (out[0]). */
    void CaptureMs(float ms, std::vector<float> &dst)
    {
        const int blocks = static_cast<int>(ms * .001f * kSr / kBlock);
        for(int b = 0; b < blocks; b++)
        {
            Block();
            for(int i = 0; i < kBlock; i++)
                dst.push_back(buf[0][i]);
        }
    }

    /** Percussion isolation: every pipe level at 0 leaves the raw pipe
     *  tap as the ONLY signal path. The pipe gains slew to zero with a
     *  coefficient-0.001 fonepole (a 1000-sample time constant), so
     *  give them ~400 ms to reach negligible residue before measuring. */
    void IsolatePercussion()
    {
        for(int p = 0; p < OrganEngine::kNumPipes; p++)
            engine.SetPipeStep(p, 0);
        RunMs(400.f);
    }

    /** Enable percussion and settle the 5 ms enable ramp. */
    void EnablePercussionSettled()
    {
        engine.SetPercussionEnabled(true);
        RunMs(20.f);
    }
};

static void test_engine_parameter_positions()
{
    Rig r;
    r.Init();
    assert(r.engine.GetMasterStep() == 8);
    assert(r.engine.GetFineTuneStep() == 16);
    assert(r.engine.GetFineTuneCents() == 0);
    assert(r.engine.GetDelayTimeStep() == 15);
    assert(r.engine.GetDelayTimeMs() == 256);

    r.engine.SetPipeCharacterStep(0, 32);
    r.engine.SetPipeLengthStep(0, 19);
    assert(r.engine.GetPipeCharacterStep(0) == 32);
    assert(r.engine.GetPipeLengthStep(0) == 19);
    assert(r.engine.GetPipeLengthOctave(0) == 0);
    r.engine.SetPipeLengthStep(0, r.engine.GetPipeLengthStep(0) + 1);
    assert(r.engine.GetPipeLengthStep(0) == 20);
    assert(r.engine.GetPipeLengthOctave(0) == 1);

    r.engine.SetVibratoDepthStep(16);
    r.engine.SetVibratoDelayStep(32);
    r.engine.SetDriveToneStep(17);
    r.engine.SetReverbDecayStep(32);
    r.engine.SetDelayMixStep(16);
    r.engine.SetDelayTimeStep(32);

    chompi::OrganPreset preset;
    preset.SetDefaults();
    chompi::CaptureFromEngine(preset, r.engine);
    assert(preset.length_step[0] == 20);
    assert(preset.character_step[0] == 32);
    assert(preset.vibrato_depth_step == 16);
    assert(preset.vibrato_delay_step == 32);
    assert(preset.drive_tone_step == 17);
    assert(preset.reverb_decay_step == 32);
    assert(preset.delay_mix_step == 16);
    assert(preset.delay_time_step == 32);

    r.engine.SetPipeLengthStep(0, 4);
    chompi::ApplyToEngine(preset, r.engine);
    assert(r.engine.GetPipeLengthStep(0) == 20);
    assert(r.engine.GetPipeLengthOctave(0) == 1);
    assert(r.engine.GetDelayTimeStep() == 32);
    assert(r.engine.GetDelayTimeMs() == 500);
}

/** Windowed DFT magnitude at an exact frequency. */
static float MagAt(const std::vector<float> &x, float freq)
{
    const size_t n = x.size();
    double re = 0., im = 0.;
    for(size_t i = 0; i < n; i++)
    {
        const double w = .5 - .5 * cos(2. * kPi * i / n); /* Hann */
        const double ph = 2. * kPi * freq * i / kSr;
        re += x[i] * w * cos(ph);
        im -= x[i] * w * sin(ph);
    }
    return static_cast<float>(sqrt(re * re + im * im) / n * 2.);
}

static void test_engine_harmonic_and_peak()
{
    Rig r;
    r.Init();
    r.IsolatePercussion();
    r.EnablePercussionSettled();

    r.engine.NoteOn(kNoteC3, 1.f);
    const float peak = r.PeakMs(30.f);
    /* predicted peak: 0.75 tap x 1/28.5 normalization x 0.25 master gain */
    const float expect = .75f * (1.f / 28.5f) * .25f;
    assert(peak > .7f * expect && peak < 1.4f * expect);

    /* the rendered ping is the pipe-2 (2 2/3') harmonic, not the
     * fundamental or the octave pipes */
    std::vector<float> cap;
    r.CaptureMs(100.f, cap);
    const float f0 = 440.f * powf(2.f, (kNoteC3 - 69.f) / 12.f);
    const float m2 = MagAt(cap, Pipe2Hz(kNoteC3));
    assert(m2 > 20.f * MagAt(cap, f0));
    assert(m2 > 20.f * MagAt(cap, 2.f * f0));
    assert(m2 > 20.f * MagAt(cap, 4.f * f0));

    /* 60 ms half-life in the rendered envelope: compare windowed peaks
     * 10..20 ms after the onset vs 70..80 ms (attack is done by 1 ms) */
    r.engine.NoteOff(kNoteC3);
    Rig r2;
    r2.Init();
    r2.IsolatePercussion();
    r2.EnablePercussionSettled();
    r2.engine.NoteOn(kNoteC3, 1.f);
    r2.RunMs(10.f);
    const float early = r2.PeakMs(10.f); /* 10..20 ms */
    r2.RunMs(50.f);
    const float late = r2.PeakMs(10.f); /* 70..80 ms */
    const float ratio = late / early;
    assert(ratio > .4f && ratio < .65f); /* ~0.5 over 60 ms */
}

static void test_engine_single_trigger_semantics()
{
    /* legato: a second note while the first is held does NOT restart
     * the decay - it shares the remaining tail */
    {
        Rig r;
        r.Init();
        r.IsolatePercussion();
        r.EnablePercussionSettled();
        r.engine.NoteOn(kNoteC3, 1.f);
        const float fresh = r.PeakMs(30.f);
        r.RunMs(70.f);           /* 100 ms in: decayed to ~0.31 */
        r.engine.NoteOn(52, 1.f); /* legato: no retrigger */
        const float after = r.PeakMs(30.f);
        /* both voices share the remaining tail: worst case 2 gates x
         * 0.31 envelope = ~0.6x fresh; a retrigger would be ~2x */
        assert(after < .75f * fresh);
    }

    /* after the decay fully settles a legato note adds (nearly)
     * nothing */
    {
        Rig r;
        r.Init();
        r.IsolatePercussion();
        r.EnablePercussionSettled();
        r.engine.NoteOn(kNoteC3, 1.f);
        r.RunMs(1200.f); /* decay settled to exact zero */
        r.engine.NoteOn(52, 1.f);
        const float after = r.PeakMs(50.f);
        assert(after < 1e-6f);
    }

    /* all-notes-released rearm: release, then a new note pings again,
     * even while a long pipe release tail is still sounding */
    {
        Rig r;
        r.Init();
        r.IsolatePercussion();
        for(int p = 0; p < OrganEngine::kNumPipes; p++)
            r.engine.SetPipeReleaseStep(p, 32); /* 10 s tails */
        r.EnablePercussionSettled();
        r.engine.NoteOn(kNoteC3, 1.f);
        const float fresh = r.PeakMs(30.f);
        r.RunMs(300.f);
        r.engine.NoteOff(kNoteC3);   /* voice keeps releasing... */
        r.RunMs(20.f);               /* ...but the phrase is over */
        r.engine.NoteOn(52, 1.f);    /* all-up -> retrigger */
        const float again = r.PeakMs(30.f);
        assert(again > .7f * fresh && again < 1.4f * fresh);
    }

    /* enabling under held notes synthesizes no attack */
    {
        Rig r;
        r.Init();
        r.IsolatePercussion();
        r.engine.NoteOn(kNoteC3, 1.f);
        r.RunMs(100.f);
        r.engine.SetPercussionEnabled(true); /* mid-hold: no trigger */
        const float after = r.PeakMs(50.f);
        assert(after < 1e-6f);
        /* ...but after a full release the next note pings normally */
        r.engine.NoteOff(kNoteC3);
        r.engine.NoteOn(52, 1.f);
        assert(r.PeakMs(30.f) > 1e-3f);
    }
}

static void test_engine_duplicate_events_and_chords()
{
    /* duplicate note-on/off never double-counts: one release of a
     * twice-pressed note ends the phrase, and a spurious extra release
     * does not open the rearm early */
    {
        Rig r;
        r.Init();
        r.IsolatePercussion();
        r.EnablePercussionSettled();
        r.engine.NoteOn(kNoteC3, 1.f);
        r.engine.NoteOn(kNoteC3, 1.f); /* duplicate on: one count */
        const float fresh = r.PeakMs(30.f);
        r.engine.NoteOff(kNoteC3);     /* phrase over despite two ons */
        r.engine.NoteOff(kNoteC3);     /* duplicate off: no underflow */
        r.engine.NoteOn(52, 1.f);
        assert(r.PeakMs(30.f) > .7f * fresh);
    }
    {
        Rig r;
        r.Init();
        r.IsolatePercussion();
        r.EnablePercussionSettled();
        r.engine.NoteOn(kNoteC3, 1.f);
        r.engine.NoteOn(52, 1.f);
        r.PeakMs(30.f);
        r.engine.NoteOff(kNoteC3);
        r.engine.NoteOff(kNoteC3);     /* spurious duplicate */
        r.engine.NoteOn(55, 1.f);      /* 52 still held: legato, 2
                                        * voices share the ~0.59 tail */
        const float legato = r.PeakMs(30.f);
        r.engine.NoteOff(52);
        r.engine.NoteOff(55);
        /* 2-voice fresh reference from a true all-up state */
        r.engine.NoteOn(kNoteC3, 1.f);
        r.engine.NoteOn(52, 1.f);
        const float fresh = r.PeakMs(30.f);
        /* same two gates open on both sides; legato keeps the ~0.59x
         * tail where a retrigger would restore the full peak */
        assert(legato < .8f * fresh);
    }

    /* chord: members share one decay rather than restarting per voice -
     * the 60 ms half-life shape is unchanged for a 3-note chord */
    {
        Rig r;
        r.Init();
        r.IsolatePercussion();
        r.EnablePercussionSettled();
        r.engine.NoteOn(kNoteC3, 1.f);
        r.engine.NoteOn(52, 1.f);
        r.engine.NoteOn(55, 1.f);
        r.RunMs(10.f);
        const float early = r.PeakMs(10.f);
        r.RunMs(50.f);
        const float late = r.PeakMs(10.f);
        const float ratio = late / early;
        assert(ratio > .35f && ratio < .65f); /* same shared envelope */
    }
}

static void test_engine_gates_release_steal_toggle()
{
    /* release: the per-voice gate closes in ~5 ms even with the longest
     * pipe release - the ping does not ring on after key-up */
    {
        Rig r;
        r.Init();
        r.IsolatePercussion();
        for(int p = 0; p < OrganEngine::kNumPipes; p++)
            r.engine.SetPipeReleaseStep(p, 32); /* 10 s */
        r.EnablePercussionSettled();
        r.engine.NoteOn(kNoteC3, 1.f);
        r.RunMs(150.f); /* mid-decay */
        r.engine.NoteOff(kNoteC3);
        r.RunMs(10.f); /* the 5 ms gate fade is audible by design... */
        const float tail = r.PeakMs(30.f);
        assert(tail < 1e-6f);  /* ...then nothing rings on */
    }

    /* toggle off mid-ping: the 5 ms enable fade kills the sound
     * quickly */
    {
        Rig r;
        r.Init();
        r.IsolatePercussion();
        r.EnablePercussionSettled();
        r.engine.NoteOn(kNoteC3, 1.f);
        r.RunMs(100.f);
        r.engine.SetPercussionEnabled(false);
        r.RunMs(10.f); /* the 5 ms enable fade is audible by design... */
        const float tail = r.PeakMs(30.f);
        assert(tail < 1e-6f);
    }

    /* stealing and a cancelled pending note stay bounded and
     * click-free; the phrase still rearms afterwards */
    {
        Rig r;
        r.Init();
        r.IsolatePercussion();
        r.EnablePercussionSettled();
        const int chord[6] = {48, 52, 55, 60, 64, 67};
        for(int i = 0; i < 6; i++)
            r.engine.NoteOn(chord[i], 1.f);
        r.RunMs(50.f);
        r.engine.NoteOn(72, 1.f); /* steals the oldest */
        r.RunMs(2.f);             /* mid 5 ms steal fade... */
        r.engine.NoteOff(72);     /* ...then cancel the pending note */
        float peak = 0.f;
        for(int b = 0; b < 100; b++)
        {
            r.Block();
            for(int i = 0; i < kBlock; i++)
            {
                const float a = fabsf(r.buf[0][i]);
                if(a > peak)
                    peak = a;
            }
        }
        assert(peak < .02f); /* bounded, well under full scale */
        for(int i = 0; i < 6; i++)
            r.engine.NoteOff(chord[i]);
        r.engine.NoteOn(48, 1.f); /* all-up: rearm still works */
        assert(r.PeakMs(30.f) > 1e-3f);
    }
}

static void test_engine_independence_of_pipe_controls()
{
    /* pipe 2's level, mute and Character are AFTER the percussion tap:
     * a muted, Character-maxed pipe 2 renders bit-identical percussion
     * to a clean one (all pipes at level 0 isolate the tap) */
    Rig a;
    a.Init();
    a.IsolatePercussion();
    a.EnablePercussionSettled();
    a.engine.NoteOn(kNoteC3, 1.f);
    std::vector<float> cap_a;
    a.CaptureMs(80.f, cap_a);

    Rig b;
    b.Init();
    b.IsolatePercussion();
    b.engine.SetPipeMuted(2, true);
    b.engine.SetPipeCharacterStep(2, 64);
    b.EnablePercussionSettled();
    b.engine.NoteOn(kNoteC3, 1.f);
    std::vector<float> cap_b;
    b.CaptureMs(80.f, cap_b);

    assert(cap_a.size() == cap_b.size());
    for(size_t i = 0; i < cap_a.size(); i++)
        assert(cap_a[i] == cap_b[i]); /* exact: the tap is pre-everything */
}

static void test_engine_normalization_and_off_equivalence()
{
    /* normalization: one sustained note, one pipe at full level; the
     * percussion-enabled-settled render is the percussion-free render
     * scaled by exactly 24/28.5 (~1.5 dB of dry attenuation) */
    {
        Rig a;
        a.Init(); /* percussion never enabled: 1/24 */
        a.engine.SetPipeStep(0, 32);
        a.engine.SetPipeStep(1, 0);
        a.engine.SetPipeStep(2, 0);
        a.engine.SetPipeStep(3, 0);
        a.RunMs(400.f); /* pipe gains fully settled */
        a.engine.NoteOn(kNoteC3, 1.f);
        a.RunMs(500.f);
        std::vector<float> cap_a;
        a.CaptureMs(300.f, cap_a);

        Rig b;
        b.Init();
        b.engine.SetPipeStep(0, 32);
        b.engine.SetPipeStep(1, 0);
        b.engine.SetPipeStep(2, 0);
        b.engine.SetPipeStep(3, 0);
        b.RunMs(400.f); /* pipe gains fully settled */
        b.EnablePercussionSettled(); /* 1/28.5, decay never triggered */
        b.engine.NoteOn(kNoteC3, 1.f);
        b.RunMs(500.f);
        std::vector<float> cap_b;
        b.CaptureMs(300.f, cap_b);

        double ea = 0., eb = 0.;
        for(size_t i = 0; i < cap_a.size(); i++)
        {
            ea += static_cast<double>(cap_a[i]) * cap_a[i];
            eb += static_cast<double>(cap_b[i]) * cap_b[i];
        }
        const double ratio = sqrt(eb / ea);
        assert(ratio > 24. / 28.5 - .005 && ratio < 24. / 28.5 + .005);
    }

    /* settled-off equivalence: an engine whose percussion was enabled
     * then disabled and settled renders bit-identically to one that
     * never had it on (the original arithmetic path is preserved) */
    {
        Rig a;
        a.Init();
        Rig b;
        b.Init();
        b.engine.SetPercussionEnabled(true);
        b.RunMs(100.f);
        b.engine.SetPercussionEnabled(false);
        b.RunMs(100.f); /* settled: ramp exactly 0, never triggered */

        const int notes[3] = {48, 55, 52};
        for(int n = 0; n < 3; n++)
        {
            a.engine.NoteOn(notes[n], 1.f);
            b.engine.NoteOn(notes[n], 1.f);
            for(int blk = 0; blk < 100; blk++)
            {
                a.Block();
                b.Block();
                for(int i = 0; i < kBlock; i++)
                    assert(a.buf[0][i] == b.buf[0][i]);
            }
            a.engine.NoteOff(notes[n]);
            b.engine.NoteOff(notes[n]);
        }
    }

    /* bounded output: full pipes, six-voice chord, percussion triggered
     * - the 28.5-unit normalization keeps the master output within
     * full scale even at master gain 1 */
    {
        Rig r;
        r.Init();
        r.engine.SetMasterGain(1.f);
        for(int p = 0; p < OrganEngine::kNumPipes; p++)
            r.engine.SetPipeStep(p, 32);
        r.EnablePercussionSettled();
        const int chord[6] = {48, 52, 55, 60, 64, 67};
        for(int i = 0; i < 6; i++)
            r.engine.NoteOn(chord[i], 1.f);
        for(int b = 0; b < 200; b++)
        {
            r.Block();
            for(int i = 0; i < kBlock; i++)
            {
                assert(fabsf(r.buf[0][i]) <= 1.001f);
                assert(std::isfinite(r.buf[0][i]));
            }
        }
    }
}

static void test_engine_aux_mix()
{
    Rig dry;
    dry.Init();
    Rig mixed;
    mixed.Init();
    float aux_l[kBlock], aux_r[kBlock];
    float dry_l[kBlock], dry_r[kBlock];
    float rec_l[kBlock], rec_r[kBlock];
    dry.engine.SetRecordTap(dry_l, dry_r);
    mixed.engine.SetRecordTap(rec_l, rec_r);
    for(int sample = 0; sample < kBlock; sample++)
    {
        aux_l[sample] = .2f * sinf(sample * .4f);
        aux_r[sample] = -.3f * cosf(sample * .2f);
    }
    for(float gain : {0.f, .5f, 1.f})
    {
        dry.engine.SetMasterGain(gain);
        mixed.engine.SetMasterGain(gain);
        dry.RunMs(400.f);
        mixed.RunMs(400.f);
        for(int block = 0; block < 20; block++)
        {
            dry.Block();
            mixed.engine.Prepare(kBlock);
            mixed.engine.Process(mixed.out, kBlock, aux_l, aux_r);
            for(int sample = 0; sample < kBlock; sample++)
            {
                assert(fabsf(rec_l[sample] - dry_l[sample]
                             - aux_l[sample] * OrganEngine::kRecordTapGain) < 1e-6f);
                assert(fabsf(rec_r[sample] - dry_r[sample]
                             - aux_r[sample] * OrganEngine::kRecordTapGain) < 1e-6f);
                assert(fabsf(mixed.buf[0][sample] - dry.buf[0][sample]
                             - aux_l[sample] * gain) < 1e-5f);
                assert(fabsf(mixed.buf[1][sample] - dry.buf[1][sample]
                             - aux_r[sample] * gain) < 1e-5f);
                assert(mixed.buf[0][sample] == mixed.buf[2][sample]);
                assert(mixed.buf[1][sample] == mixed.buf[3][sample]);
            }
        }
        dry.engine.NoteOn(kNoteC3, 1.f);
        mixed.engine.NoteOn(kNoteC3, 1.f);
    }
    dry.Block();
    mixed.Block();
    for(int channel = 0; channel < 4; channel++)
        for(int sample = 0; sample < kBlock; sample++)
            assert(dry.buf[channel][sample] == mixed.buf[channel][sample]);
}

int main()
{
    test_engine_parameter_positions();
    test_init_settled();
    test_enable_ramp_endpoints(48000.f);
    test_enable_ramp_endpoints(32000.f);
    test_trigger_attack_and_half_life(48000.f);
    test_trigger_attack_and_half_life(32000.f);
    test_retrigger_continuity(48000.f);
    test_gate_rates(48000.f);
    test_gate_rates(32000.f);

    test_engine_harmonic_and_peak();
    test_engine_single_trigger_semantics();
    test_engine_duplicate_events_and_chords();
    test_engine_gates_release_steal_toggle();
    test_engine_independence_of_pipe_controls();
    test_engine_normalization_and_off_equivalence();
    test_engine_aux_mix();
    printf("percussion_test: all tests passed\n");
    return 0;
}
