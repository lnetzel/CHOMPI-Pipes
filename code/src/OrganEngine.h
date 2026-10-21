/** @file OrganEngine.h
 *  @brief Polyphonic organ voice engine: 6 voices x 4 pipe registrations.
 *
 *  Implements FR-1/FR-6 and DSP-1..DSP-3 of chompi-pipes- specification.md:
 *  - Pipe ratios 1x / 2x / 2^(19/12)x / 4x with 20 kHz octave foldback,
 *    plus a per-pipe +/-2-octave pipe-length offset (encoder page 3)
 *  - Per-pipe Character: tonewheel-inspired harmonic coloration, 32
 *    control positions mapped to the shaper's 0..64 DSP range, one
 *    independent amount per pipe shared across all voices
 *    (TonewheelCharacter.h). Position 0 is the exact clean sine; 32 is
 *    clearly aged but still organ-like. One table-lookup sine
 *    oscillator per pipe (SineLut.h: 2048-entry interpolated table,
 *    ~1.2e-6 max error - audibly identical to the libm sinf oscillator
 *    it replaced, at a fraction of the CPU); the 2nd/3rd harmonics are
 *    derived algebraically from the sine sample, smoothly eased in and
 *    anti-alias-masked per folded note. On the 8' pipe the harmonic
 *    gains get an extra low-frequency boost (full at or below 150 Hz,
 *    smoothstepped to 0 at 400 Hz), so the bottom octave gains grit as
 *    Character rises - the organ-builder's quint-reinforcement trick
 *    for low fundamentals; step 0 stays the exact clean sine.
 *  - Free-running oscillators (phase never reset at note-on)
 *  - Per-pipe attack/release gate envelopes: 32 log-spaced steps each,
 *    5 ms (step 0, boot default = the original fixed timing) to 10 s
 *    (EnvelopeMap.h). Each pipe fades in/out at its own rate, so pipes
 *    can stagger their fade-in; a voice frees only when every pipe's
 *    release finishes. 5 ms steal fade
 *  - Oldest-first voice stealing; same-note retrigger reuses its voice.
 *    The steal fade completes inside Process(), but the new voice
 *    starts once per block in Prepare() (<= 0.5 ms later, inaudible),
 *    so the per-sample loop never does note-event work
 *  - Fixed 1/24 output scale so 6 voices x 4 pipes cannot clip
 *  - Pipe levels in 32 steps, gain = (step/32)^2, smoothed per sample
 *  - Distortion: 32-step fuzz saturation stage on the mix
 *    (Distortion.h), placed before the master gain so the drive
 *    character is level-independent; 0% is an exact bypass (DSP-5).
 *    SHIFT + volume-encoder turn on page 2 is a tone control sweeping
 *    the fuzz's post low-pass logarithmically from 500 Hz (0%, dark)
 *    to 10 kHz (100%, boot default), smoothed click-free. The filter
 *    is two identical cascaded one-pole stages (approximately
 *    12 dB/octave stopband roll-off); the mapped frequency is the
 *    PER-STAGE cutoff, so the combined -3 dB point sits lower, and
 *    tone 100 is intentionally darker than the historical single-pole
 *    Fuzz. The tone is saved in the SD-card presets (FR-4b). An input-referenced output leveling (per-
 *  block RMS measurement, ~50 ms smoothing, trim = (dry level + 1.5 dB
 *  headroom) / fuzzed level, clamped <= 1 and floored at -30 dB),
 *  folded through the
 *    live wet amount, is applied after the rotary and before the plate
 *    reverb, so the reverb also receives the leveled level (no user
 *    control)
 *  - Rotary speaker: 60s Leslie-style mono->stereo stage after the
 *    distortion (800 Hz crossover, Doppler horn + AM drum, two virtual
 *    mics 180 deg apart); 0% is an exact bypass (DSP-6)
 *  - Plate reverb: stereo-input vintage-1960s-inspired plate after
 *    the rotary (PlateReverb.h; Griesinger/Dattorro tank adapted from
 *    chompi-wave's reverb, reworked stereo-in): each channel excites
 *    its own diffused tank side, so the rotary's left/right
 *    differences survive in the wet; the tank's decay-scaled
 *    cross-feedback progressively couples the channels into one
 *    diffuse field. Mix is adjustable in 32 steps through a linear
 *    complementary dry/wet crossfade with a ~10 ms ramp: step 0 is
 *    bit-exact dry, step 32 is wet only. Decay is
 *    adjustable 0.2-20 s in 32 log-spaced positions (boots at 0.2 s). The
 *    tank runs continuously (live tails, no stale state); the 128 KiB
 *    float delay buffer is caller-owned SDRAM passed to Init().
 *  - Stereo BBD-inspired delay (StereoBbdDelay.h) after the rotary and
 *    its fuzz-output leveling, before the plate reverb: two caller-owned
 *    128 KiB SDRAM float lines, 40-500 ms with smooth pitch-gliding time
 *    changes, fixed ~35% darkening feedback, DC control and gentle
 *    bounded saturation; subtle L/R timing/modulation differences keep
 *    it stereo (not ping-pong). Linear complementary mix crossfade with
 *    a ~10 ms ramp: settled 0% is bit-exact dry, 100% is wet only. The
 *    buffers keep running at dry mix and across preset changes, so
 *    echoes never drop. Boots dry at position 15 (256 ms); saved in the
 *    SD-card presets and addressed by CC 113-114.
 *  - Vibrato: global pitch modulation from one shared, free-running
 *    5 Hz LFO (Vibrato.h). Depth uses 32 positions across 0-50 cents
 *    peak, applied to
 *    every voice as a per-sample frequency multiplier on the stored
 *    base frequencies (no oscillator phase resets, no drift). Depth 0
 *    is an exact bypass. Separate from the rotary speaker's Doppler.
 *  - Modulation Mode drift: thirteen slow, free-running
 *    DriftModulators (one per pipe for volume, Character and pipe
 *    length, plus one global instance for the distortion drive) give
 *    the organ a living, organic movement. Each parameter's user value
 *    is the window center; the per-pipe amount (32 steps, boot 0)
 *    scales a +/-20% full-scale window that slides to stay inside
 *    [0,1] (DriftModulator::ApplyWindow); the drive amount is a single
 *    global value with the same window around the drive center.
 *    Modulated gain/Character/drive targets are recomputed once per
 *    block in Prepare(); the existing
 *    ~1 ms gain slew and ~10 ms Character ramp smooth the block steps.
 *    Amount 0 is bit-exact the unmodulated center; mute still forces
 *    silence. Saved in the SD-card presets; per-pipe amounts use CC
 *    94-105 and global drive/delay amounts use CC 115-116.
 *  - Hammond-style percussion (Percussion.h): toggled by the Play/Pause
 *    key, one SHARED decaying burst of the raw 2 2/3' pipe sine (tapped
 *    before Character, pipe gain and the pipe envelopes) is added to
 *    every sounding voice through a 1 ms per-voice click-prevention
 *    gate. Classic single triggering: only a note-on from an
 *    all-notes-released state restarts the shared 60 ms half-life
 *    decay; legato/chord notes share its remaining tail. Percussion is
 *    independent of pipe levels, mutes, Character, modulation amounts
 *    and the pipe envelope times, but follows note pitch, fine tuning,
 *    foldback, vibrato, velocity and master gain, and passes through
 *    the fuzz/rotary/reverb chain. With 0.75-peak percussion per voice
 *    the conservative six-voice sum bound rises from 24 to 28.5, so the
 *    mix normalization interpolates 1/24 -> 1/28.5 with the 5 ms enable
 *    ramp; settled-off is exactly 1/24 and the whole path is skipped
 *    bit-exactly. On/off is saved in the SD-card presets.
 *  - Smoothed master gain (existing CHOMPI volume-knob behaviour)
 *  - SD-recording tap (SdRecorder): Process() copies the pre-master-gain
 *    stereo bus into caller-owned block buffers at a fixed -6 dB level
 *    (kRecordTapGain), so card takes are DAW-ready and independent of
 *    the volume knob; the headphone/master outputs are unchanged.
 */
#pragma once
#include "daisysp.h"
#include "SineLut.h"
#include "Distortion.h"
#include "RotarySpeaker.h"
#include "TonewheelCharacter.h"
#include "PlateReverb.h"
#include "StereoBbdDelay.h"
#include "Vibrato.h"
#include "EnvelopeMap.h"
#include "DriftModulator.h"
#include "Percussion.h"
#include "ParameterSteps.h"

namespace chompi
{

class OrganEngine
{
  public:
    static constexpr int kNumVoices         = 6;
    static constexpr int kNumPipes          = 4;
    static constexpr int kMaxSteps          = 32;
    static constexpr int kMaxCharacterSteps = 64;

    /** Pipe frequency ratios: 8', 4', 2 2/3' (equal-tempered), 2' */
    static float PipeRatio(int p)
    {
        switch(p)
        {
            case 1: return 2.f;
            case 2: return 2.9966142f; /* 2^(19/12) */
            case 3: return 4.f;
            default: return 1.f;
        }
    }
    /** Octave multiplier for a pipe-length offset (-2..+2): exact
     *  powers of two, so the default offset 0 (1x) keeps the frequency
     *  path bit-identical to the unshifted sound. */
    static float PipeLengthMult(int oct)
    {
        const float kMult[5] = {.25f, .5f, 1.f, 2.f, 4.f};
        oct = oct < -2 ? -2 : (oct > 2 ? 2 : oct);
        return kMult[oct + 2];
    }
    static constexpr float kFoldbackHz     = 20000.f;
    static constexpr float kMixScale       = 1.f / 24.f;
    /** Fixed record-tap level: -6 dB (x0.5) on the pre-master-gain bus,
     *  so SD-card takes land DAW-ready regardless of the volume knob. */
    static constexpr float kRecordTapGain  = .5f;
    static constexpr int   kDefaultStep          = 16; /* 50% volume */
    static constexpr int   kDefaultCharacterStep = 0;  /* clean sine, shaping
                                                        * bypassed */
    static constexpr int   kDefaultPipeLengthStep = 16;
    static constexpr int   kDefaultMasterStep     = 16; /* gain 0.5 */
    static constexpr int   kDefaultDriveStep     = 0;  /* overdrive off */
    static constexpr int   kDefaultRotaryStep    = 0;  /* rotary off (exact
                                                        * bypass) */
    static constexpr int   kDefaultReverbMixStep    = 0; /* reverb dry */
    static constexpr int   kDefaultReverbDecayStep  = 0; /* 0.2 s decay */
    static constexpr int   kMaxReverbDecaySteps     = 32;
    static constexpr float kMinReverbDecaySeconds   = .2f;
    static constexpr float kMaxReverbDecaySeconds   = 20.f;
    static constexpr int   kMaxVibratoDepthCents    = Vibrato::kMaxDepthCents;
    static constexpr int   kDefaultVibratoDepth     = 0; /* vibrato off */
    static constexpr int   kMaxVibratoDelaySteps    = 32; /* 0..5 s */
    static constexpr int   kDefaultVibratoDelayStep = 0; /* delay off */
    static constexpr int   kMaxEnvSteps     = EnvelopeMap::kMaxSteps;
    static constexpr int   kDefaultEnvStep  = 0; /* 5 ms: the original timing */
    static constexpr int   kMaxModSteps     = 32; /* per-pipe modulation amounts */
    static constexpr int   kLengthModMaxCents = 20; /* pipe-length drift
                                                       * half-width at 100% */
    static constexpr int   kMaxPipeLengthOctave    = 2; /* +/-2 octaves
                                                        * from nominal */
    static constexpr int   kDefaultPipeLengthOctave = 0; /* nominal
                                                        * footage */
    static constexpr int   kDefaultDelayMixPercent = 0; /* delay dry */
    static constexpr int   kDefaultDelayTimeStep   = 15;
    static constexpr int   kDefaultDelayTimeMs     = 256;
    static constexpr int   kMinDelayTimeMs         = 40;
    static constexpr int   kMaxDelayTimeMs         = 500;
    /** Delay page-3 combined control: 0..50 sweeps the dry/wet mix
     *  0..50%, above 50 the mix locks at 50% and the feedback rises
     *  from the 35% floor to 120%; past ~100% (the top ~12% of the
     *  bar) the delay self-oscillates into a bounded drone. */
    static constexpr int   kDelayMixPivotPercent   = 50;
    static constexpr float kDelayMixAtPivot        = .5f;
    static constexpr float kDelayFeedbackFloor     = .35f;
    static constexpr float kDelayFeedbackMax
        = StereoBbdDelay::kMaxFeedback;

    /** `reverb_delay` must point to PlateReverb::kDelayBufferSize
     *  PlateReverb::T elements in writable memory (ORGAN uses SDRAM);
     *  pass it
     *  only after hw.Init() has configured the SDRAM controller.
     *  `delay_left`/`delay_right` must each point to
     *  StereoBbdDelay::kBufferSize floats, likewise in SDRAM. */
    void Init(float sample_rate, PlateReverb::T *reverb_delay,
              float *delay_left, float *delay_right)
    {
        sample_rate_ = sample_rate;
        steal_inc_   = 1.f / (0.005f * sample_rate); /* 5 ms steal fade */
        age_counter_ = 0;
        fine_step_   = kMaxSteps / 2;
        fine_mult_   = 1.f;
        freqs_dirty_ = false;
        /* MIDI note -> Hz, equal-tempered A440: filled once here with
         * the same expression the old per-note powf path used, so the
         * table values are bit-identical to it; fine tune multiplies on
         * top via fine_mult_ (UpdateFreqs). */
        for(int n = 0; n < 128; n++)
            note_hz_[n] = 440.f
                          * powf(2.f, (static_cast<float>(n) - 69.f) / 12.f);
        character_.Init(sample_rate);
        for(int p = 0; p < kNumPipes; p++)
        {
            character_step_[p] = kDefaultCharacterStep;
            character_.SetStep(
                p, ParameterSteps::CharacterDspStep(kDefaultCharacterStep));
        }

        for(int v = 0; v < kNumVoices; v++)
        {
            Voice &voice = voices_[v];
            voice.state        = IDLE;
            voice.note         = 0;
            voice.pending_note = 0;
            voice.pending_vel  = 0.f;
            voice.velocity     = 0.f;
            voice.age          = 0;
            voice.perc_gate    = 0.f;
            for(int p = 0; p < kNumPipes; p++)
            {
                voice.pipe_env[p] = 0.f;
                voice.osc[p].Init(sample_rate);
                voice.osc[p].SetFreq(100.f);
                base_freq_[v][p] = 100.f;
            }
        }

        for(int p = 0; p < kNumPipes; p++)
        {
            pipe_steps[p]  = kDefaultStep;
            pipe_muted_[p] = false; /* mute boots clear */
            pipe_gain_[p] = pipe_gain_target_[p] = StepToGain(kDefaultStep);
            pipe_attack_step_[p]  = kDefaultEnvStep;
            pipe_release_step_[p] = kDefaultEnvStep;
            pipe_length_step_[p]  = kDefaultPipeLengthStep;
            attack_inc_[p]  = EnvelopeMap::StepToInc(kDefaultEnvStep,
                                                     sample_rate);
            release_inc_[p] = attack_inc_[p];
        }

        /* Modulation Mode drift: thirteen free-running modulators (4
         * pipes x {volume, Character, pipe length} + one global drive),
         * distinct fixed seeds (nonzero, xorshift32-safe) so the pipes
         * never run in lockstep; amounts boot at 0 = exact unmodulated
         * sound. */
        const uint32_t vol_seeds[kNumPipes]
            = {0x2545F491u, 0x8F3A7B15u, 0x6C8E9CF1u, 0x1F123BB5u};
        const uint32_t char_seeds[kNumPipes]
            = {0xD1B54A35u, 0x9E3779B9u, 0x4A3C6D27u, 0x85A5E1B3u};
        const uint32_t length_seeds[kNumPipes]
            = {0x7F4A7C15u, 0x1B873593u, 0xB5297A4Du, 0x02B5F5A9u};
        /* Per-pipe rate bands make the four length drifts clearly
         * distinct: the 8' pipe wanders slowest, the 2' pipe fastest
         * (half-swing ranges 3.6-14.3 s / 2.8-11.1 s / 2.2-8.7 s /
         * 1.7-6.9 s), so the pipes never appear to move in step. */
        const float length_rates[kNumPipes] = {0.70f, 0.90f, 1.15f, 1.45f};
        for(int p = 0; p < kNumPipes; p++)
        {
            vol_mod_step_[p]    = 0;
            char_mod_step_[p]   = 0;
            length_mod_step_[p] = 0;
            length_mod_mult_[p] = 1.f;
            vol_mod_[p].Init(sample_rate, vol_seeds[p]);
            char_mod_[p].Init(sample_rate, char_seeds[p]);
            length_mod_[p].Init(sample_rate, length_seeds[p],
                                length_rates[p]);
        }
        /* the single global drive drift runs at the base rate with its
         * own seed; amount boots 0 = exact unmodulated drive */
        drive_mod_step_ = 0;
        drive_mod_.Init(sample_rate, 0xC3D2E1F0u);
        length_mod_active_ = false;

        master_step_ = kDefaultMasterStep;
        master_gain_ = master_gain_target_
            = ParameterSteps::Normalized(kDefaultMasterStep);

        drive_step_ = kDefaultDriveStep;
        drive_ = drive_target_
            = static_cast<float>(kDefaultDriveStep) * (1.f / kMaxSteps);
        drive_mod_target_ = drive_target_;
        distortion_.Init(sample_rate); /* fuzz; tone boots at 100% */
        SetDriveToneStep(kMaxSteps);

        rotary_.Init(sample_rate);
        SetRotaryStep(kDefaultRotaryStep);

        plate_.Init(sample_rate, reverb_delay);
        reverb_mix_step_    = kDefaultReverbMixStep;
        reverb_decay_step_  = kDefaultReverbDecayStep; /* plate_.Init()
                                                        * already set 0.2 s */

        delay_.Init(sample_rate, delay_left, delay_right);
        delay_mix_step_ = ParameterSteps::LegacyPosition(
            kDefaultDelayMixPercent, 0, 100); /* boots exact dry */
        delay_time_step_ = kDefaultDelayTimeStep; /* 256 ms on the shared grid */
        delay_.SetDelayMs(static_cast<float>(GetDelayTimeMs()));
        /* the single global delay drift runs at the base rate with its
         * own seed; amount boots 0 = exact unmodulated delay */
        delay_mod_step_   = 0;
        delay_mod_active_ = false;
        delay_mod_.Init(sample_rate, 0xD4A137B2u);

        vibrato_.Init(sample_rate);
        vibrato_depth_step_ = ParameterSteps::LegacyPosition(
            kDefaultVibratoDepth, 0, kMaxVibratoDepthCents);
        vibrato_.SetDepth(ParameterSteps::VibratoDepthCents(
            vibrato_depth_step_)); /* boots exactly dry */
        vibrato_delay_step_ = kDefaultVibratoDelayStep;
        vibrato_.SetDelayMs(0.f); /* delay envelope off */

        percussion_.Init(sample_rate); /* boots disabled, settled */
        held_count_ = 0;
        for(int n = 0; n < 128; n++)
            note_held_[n] = false;
    }

    /** Apply one encoder click (or MIDI CC step) to a pipe level. The
 *      level stays editable while the pipe is muted; the mute keeps the
 *      gain target at 0 until the pipe is unmuted. */
    void SetPipeStep(int pipe, int step)
    {
        if(pipe < 0 || pipe >= kNumPipes)
            return;
        step = step < 0 ? 0 : (step > kMaxSteps ? kMaxSteps : step);
        pipe_steps[pipe] = static_cast<uint8_t>(step);
        UpdatePipeGainTarget(pipe);
    }

    int GetPipeStep(int pipe) const { return pipe_steps[pipe]; }

    /** Mute/unmute a pipe (SHIFT + pipe-encoder press). The mute is
 *      folded into pipe_gain_target_, so the existing ~1 ms gain slew
 *      in Process() makes it click-free, and the level step is
 *      preserved while muted and restored on unmute. Boots unmuted;
 *      saved in the SD-card presets; no MIDI CC. */
    void SetPipeMuted(int pipe, bool muted)
    {
        if(pipe < 0 || pipe >= kNumPipes)
            return;
        pipe_muted_[pipe] = muted;
        UpdatePipeGainTarget(pipe);
    }

    bool GetPipeMuted(int pipe) const
    {
        return (pipe < 0 || pipe >= kNumPipes) ? false
                                               : pipe_muted_[pipe];
    }

    void TogglePipeMuted(int pipe)
    {
        if(pipe < 0 || pipe >= kNumPipes)
            return;
        SetPipeMuted(pipe, !pipe_muted_[pipe]);
    }

    /** Set a pipe's Character position (0..32). The DSP shaper uses
     *  twice this position internally; ramped ~10 ms, click-free. */
    void SetPipeCharacterStep(int pipe, int step)
    {
        if(pipe < 0 || pipe >= kNumPipes)
            return;
        step = ParameterSteps::Clamp(step);
        character_step_[pipe] = static_cast<uint8_t>(step);
        character_.SetStep(pipe, ParameterSteps::CharacterDspStep(step));
    }

    int GetPipeCharacterStep(int pipe) const
    {
        return (pipe < 0 || pipe >= kNumPipes) ? 0 : character_step_[pipe];
    }

    /** Modulation Mode: set a pipe's volume-modulation amount
 *      (0..kMaxModSteps, clamped). 100% swings the level inside a
 *      +/-20% window around its center; 0 is the exact unmodulated
 *      center. The modulator keeps free-running; changing the amount
 *      never restarts it. Boots 0; saved in presets; MIDI CCs 94-97. */
    void SetPipeVolModStep(int pipe, int step)
    {
        if(pipe < 0 || pipe >= kNumPipes)
            return;
        step = step < 0 ? 0 : (step > kMaxModSteps ? kMaxModSteps : step);
        vol_mod_step_[pipe] = static_cast<uint8_t>(step);
    }

    int GetPipeVolModStep(int pipe) const
    {
        return (pipe < 0 || pipe >= kNumPipes) ? 0 : vol_mod_step_[pipe];
    }

    /** Modulation Mode: set a pipe's Character-modulation amount
 *      (0..kMaxModSteps, clamped). Same window semantics as the volume
 *      modulation, applied to the Character center before the
 *      smoothstep easing. Boots 0; saved in presets; MIDI CCs 98-101. */
    void SetPipeCharModStep(int pipe, int step)
    {
        if(pipe < 0 || pipe >= kNumPipes)
            return;
        step = step < 0 ? 0 : (step > kMaxModSteps ? kMaxModSteps : step);
        char_mod_step_[pipe] = static_cast<uint8_t>(step);
    }

    int GetPipeCharModStep(int pipe) const
    {
        return (pipe < 0 || pipe >= kNumPipes) ? 0 : char_mod_step_[pipe];
    }

    /** Modulation Mode: set a pipe's pipe-length modulation amount
 *      (0..kMaxModSteps, clamped). 100% drifts the pipe's pitch inside a
 *      symmetric +/-kLengthModMaxCents window around its set length
 *      (linear in the step); 0 is the exact unmodulated pitch
 *      (multiplier exactly 1). The modulator keeps free-running;
 *      changing the amount never restarts it. Boots 0; saved in presets;
 *      MIDI CCs 102-105. */
    void SetPipeLengthModStep(int pipe, int step)
    {
        if(pipe < 0 || pipe >= kNumPipes)
            return;
        step = step < 0 ? 0 : (step > kMaxModSteps ? kMaxModSteps : step);
        length_mod_step_[pipe] = static_cast<uint8_t>(step);
    }

    int GetPipeLengthModStep(int pipe) const
    {
        return (pipe < 0 || pipe >= kNumPipes) ? 0 : length_mod_step_[pipe];
    }

    /** Apply one encoder click to a pipe's attack time (0..kMaxEnvSteps,
 *      log-spaced 5 ms..10 s, EnvelopeMap.h). Step 0 is the original
 *      fixed 5 ms gate - boot default, no behavior change. CCs 78-85
 *      address attack/release; changes apply to sounding voices next sample. */
    void SetPipeAttackStep(int pipe, int step)
    {
        if(pipe < 0 || pipe >= kNumPipes)
            return;
        step = step < 0 ? 0 : (step > kMaxEnvSteps ? kMaxEnvSteps : step);
        pipe_attack_step_[pipe] = static_cast<uint8_t>(step);
        attack_inc_[pipe]       = EnvelopeMap::StepToInc(step, sample_rate_);
    }

    int GetPipeAttackStep(int pipe) const
    {
        return pipe_attack_step_[pipe];
    }

    /** Apply one encoder click to a pipe's release time (0..kMaxEnvSteps,
 *      same log-spaced 5 ms..10 s mapping as the attack). The 5 ms floor
 *      keeps key release click-free (a true 0 ms cut would pop). */
    void SetPipeReleaseStep(int pipe, int step)
    {
        if(pipe < 0 || pipe >= kNumPipes)
            return;
        step = step < 0 ? 0 : (step > kMaxEnvSteps ? kMaxEnvSteps : step);
        pipe_release_step_[pipe] = static_cast<uint8_t>(step);
        release_inc_[pipe]       = EnvelopeMap::StepToInc(step, sample_rate_);
    }

    int GetPipeReleaseStep(int pipe) const
    {
        return pipe_release_step_[pipe];
    }

    /** Set a pipe's retained length position (0..32), snapped to an
     *  octave offset of -2..+2 from nominal footage. The new ratio folds
 *      into the stored base frequencies via freqs_dirty_ without an
 *      oscillator phase reset, so sounding notes move to the new pitch
 *      click-free (same mechanism as fine tune); the 20 kHz foldback
 *      applies after the offset, like a real organ. The percussion taps
 *      the 2 2/3' pipe's raw oscillator, so it follows that pipe's
 *      length. Boots at position 16; saved in presets and addressed by
 *      its per-pipe MIDI CC. */
    void SetPipeLengthStep(int pipe, int step)
    {
        if(pipe < 0 || pipe >= kNumPipes)
            return;
        pipe_length_step_[pipe] = static_cast<uint8_t>(
            ParameterSteps::Clamp(step));
        freqs_dirty_           = true;
    }

    void SetPipeLengthOctave(int pipe, int oct)
    {
        oct = oct < -kMaxPipeLengthOctave
                  ? -kMaxPipeLengthOctave
                  : (oct > kMaxPipeLengthOctave ? kMaxPipeLengthOctave : oct);
        SetPipeLengthStep(pipe, (oct + 2) * 8);
    }

    int GetPipeLengthStep(int pipe) const
    {
        return (pipe < 0 || pipe >= kNumPipes) ? 0 : pipe_length_step_[pipe];
    }

    int GetPipeLengthOctave(int pipe) const
    {
        return (pipe < 0 || pipe >= kNumPipes)
               ? 0
               : ParameterSteps::PipeLengthOctave(pipe_length_step_[pipe]);
    }

    /** Apply one encoder click to the overdrive amount (0..kMaxSteps).
 *      Step 0 is an exact bypass; higher steps increase the pre-gain. */
    void SetDriveStep(int step)
    {
        step = step < 0 ? 0 : (step > kMaxSteps ? kMaxSteps : step);
        drive_step_   = static_cast<uint8_t>(step);
        drive_target_ = static_cast<float>(step) * (1.f / kMaxSteps);
    }

    int GetDriveStep() const { return drive_step_; }

    /** Modulation Mode: set the GLOBAL distortion modulation amount
 *      (0..kMaxModSteps, clamped). The drive is shared by all four
 *      pipes, so there is one amount, not one per pipe. 100% swings
 *      the drive inside a +/-20% full-scale window around its set
 *      level (the window slides to stay inside [0,1], so a 0 drive
 *      drifts in and out of audibility); 0 is the exact unmodulated
 *      center. The modulator keeps free-running; changing the amount
 *      never restarts it. Boots 0; saved in the SD-card presets as
 *      "drive_mod"; MIDI CC 115 (receive-only). */
    void SetDriveModStep(int step)
    {
        step = step < 0 ? 0 : (step > kMaxModSteps ? kMaxModSteps : step);
        drive_mod_step_ = static_cast<uint8_t>(step);
    }

    int GetDriveModStep() const { return drive_mod_step_; }

    /** Set the distortion tone (0..100 percent, clamped, no wrap):
 *      0 = 500 Hz low-pass per stage (dark), 100 = 10 kHz per stage
 *      (boot default; the two-stage cascade makes this intentionally
 *      darker than the historical single-pole Fuzz). Never changes the
 *      amount, tuning or note state; the filter coefficient ramps
 *      ~10 ms, click-free. Saved in the SD-card presets. */
    void SetDriveTonePercent(int percent)
    {
        SetDriveToneStep(ParameterSteps::LegacyPosition(percent, 0, 100));
    }

    void SetDriveToneStep(int step)
    {
        drive_tone_step_ = static_cast<uint8_t>(ParameterSteps::Clamp(step));
        distortion_.SetTonePercent(
            ParameterSteps::DriveTonePercent(drive_tone_step_));
    }

    int GetDriveToneStep() const { return drive_tone_step_; }

    int GetDriveTonePercent() const
    {
        return ParameterSteps::DriveTonePercent(drive_tone_step_);
    }

    /** Apply one encoder click to the rotary-speaker amount (0..kMaxSteps).
 *      Step 0 is an exact bypass; higher steps deepen the modulation and
 *      spin the virtual rotors faster (DSP-6). */
    void SetRotaryStep(int step)
    {
        step = step < 0 ? 0 : (step > kMaxSteps ? kMaxSteps : step);
        rotary_step_ = static_cast<uint8_t>(step);
        rotary_.SetAmount(static_cast<float>(step) * (1.f / kMaxSteps));
    }

    int GetRotaryStep() const { return rotary_step_; }

     /** Set vibrato depth position (0..32) across 0..50 cents peak. The
 *      shared 5 Hz LFO keeps running
 *      regardless; depth 0 is an exact bypass. On reaching 0 the
 *      sounding voices are snapped back to their exact base frequencies
 *      once, so the bypass is bit-exact rather than "1.0 within float
 *      rounding". Boots at 0; saved in the SD-card presets. */
    void SetVibratoDepthStep(int step)
    {
        step = ParameterSteps::Clamp(step);
        const bool was_on = vibrato_depth_step_ > 0;
        vibrato_depth_step_ = static_cast<uint8_t>(step);
        vibrato_.SetDepth(ParameterSteps::VibratoDepthCents(
            vibrato_depth_step_));
        if(was_on && vibrato_depth_step_ == 0)
        {
            for(int v = 0; v < kNumVoices; v++)
            {
                if(voices_[v].state == IDLE)
                    continue;
                for(int p = 0; p < kNumPipes; p++)
                    voices_[v].osc[p].SetFreq(base_freq_[v][p]);
            }
        }
    }

    int GetVibratoDepthStep() const { return vibrato_depth_step_; }

    void SetVibratoDepth(int cents)
    {
        SetVibratoDepthStep(ParameterSteps::LegacyPosition(
            cents, 0, kMaxVibratoDepthCents));
    }

    int GetVibratoDepth() const
    {
        return ParameterSteps::VibratoDepthCents(vibrato_depth_step_);
    }

     /** Set vibrato note-on delay position (0..32 across 0..5 s,
 *      clamped, no wrap). Step 0 (boot, init
 *      patch) disables the envelope bit-exactly; any set step holds the
 *      vibrato at exact dry after a note-on from an all-notes-released
 *      state, then fades in over a fixed 250 ms ramp (Vibrato.h).
 *      Saved in presets and addressed by MIDI CC 106. */
    void SetVibratoDelayStep(int step)
    {
        step = ParameterSteps::Clamp(step);
        vibrato_delay_step_ = static_cast<uint8_t>(step);
        vibrato_.SetDelayMs(ParameterSteps::VibratoDelayMs(step));
    }

    int GetVibratoDelayStep() const { return vibrato_delay_step_; }

    /** Apply one encoder click to the plate-reverb mix (0..kMaxSteps,
 *      clamped). Step 0 is bit-exact dry, step 32 is wet only; the
 *      ~10 ms crossfade ramp retargets from the current mix,
 *      click-free. */
    void SetReverbMixStep(int step)
    {
        step = step < 0 ? 0 : (step > kMaxSteps ? kMaxSteps : step);
        reverb_mix_step_ = static_cast<uint8_t>(step);
        plate_.SetMix(static_cast<float>(step) * (1.f / kMaxSteps));
    }

    int GetReverbMixStep() const { return reverb_mix_step_; }

     /** Set plate-reverb decay position (0..32), mapped exponentially
 *      from 0.2 s (step 0, boot default) to 20 s (step 32). Log spacing
 *      keeps the short end usable - a linear seconds-per-click mapping
 *      would make the first clicks huge relative jumps. */
    void SetReverbDecayStep(int step)
    {
        step = ParameterSteps::Clamp(step);
        reverb_decay_step_ = static_cast<uint8_t>(step);
        plate_.SetDecaySeconds(ParameterSteps::ReverbDecaySeconds(step));
    }

    int GetReverbDecayStep() const { return reverb_decay_step_; }

    /** Set the stereo delay's combined mix/feedback position (0..32),
     *  mapped onto the existing 0..100 percent curve. 0..16 sweeps
     *  the dry/wet mix 0..50%: 0% is bit-exact dry; the ~10 ms
     *  crossfade ramp retargets from the current mix, click-free, and
     *  the delay lines keep running at dry mix (live tails). Above 50
     *  the mix locks at 50% and the feedback rises from the 35% floor
     *  to 120%; past ~100% the delay self-oscillates into a bounded
     *  feedback drone whose pitch follows the delay time.
     *  Stored in the SD-card presets as `delay_mix`. */
    void SetDelayMixStep(int step)
    {
        delay_mix_step_ = static_cast<uint8_t>(ParameterSteps::Clamp(step));
        ApplyDelayCombined(static_cast<float>(GetDelayMixPercent()));
    }

    int GetDelayMixStep() const { return delay_mix_step_; }

    void SetDelayMixPercent(int percent)
    {
        SetDelayMixStep(ParameterSteps::LegacyPosition(percent, 0, 100));
    }

    int GetDelayMixPercent() const
    {
        return ParameterSteps::DelayCombinedPercent(delay_mix_step_);
    }

    /** Modulation Mode: set the GLOBAL delay modulation amount
 *      (0..kMaxModSteps, clamped). The stereo delay's combined
 *      mix/feedback control is shared by the whole instrument, so
 *      there is one amount, not one per pipe. 100% swings the combined
 *      0..100 control inside a +/-20% full-scale window around its set
 *      value (the window slides to stay inside [0,100], so a dry
 *      setting drifts in and out of audibility and a high-feedback
 *      setting drifts in and out of self-oscillation); 0 is the exact
 *      unmodulated center. The modulator keeps free-running; changing
 *      the amount never restarts it. Boots 0; saved in the SD-card
 *      presets as "delay_mod"; MIDI CC 116 (receive-only). */
    void SetDelayModStep(int step)
    {
        step = step < 0 ? 0 : (step > kMaxModSteps ? kMaxModSteps : step);
        delay_mod_step_ = static_cast<uint8_t>(step);
    }

    int GetDelayModStep() const { return delay_mod_step_; }

    /** Set stereo delay time position (0..32 across 40..500 ms). The
     *  read position slews to the
     *  new time with a smooth BBD-like pitch glide; the echo history is
     *  never cleared. */
    void SetDelayTimeStep(int step)
    {
        delay_time_step_ = static_cast<uint8_t>(ParameterSteps::Clamp(step));
        delay_.SetDelayMs(static_cast<float>(GetDelayTimeMs()));
    }

    int GetDelayTimeStep() const { return delay_time_step_; }

    void SetDelayTimeMs(int ms)
    {
        SetDelayTimeStep(ParameterSteps::LegacyDelayPosition(ms));
    }

    int GetDelayTimeMs() const
    {
        return ParameterSteps::DelayTimeMs(delay_time_step_);
    }

    /** Toggle the Hammond-style percussion (Play/Pause key). Off is an
     *  exact bypass: no trigger bookkeeping effect on the sound, exact
     *  1/24 normalization, and the settled path is skipped entirely.
     *  Toggling never re-triggers the decay; enabling while notes are
     *  held waits for the next all-notes-released note-on. Saved in the
     *  SD-card presets; no MIDI CC. */
    void SetPercussionEnabled(bool on) { percussion_.SetEnabled(on); }

    bool GetPercussionEnabled() const { return percussion_.IsEnabled(); }

    void SetMasterStep(int step)
    {
        master_step_ = static_cast<uint8_t>(ParameterSteps::Clamp(step));
        master_gain_target_ = ParameterSteps::Normalized(master_step_);
    }

    int GetMasterStep() const { return master_step_; }

    void SetMasterGain(float g)
    {
        g = g < 0.f ? 0.f : (g > 1.f ? 1.f : g);
        SetMasterStep(static_cast<int>(g * kMaxSteps + .5f));
    }

    float GetMasterGain() const { return master_gain_target_; }

    /** SD-recording tap: when set, Process() writes each block's
     *  pre-master-gain stereo bus (post pipes/fuzz/rotary/delay/reverb)
     *  into these buffers at the fixed -6 dB record level
     *  (kRecordTapGain). The buffers must hold at least one audio block
     *  per channel; a nullptr disables the tap (exact zero cost). */
    void SetRecordTap(float *l, float *r)
    {
        rec_tap_l_ = l;
        rec_tap_r_ = r;
    }

    /** SD-playback injection: when set, Process() adds these
     *  deinterleaved float stereo buffers to the bus AFTER the
     *  record-tap write and BEFORE the master-gain multiply, so a
     *  take bypasses every FX stage, is never re-recorded and the
     *  volume knob still scales it. The buffers must hold at least
     *  one audio block per channel; a nullptr disables the injection
     *  (exact zero cost). */
    void SetPlaybackIn(const float *l, const float *r)
    {
        pb_in_l_ = l;
        pb_in_r_ = r;
    }

    void SetFineTuneStep(int step)
    {
        step = ParameterSteps::Clamp(step);
        if(step != fine_step_)
        {
            fine_step_ = static_cast<uint8_t>(step);
            fine_mult_ = exp2f(static_cast<float>(GetFineTuneCents())
                               * (1.f / 1200.f));
            freqs_dirty_ = true;
        }
    }

    void AddFineTuneStep(int delta)
    {
        SetFineTuneStep(ParameterSteps::Advance(fine_step_, delta));
    }

    int GetFineTuneStep() const { return fine_step_; }

    void AddFineTuneCents(int delta)
    {
        SetFineTuneStep(ParameterSteps::LegacyPosition(
            GetFineTuneCents() + delta, -50, 50));
    }

    int GetFineTuneCents() const
    {
        return ParameterSteps::FineTuneCents(fine_step_);
    }

    void NoteOn(uint8_t note, float velocity)
    {
        note &= 0x7f;
        /* Held-note bookkeeping for the percussion single-trigger:
         * logical held pitches (keys, latch, MIDI) count, release tails
         * and voice stealing do not; duplicate note-ons/offs never
         * double-count. A note-on from an all-notes-released state
         * while enabled restarts the shared decay - checked BEFORE
         * marking this note held. */
        if(!note_held_[note])
        {
            if(held_count_ == 0 && percussion_.IsEnabled())
                percussion_.Trigger();
            /* Vibrato note-on delay: single-trigger from an
             * all-notes-released state (the same rule as the percussion
             * rearm); a no-op while the delay step is 0. Legato
             * note-ons never restart the fade-in. */
            if(held_count_ == 0)
                vibrato_.Trigger();
            note_held_[note] = true;
            held_count_++;
        }

        /* Retrigger: reuse a voice already playing/releasing this note.
         * C1: a voice mid-STEAL whose pending note matches is also a
         * retrigger (it starts this note when its fade ends) - without
         * this, two voices could end up on the same note. */
        for(int v = 0; v < kNumVoices; v++)
        {
            Voice &voice = voices_[v];
            if((voice.state == ATTACK || voice.state == SUSTAIN
                || voice.state == RELEASE)
               && voice.note == note)
            {
                StartVoice(v, note, velocity);
                return;
            }
            if((voice.state == STEAL || voice.state == STEAL_START)
               && voice.pending_note == note)
            {
                voice.pending_vel = velocity;
                return;
            }
        }

        /* Free voice */
        for(int v = 0; v < kNumVoices; v++)
        {
            if(voices_[v].state == IDLE)
            {
                StartVoice(v, note, velocity);
                return;
            }
        }

        /* Steal the oldest voice (5 ms fade, then the new note starts).
         * C1: prefer voices not already mid-STEAL - stealing one would
         * silently drop its pending note (that key stays held but
         * sounds nothing). */
        int oldest = -1;
        for(int v = 0; v < kNumVoices; v++)
        {
            if(voices_[v].state != STEAL && voices_[v].state != STEAL_START
               && (oldest < 0 || voices_[v].age < voices_[oldest].age))
                oldest = v;
        }
        if(oldest < 0) /* all six mid-steal: fall back to oldest overall */
        {
            for(int v = 0; v < kNumVoices; v++)
            {
                if(oldest < 0 || voices_[v].age < voices_[oldest].age)
                    oldest = v;
            }
        }
        Voice &voice      = voices_[oldest];
        voice.pending_note = note;
        voice.pending_vel  = velocity;
        voice.state        = STEAL;
    }

    void NoteOff(uint8_t note)
    {
        note &= 0x7f;
        if(note_held_[note])
        {
            note_held_[note] = false;
            held_count_--;
        }

        for(int v = 0; v < kNumVoices; v++)
        {
            Voice &voice = voices_[v];
            if((voice.state == ATTACK || voice.state == SUSTAIN)
               && voice.note == note)
            {
                voice.state = RELEASE;
            }
            else if((voice.state == STEAL || voice.state == STEAL_START)
                    && voice.pending_note == note)
            {
                /* stolen voice's pending note was cancelled: just fade out */
                voice.state = RELEASE;
            }
        }
    }

    bool IsNoteHeld(uint8_t note) const { return note_held_[note & 0x7f]; }

    /** Called once per audio block, before Process(). */
    void Prepare(size_t num_samples)
    {
        /* Block-rate steal completion: voices whose 5 ms steal fade
         * finished during the previous block start their pending note
         * here, so the per-sample Process() loop never does note-event
         * work. <= 0.5 ms late - inaudible against the fade itself. */
        for(int v = 0; v < kNumVoices; v++)
        {
            if(voices_[v].state == STEAL_START)
                StartVoice(v, voices_[v].pending_note,
                           voices_[v].pending_vel);
        }

        /* B4: the distortion gain/normalization only depend on the
         * drive target, so compute them once per block instead of a
         * divide per sample; the live gains ramp to the block targets
         * over ~10 ms inside Distortion, so encoder ticks glide
         * instead of stepping. drive_ still slews per sample for
         * a click-free bypass. The global drive modulation advances
         * its free-running drift once per block and swings the target
         * inside its window first; amount 0 is the exact unmodulated
         * center, so the bypass path stays bit-exact. */
        drive_mod_.Advance(static_cast<int>(num_samples));
        drive_mod_target_
            = DriftModulator::ApplyWindow(drive_target_,
                                          static_cast<float>(drive_mod_step_)
                                              * (1.f / kMaxModSteps),
                                          drive_mod_.Value());
        distortion_.PrepareBlock(drive_mod_target_);

        /* the global delay modulation advances its free-running drift
         * once per block and swings the combined 0..100 mix/feedback
         * control inside its window (the same +/-20% full-scale window
         * as the drive, so a feedback setting can drift into bounded
         * self-oscillation); StereoBbdDelay's per-sample mix/feedback
         * ramps smooth the block-rate steps. Amount 0 restores the
         * exact user setting once and then leaves the delay untouched,
         * so the unmodulated path stays bit-exact. */
        delay_mod_.Advance(static_cast<int>(num_samples));
        if(delay_mod_step_ > 0)
        {
            delay_mod_active_ = true;
            ApplyDelayCombined(
                DriftModulator::ApplyWindow(
                    static_cast<float>(GetDelayMixPercent()) * .01f,
                    static_cast<float>(delay_mod_step_)
                        * (1.f / kMaxModSteps),
                    delay_mod_.Value())
                * 100.f);
        }
        else if(delay_mod_active_)
        {
            delay_mod_active_ = false;
            ApplyDelayCombined(static_cast<float>(GetDelayMixPercent()));
        }

        /* Modulation Mode drift: advance the twelve free-running
         * per-pipe
         * modulators once per block and re-derive the modulated pipe
         * gain targets, Character ramp targets and pipe-length pitch
         * multipliers from the user centers. The existing ~1 ms
         * per-sample gain slew and the Character ~10 ms ramp smooth the
         * block-rate steps; the pitch multiplier steps are inaudible
         * (worst case ~7e-6 relative per block) and SineLut never
         * resets the phase, so pitch glides click-free; amount 0
         * reproduces the unmodulated targets bit-exactly. */
        length_mod_active_ = false;
        for(int p = 0; p < kNumPipes; p++)
        {
            vol_mod_[p].Advance(static_cast<int>(num_samples));
            char_mod_[p].Advance(static_cast<int>(num_samples));
            length_mod_[p].Advance(static_cast<int>(num_samples));
            UpdatePipeGainTarget(p);
            const float a_center = static_cast<float>(character_step_[p])
                                   * (1.f / kMaxSteps);
            const float a_amount = static_cast<float>(char_mod_step_[p])
                                   * (1.f / kMaxModSteps);
            character_.SetModulatedTarget(
                p,
                DriftModulator::ApplyWindow(a_center, a_amount,
                                            char_mod_[p].Value()));
            const int lmod = length_mod_step_[p];
            if(lmod > 0)
            {
                length_mod_active_ = true;
                /* symmetric window: the raised-cosine drift u in [0,1]
                 * maps to +/-kLengthModMaxCents scaled by the amount */
                const float cents
                    = (length_mod_[p].Value() - .5f) * 2.f
                      * kLengthModMaxCents
                      * (static_cast<float>(lmod) * (1.f / kMaxModSteps));
                length_mod_mult_[p] = exp2f(cents * (1.f / 1200.f));
            }
            else
                length_mod_mult_[p] = 1.f; /* exact unmodulated pitch */
        }

        if(freqs_dirty_)
        {
            freqs_dirty_ = false;
            for(int v = 0; v < kNumVoices; v++)
            {
                if(voices_[v].state != IDLE && voices_[v].state != STEAL
                   && voices_[v].state != STEAL_START)
                    UpdateFreqs(v);
            }
        }
    }

    void Process(float **out, size_t size,
                 const float *aux_l = nullptr, const float *aux_r = nullptr)
    {
        for(size_t i = 0; i < size; i++)
        {
            /* Smooth pipe gains and master gain (~1 ms slew). Advance the
             * four Character amounts once per sample (not once per voice),
             * even while voices are idle, so the ramps always settle. */
            float ca[kNumPipes];
            character_.Advance();
            for(int p = 0; p < kNumPipes; p++)
            {
                daisysp::fonepole(pipe_gain_[p], pipe_gain_target_[p], .001f);
                ca[p] = character_.EasedAmount(p);
            }
            daisysp::fonepole(master_gain_, master_gain_target_, .001f);
            daisysp::fonepole(drive_, drive_mod_target_, .001f);

            /* Percussion: advance the SHARED decay envelope once per
             * sample (never per voice). PathActive() false means the
             * whole percussion path below is skipped bit-exactly. */
            const float perc_env  = percussion_.Process();
            const float perc_ramp = percussion_.EnableRamp();
            const bool  perc_on   = percussion_.PathActive();
            /* Mix normalization: the conservative sum bound is 6 voices
             * x 4 pipes = 24 units dry, 6 x (4 + 0.75 percussion) = 28.5
             * with percussion fully enabled. Interpolating
             * 1/(24 + 6*kPeakGain*r) with the enable ramp r keeps every
             * intermediate state inside its true bound (and tracks
             * kPeakGain automatically); at settled off (ramp exactly 0)
             * the original constant is used bit-exactly. ~1.5 dB of dry
             * attenuation while enabled. */
            float mix_scale = kMixScale;
            if(perc_ramp > 0.f)
                mix_scale = 1.f
                            / (24.f + 6.f * Percussion::kPeakGain * perc_ramp);

            /* Vibrato: advance the one shared, free-running 5 Hz LFO and
             * take this sample's global pitch multiplier (exactly 1.f at
             * depth 0, no transcendental work then). Applied to every
             * sounding voice as a multiplier on the STORED base
             * frequencies: the oscillator phase is never reset and the
             * frequency is recomputed from base each sample, so pitch
             * neither clicks nor drifts. The two transcendental calls
             * happen once per sample here, never in the oscillator
             * loop. */
            const int   vib_depth = vibrato_.GetDepth();
            const float vib_mult  = vibrato_.Process();

            float mix = 0.f;

            for(int v = 0; v < kNumVoices; v++)
            {
                Voice &voice = voices_[v];
                if(voice.state == IDLE)
                    continue;

                /* Vibrato and pipe-length drift share this per-sample
                 * retune: both multiply the stored base frequencies
                 * (SineLut SetFreq is phase-continuous). The gate keeps
                 * the fully-bypassed path bit-exact. */
                if(vib_depth > 0 || length_mod_active_)
                {
                    for(int p = 0; p < kNumPipes; p++)
                        voice.osc[p].SetFreq(base_freq_[v][p] * vib_mult
                                             * length_mod_mult_[p]);
                }

                float sig  = 0.f;
                float raw2 = 0.f; /* pipe 2's raw sine, for percussion */
                for(int p = 0; p < kNumPipes; p++)
                {
                    /* One sine per pipe. Character shapes the raw sine
                     * sample BEFORE pipe gain/envelope/velocity, so muted
                     * pipes and completed envelopes stay silent; settled
                     * Character zero returns the sine exactly. */
                    float psig = voice.osc[p].Process();
                    if(p == 2)
                        raw2 = psig; /* percussion taps the raw 2 2/3'
                                      * sine: before Character, pipe gain
                                      * and the pipe envelopes */
                    if(ca[p] > 0.f)
                        psig = TonewheelCharacter::ShapeSample(
                            psig, character_.Coeffs(v, p), ca[p]);
                    /* per-pipe gate envelope: each pipe fades in/out at
                     * its own attack/release rate */
                    sig += psig * pipe_gain_[p] * voice.pipe_env[p];
                }

                /* Percussion: pass the raw 2 2/3' tap through the
                 * per-voice click-prevention gate (1 ms up while
                 * sounding, 5 ms down on release/steal - independent of
                 * the pipe envelope times) and the shared decay. Idle or
                 * settled-off voices add exactly nothing; old release
                 * tails never gain a fresh attack (their gate is down). */
                if(perc_on)
                {
                    const bool sounding = voice.state == ATTACK
                                          || voice.state == SUSTAIN;
                    voice.perc_gate = percussion_.AdvanceGate(
                        voice.perc_gate, sounding);
                    if(perc_env > 0.f && voice.perc_gate > 0.f)
                        sig += raw2 * voice.perc_gate
                               * (Percussion::kPeakGain * perc_env
                                  * perc_ramp);
                }

                switch(voice.state)
                {
                    case ATTACK:
                    {
                        /* the voice sustains once ALL pipes are fully up
                         * (the slowest attack sets the transition) */
                        bool all_up = true;
                        for(int p = 0; p < kNumPipes; p++)
                        {
                            voice.pipe_env[p] += attack_inc_[p];
                            if(voice.pipe_env[p] >= 1.f)
                                voice.pipe_env[p] = 1.f;
                            else
                                all_up = false;
                        }
                        if(all_up)
                            voice.state = SUSTAIN;
                        break;
                    }
                    case SUSTAIN: break;
                    case RELEASE:
                    {
                        /* the voice frees once ALL pipes are fully out
                         * (the slowest release holds the voice) */
                        bool all_down = true;
                        for(int p = 0; p < kNumPipes; p++)
                        {
                            voice.pipe_env[p] -= release_inc_[p];
                            if(voice.pipe_env[p] <= 0.f)
                                voice.pipe_env[p] = 0.f;
                            else
                                all_down = false;
                        }
                        if(all_down)
                            voice.state = IDLE;
                        break;
                    }
                    case STEAL:
                    {
                        /* fixed 5 ms fade on all pipes; the new note then
                         * starts once per block from Prepare()
                         * (STEAL_START), never inside this per-sample
                         * loop */
                        bool all_down = true;
                        for(int p = 0; p < kNumPipes; p++)
                        {
                            voice.pipe_env[p] -= steal_inc_;
                            if(voice.pipe_env[p] <= 0.f)
                                voice.pipe_env[p] = 0.f;
                            else
                                all_down = false;
                        }
                        if(all_down)
                            voice.state = STEAL_START;
                        break;
                    }
                    default: break;
                }

                mix += sig * voice.velocity;
            }

            /* Distortion (DSP-5): fuzz saturation stage with a tone
             * control on the mix. Runs before the master gain so the
             * drive character does not depend on the listening level;
             * step 0 is an exact bypass at every tone. mix_scale is the
             * percussion-aware normalization above (exactly kMixScale
             * when percussion is settled off). */
            float out_sig = distortion_.Process(mix * mix_scale, drive_);
            /* Rotary speaker (DSP-6): mono mix -> stereo Leslie, placed
             * after the distortion (the drive is the preamp, the Leslie
             * is the speaker cabinet) and before the master gain so the
             * volume knob keeps scaling both channels equally. Step 0 is
             * an exact bypass (both channels = out_sig). */
            float out_l, out_r;
            rotary_.Process(out_sig, out_l, out_r);
            /* Input-referenced FUZZ output leveling: servo-matches the
             * fuzzed level down to just above the measured dry level
             * (+1.5 dB headroom; per-block RMS, ~50 ms smoothing,
             * clamped <= 1, floored at -30 dB),
             * so the fuzzed sound keeps the dry sound's power at any
             * registration or polyphony - dry signal and reverb send
             * alike, no user control. Folded through the live wet
             * amount, it ramps click-free with the distortion's own
             * ~10 ms wet/bypass crossfade and is exactly unity at
             * settled bypass or zero drive. */
            const float fuzz_out_gain = distortion_.GetOutputGain();
            out_l *= fuzz_out_gain;
            out_r *= fuzz_out_gain;
            /* Stereo BBD-style delay: between the leveled rotary output
             * and the plate reverb, so echoes feed the reverb and the
             * master gain scales them. The buffers keep running at dry
             * mix; settled 0% is bit-exact dry. */
            delay_.Process(out_l, out_r, out_l, out_r);
            /* Plate reverb: stereo in/out between the rotary (speaker
             * cabinet) and the master gain, so the volume knob scales
             * dry and wet equally and 0% master mutes the tail. The tank
             * runs even with idle voices; 0% mix is bit-exact dry. */
            plate_.Process(out_l, out_r, out_l, out_r);
            if(aux_l != nullptr && aux_r != nullptr)
            {
                out_l += aux_l[i];
                out_r += aux_r[i];
            }
            /* record tap: the pre-master-gain bus at the fixed -6 dB
             * recording level - the volume knob never touches a take */
            if(rec_tap_l_ != nullptr)
            {
                rec_tap_l_[i] = out_l * kRecordTapGain;
                rec_tap_r_[i] = out_r * kRecordTapGain;
            }
            /* SD playback: injected after the record-tap write (a
             * take is never re-recorded, whatever the recorder gate)
             * and before the master gain (the volume knob scales it);
             * bypasses every FX stage */
            if(pb_in_l_ != nullptr)
            {
                out_l += pb_in_l_[i];
                out_r += pb_in_r_[i];
            }
            out_l *= master_gain_;
            out_r *= master_gain_;
            out[0][i] = out_l; /* headphone L */
            out[1][i] = out_r; /* headphone R */
            out[2][i] = out_l; /* master L */
            out[3][i] = out_r; /* master R */
        }
    }

  private:
    enum VoiceState : uint8_t
    {
        IDLE,
        ATTACK,
        SUSTAIN,
        RELEASE,
        STEAL,
        STEAL_START /* fade done; StartVoice runs in Prepare() */
    };

    struct Voice
    {
        SineLut  osc[kNumPipes]; /* one sine per pipe */
        VoiceState          state;
        uint8_t             note;
        uint8_t             pending_note;
        float               pending_vel;
        float               velocity;
        float               pipe_env[kNumPipes]; /* per-pipe gate envelope */
        float               perc_gate; /* percussion click-prevention gate */
        uint32_t            age;
    };

    static float StepToGain(int step)
    {
        return ParameterSteps::PipeLevelGain(step);
    }

    /** Derive one pipe's gain target from its level-step center, its
     *  volume-modulation amount and the modulator's current value;
     *  mute forces silence. Amount 0 is bit-exact StepToGain(step):
     *  ApplyWindow(x, 0, u) returns x unchanged. Called per block from
     *  Prepare() and immediately from the level/mute setters. */
    void UpdatePipeGainTarget(int pipe)
    {
        const float x  = ParameterSteps::Normalized(pipe_steps[pipe]);
        const float am = static_cast<float>(vol_mod_step_[pipe])
                         * (1.f / kMaxModSteps);
        const float xm = DriftModulator::ApplyWindow(x, am,
                                                     vol_mod_[pipe].Value());
          pipe_gain_target_[pipe] = pipe_muted_[pipe] ? 0.f : xm * xm;
    }

    void UpdateFreqs(int v)
    {
        Voice      &voice = voices_[v];
        const float base  = note_hz_[voice.note] * fine_mult_;
        for(int p = 0; p < kNumPipes; p++)
        {
            float f = base * PipeRatio(p)
                       * PipeLengthMult(ParameterSteps::PipeLengthOctave(
                           pipe_length_step_[p]));
            while(f > kFoldbackHz)
                f *= .5f; /* organ foldback: drop whole octaves */
            base_freq_[v][p] = f; /* vibrato multiplies this per sample */
            voice.osc[p].SetFreq(f);
            /* Character voicing + anti-alias masks track the FINAL folded
             * frequency (and fine tuning); identical for every voice slot
             * playing this note/pipe. */
            character_.UpdateVoiceCoeffs(v, voice.note, p, f);
        }
    }

    void StartVoice(int v, uint8_t note, float velocity)
    {
        /* pipe_env is deliberately NOT reset: IDLE voices and completed
         * STEAL fades are already at 0 on every pipe, and a same-note
         * retrigger should attack from its current level so a long
         * release tail is not cut. */
        Voice &voice   = voices_[v];
        voice.note     = note;
        voice.velocity = velocity;
        voice.age      = ++age_counter_;
        voice.state    = ATTACK;
        UpdateFreqs(v);
    }

    static constexpr float kDefaultMasterGain = .5f; /* 50% at boot */

    /** Push the combined 0..100 delay mix/feedback control into the
     *  delay: 0..50 sweeps the dry/wet mix 0..50% at the feedback
     *  floor; above 50 the mix locks at 50% and the feedback rises
     *  from the floor to kDelayFeedbackMax. Shared by the user setter
     *  (integer percent) and the global delay modulation (block-rate
     *  float percent); StereoBbdDelay ramps every retarget click-free. */
    void ApplyDelayCombined(float percent)
    {
        percent = percent < 0.f ? 0.f : (percent > 100.f ? 100.f : percent);
        if(percent <= static_cast<float>(kDelayMixPivotPercent))
        {
            delay_.SetMix(percent * (kDelayMixAtPivot / kDelayMixPivotPercent));
            delay_.SetFeedback(kDelayFeedbackFloor);
        }
        else
        {
            delay_.SetMix(kDelayMixAtPivot);
            delay_.SetFeedback(kDelayFeedbackFloor
                               + (percent - kDelayMixPivotPercent) * .02f
                                     * (kDelayFeedbackMax
                                        - kDelayFeedbackFloor));
        }
    }

    float    sample_rate_;
    float    steal_inc_;
    Voice    voices_[kNumVoices];
    uint32_t age_counter_;
    uint8_t  fine_step_;
    float    fine_mult_;     /* 2^(fine tune cents/1200), folded into the
                              * note table by UpdateFreqs */
    float    note_hz_[128];  /* A440 equal temperament, filled in Init() */
    bool     freqs_dirty_;
    float    pipe_gain_[kNumPipes];
    float    pipe_gain_target_[kNumPipes];
    bool     pipe_muted_[kNumPipes]; /* boots unmuted; saved in presets */
    TonewheelCharacter character_;
    uint8_t  character_step_[kNumPipes];
    uint8_t  vol_mod_step_[kNumPipes];  /* boot 0; saved in presets */
    uint8_t  char_mod_step_[kNumPipes]; /* boot 0; saved in presets */
    uint8_t  length_mod_step_[kNumPipes]; /* boot 0; saved in presets */
    DriftModulator vol_mod_[kNumPipes];  /* free-running per-pipe drift */
    DriftModulator char_mod_[kNumPipes];
    DriftModulator length_mod_[kNumPipes];
    float    length_mod_mult_[kNumPipes]; /* block-rate pitch multiplier,
                                           * exactly 1 while amount is 0 */
    bool     length_mod_active_; /* any length-mod amount > 0 (Prepare) */
    float    master_gain_;
    float    master_gain_target_;
    uint8_t  master_step_;
    float   *rec_tap_l_ = nullptr; /* pre-gain record tap, -6 dB */
    float   *rec_tap_r_ = nullptr;
    const float *pb_in_l_ = nullptr; /* SD playback: post-tap,
                                      * pre-master-gain injection */
    const float *pb_in_r_ = nullptr;
    float    drive_;
    float    drive_target_;
    float    drive_mod_target_; /* drive_target_ swung by the drive
                                 * drift window; drive_ slews to this
                                 * per sample (Prepare, block rate) */
    Distortion distortion_;
    uint8_t  drive_step_;
    uint8_t  drive_mod_step_;     /* boot 0; saved in presets as the
                                   * global "drive_mod" field */
    DriftModulator drive_mod_;    /* free-running global drive drift */
    RotarySpeaker rotary_;
    uint8_t  rotary_step_;
    uint8_t  drive_tone_step_;
    uint8_t  pipe_attack_step_[kNumPipes];
    uint8_t  pipe_release_step_[kNumPipes];
    uint8_t  pipe_length_step_[kNumPipes]; /* 0..32; saved in presets */
    float    attack_inc_[kNumPipes];
    float    release_inc_[kNumPipes];
    PlateReverb plate_;
    uint8_t  reverb_mix_step_;
    uint8_t  reverb_decay_step_;
    StereoBbdDelay delay_;
    uint8_t  delay_mix_step_;
    uint8_t  delay_time_step_;
    uint8_t  delay_mod_step_;     /* boot 0; saved in presets as the
                                   * global "delay_mod" field */
    DriftModulator delay_mod_;    /* free-running global delay drift */
    bool     delay_mod_active_;   /* the block-rate modulation is
                                   * applying (restore-on-zero gate) */
    Vibrato  vibrato_;
    uint8_t  vibrato_depth_step_;
    uint8_t  vibrato_delay_step_; /* 0..50 x 100 ms; saved in presets */
    float    base_freq_[kNumVoices][kNumPipes]; /* pre-vibrato freqs */
    bool     note_held_[128];
    int      held_count_; /* logical held notes, for percussion rearm */
    Percussion percussion_;

  public:
    uint8_t pipe_steps[kNumPipes];
};

} // namespace chompi
