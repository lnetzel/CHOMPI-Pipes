/** @file chompi_main.cpp
 *  @brief CHOMPI-PIPES firmware entry point.
 *
 *  Runs the spec in firmware/chompi-pipes/chompi-pipes- specification.md.
 *  Three execution contexts, like the other CHOMPI firmwares:
 *   1. AudioCallback() - audio ISR: MIDI in, control polling, audio render.
 *   2. MainLoop()      - MIDI out, LEDs, battery checks.
 *   3. Boot            - control-surface settle before audio starts.
 */
#include "hardware.h"
#include "temp_led_stuff.h"
#include "daisysp.h"
#include "OrganEngine.h"
#include "MidiHandler.h"
#include "OrganPreset.h"
#include "PresetStorage.h"
#include "SdRecorder.h"
#include "SdPlayer.h"
#include "ModDest.h"
#include "AudioRouting.h"
#ifdef PIPES_CPU_METER
#include "util/CpuLoadMeter.h"
#endif

using namespace daisy;
using namespace chompi;

Hardware    hw;
OrganEngine engine;
OrganMidi   midi;

/* Plate-reverb delay memory: 128 KiB of floats in external SDRAM, kept
 * out of the tight SRAM regions. Never written from a global
 * constructor: it is cleared by PlateReverb::Init via engine.Init(),
 * after hw.Init() has configured the SDRAM controller. */
static PlateReverb::T DSY_SDRAM_BSS
    plate_delay_mem[PlateReverb::kDelayBufferSize];

/* Stereo BBD-style delay memory: 2 x 128 KiB floats in external SDRAM,
 * same ownership/init pattern as the plate buffer above: cleared by
 * StereoBbdDelay::Init via engine.Init(), after hw.Init() has
 * configured the SDRAM controller. */
static float DSY_SDRAM_BSS delay_mem_l[StereoBbdDelay::kBufferSize];
static float DSY_SDRAM_BSS delay_mem_r[StereoBbdDelay::kBufferSize];

/* Record-to-SD ring: 2 x 262,144 int16 (1 MiB, ~5.5 s of card-stall
 * slack at 48 kHz stereo) in external SDRAM, same ownership pattern as
 * the delay buffers above. */
static int16_t DSY_SDRAM_BSS rec_ring[2 * 262144];

/* SD-playback ring: another 2 x 262,144 int16 (1 MiB, ~5.5 s) in
 * external SDRAM - the player's main-loop Service fills it from the
 * card and the audio ISR drains it (the gapless take advance rides on
 * the same slack). */
static int16_t DSY_SDRAM_BSS play_ring[2 * 262144];

/* Record tap buffers: one audio block of the pre-master-gain stereo
 * bus at the fixed -6 dB recording level, written by the engine and
 * drained by sd_recorder_ (block size 24, hardware.h). */
static float rec_tap_l[24];
static float rec_tap_r[24];

/* Playback mix buffers: one audio block of the decoded take,
 * deinterleaved; written by sd_player_.PopBlock and injected by the
 * engine after the record tap, before the master gain. */
static float play_mix_l[24];
static float play_mix_r[24];

/* B4: debug-build CPU meter (build with PIPES_CPU_METER=1); peak load
 * shows on PTH LED 0 (green < 70%, amber 70-85%, red > 85%). */
#ifdef PIPES_CPU_METER
static daisy::CpuLoadMeter cpu_meter;
#endif

/** SwId index -> MIDI note, copied from chompi-wave's NormalPage.h.
 *  The 25 playable keys are SwId 7..31 (KEY_16, KEY_2..KEY_5, KEY_17..KEY_19,
 *  KEY_1, KEY_6..KEY_10, KEY_20..KEY_22, KEY_11..KEY_15, KEY_23..KEY_25). */
static const uint8_t key_map[40] = {
    0x01, /**< ENC_1_SW */
    0x02, /**< ENC_2_SW */
    0x03, /**< ENC_3_SW */
    0x00, /**< ENC_4_SW */
    0x04, /**< ENC_5_SW */
    0x15, /**< KEY_26 chompi (shift) */
    0x00, /**< SW_TOG skip */
    0x31, /**< KEY_16 */
    0x32, /**< KEY_2 */
    0x34, /**< KEY_3 */
    0x35, /**< KEY_4 */
    0x37, /**< KEY_5 */
    0x33, /**< KEY_17 */
    0x36, /**< KEY_18 */
    0x38, /**< KEY_19 */
    0x30, /**< KEY_1 (C3, lowest) */
    0x39, /**< KEY_6 */
    0x3b, /**< KEY_7 */
    0x3c, /**< KEY_8 */
    0x3e, /**< KEY_9 */
    0x40, /**< KEY_10 */
    0x3a, /**< KEY_20 */
    0x3d, /**< KEY_21 */
    0x3f, /**< KEY_22 */
    0x41, /**< KEY_11 */
    0x43, /**< KEY_12 */
    0x45, /**< KEY_13 */
    0x47, /**< KEY_14 */
    0x48, /**< KEY_15 */
    0x42, /**< KEY_23 */
    0x44, /**< KEY_24 */
    0x46, /**< KEY_25 */
    0x05, /**< ENC_6_SW */
    0x17, /**< KEY_27 play */
    0x18, /**< KEY_28 loop */
    0x00, /**< NC_1 skip */
    0x00, /**< NC_2 skip */
    0x00, /**< NC_3 skip */
    0x00, /**< NC_4 skip */
    0x00, /**< NC_5 skip */
};

/** SwId index -> keybed (SMT) LED index, copied from chompi-wave's NormalPage.h */
static const uint8_t led_map[40] = {
    2,    /**< ENC_1_SW */
    3,    /**< ENC_2_SW */
    4,    /**< ENC_3_SW */
    1,    /**< ENC_4_SW */
    0,    /**< ENC_5_SW */
    0,    /**< KEY_26 chompi */
    0x00, /**< SW_TOG skip */
    0,    /**< KEY_16 */
    23,   /**< KEY_2 */
    22,   /**< KEY_3 */
    21,   /**< KEY_4 */
    20,   /**< KEY_5 */
    1,    /**< KEY_17 */
    2,    /**< KEY_18 */
    3,    /**< KEY_19 */
    24,   /**< KEY_1 */
    19,   /**< KEY_6 */
    18,   /**< KEY_7 */
    17,   /**< KEY_8 */
    16,   /**< KEY_9 */
    15,   /**< KEY_10 */
    4,    /**< KEY_20 */
    5,    /**< KEY_21 */
    6,    /**< KEY_22 */
    14,   /**< KEY_11 */
    13,   /**< KEY_12 */
    12,   /**< KEY_13 */
    11,   /**< KEY_14 */
    10,   /**< KEY_15 */
    7,    /**< KEY_23 */
    8,    /**< KEY_24 */
    9,    /**< KEY_25 */
    9,    /**< ENC_6_SW */
    7,    /**< KEY_27 play */
    8,    /**< KEY_28 loop */
    0x00, /**< NC_1 skip */
    0x00, /**< NC_2 skip */
    0x00, /**< NC_3 skip */
    0x00, /**< NC_4 skip */
    0x00, /**< NC_5 skip */
};

/** Pipe registration -> hardware encoder index.
 *  Physical knobs left-to-right are enc[3], enc[0], enc[1], enc[2]
 *  (PCB net names ENC_4, ENC_1, ENC_2, ENC_3); enc[4] is the transport
 *  wheel (ENC_5, rotary-speaker amount) and enc[5] (ENC_6) is master
 *  volume. */
static const int pipe_encoder[OrganEngine::kNumPipes] = {3, 0, 1, 2};
static const int kVolEnc = 5; /* ENC_6 */

/* the Modulation-mode destination keys are the first five physical
 * black keys from the left plus the eighth and ninth (ModDest.h keeps
 * the values so the native tests can exercise the mapping; break the
 * build, not the mapping, if the shift-register order ever changes) */
static_assert(kModDestKeySw[0] == static_cast<int>(Hardware::SwId::KEY_16),
              "mod destination key 1 is KEY_16 (C#3)");
static_assert(kModDestKeySw[1] == static_cast<int>(Hardware::SwId::KEY_17),
              "mod destination key 2 is KEY_17 (D#3)");
static_assert(kModDestKeySw[2] == static_cast<int>(Hardware::SwId::KEY_18),
              "mod destination key 3 is KEY_18 (F#3)");
static_assert(kModDestKeySw[3] == static_cast<int>(Hardware::SwId::KEY_19),
              "mod destination key 4 is KEY_19 (G#3)");
static_assert(kModDestKeySw[4] == static_cast<int>(Hardware::SwId::KEY_20),
              "mod destination key 5 is KEY_20 (A#3)");
static_assert(kModDestKeySw[5] == static_cast<int>(Hardware::SwId::KEY_23),
              "mod destination key 8 is KEY_23 (F#4)");
static_assert(kModDestKeySw[6] == static_cast<int>(Hardware::SwId::KEY_24),
              "mod destination key 9 is KEY_24 (G#4)");

/* the audio-routing selection keys are the sixth and seventh physical
 * black keys from the left (AudioRouting.h keeps the values so the
 * native tests can exercise the mapping; same break-the-build rule) */
static_assert(kRoutingKeySw[0] == static_cast<int>(Hardware::SwId::KEY_21),
              "routing key: Normal is KEY_21 (C#4, black key 6)");
static_assert(kRoutingKeySw[1] == static_cast<int>(Hardware::SwId::KEY_22),
              "routing key: Send FX is KEY_22 (D#4, black key 7)");

/** SMT LED indices of the 15 white keys, left (C3) to right (C5). */
static constexpr int     kNumWhiteKeys = 15;
static const uint8_t white_key_led[kNumWhiteKeys] = {
    24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10
};

/** Key-bar color modes. */
enum BarKind
{
    kBarVolume,   /**< green/orange/red position gradient */
    kBarCharacter, /**< uniformly blue: pipe Character bar */
    kBarDrive,    /**< uniformly red: overdrive bar */
    kBarRotary,   /**< uniformly purple: rotary-speaker bar */
    kBarReverb,   /**< uniformly blue: plate-reverb mix bar */
    kBarVibrato,  /**< uniformly pink: vibrato-depth bar */
    kBarAttack,   /**< uniformly purple: pipe attack-time bar */
    kBarRelease,  /**< uniformly yellow: pipe release-time bar */
    kBarModVolume,   /**< uniformly white: pipe volume-modulation bar */
    kBarModCharacter, /**< uniformly blue: pipe Character-modulation bar */
    kBarModLength,   /**< uniformly yellow: pipe-length modulation bar */
    kBarFineTune,     /**< center-out red/green: fine-tune offset bar */
    kBarDelay,        /**< uniformly yellow: stereo-delay mix/time bars */
    kBarLength        /**< 3-key yellow octave group: pipe-length bar */
};

/** Key-bar color by white-key position w (0 = leftmost): volume bars
 *  (page-1 pipes + master volume) run green for the first 50% of keys,
 *  orange 50-75%, red above 75%. The page-2 pipe Character bar
 *  (kBarCharacter) is uniformly blue; the page-3 pipe attack bar
 *  (kBarAttack) is uniformly purple and the page-4 pipe release bar
 *  (kBarRelease) is uniformly yellow; the overdrive bar (kBarDrive) is
 *  uniformly red; the rotary bar (kBarRotary) is uniformly purple; the
 *  plate-reverb mix bar (kBarReverb) is uniformly blue; the vibrato bar
 *  (kBarVibrato) is uniformly pink. */
static void BarKeyColor(int w, BarKind kind, float &r, float &g, float &b)
{
    if(kind == kBarAttack)
    {
        r = .55f;
        g = 0.f;
        b = 1.f; /* purple: the whole attack-time bar */
        return;
    }
    if(kind == kBarRelease)
    {
        r = 1.f;
        g = 1.f;
        b = 0.f; /* yellow: the whole release-time bar */
        return;
    }
    if(kind == kBarVibrato)
    {
        r = 1.f;
        g = 0.f;
        b = .5f; /* pink: the whole vibrato-depth bar */
        return;
    }
    if(kind == kBarCharacter)
    {
        r = 0.f;
        g = 0.f;
        b = 1.f; /* blue: the whole Character bar */
        return;
    }
    if(kind == kBarReverb)
    {
        r = 0.f;
        g = 0.f;
        b = 1.f; /* blue: the whole plate-reverb mix bar */
        return;
    }
    if(kind == kBarDrive)
    {
        r = 1.f;
        g = 0.f;
        b = 0.f; /* red: the whole overdrive bar, no gradient */
        return;
    }
    if(kind == kBarRotary)
    {
        r = .55f;
        g = 0.f;
        b = 1.f; /* purple: the whole rotary bar */
        return;
    }
    if(kind == kBarModVolume)
    {
        r = 1.f;
        g = 1.f;
        b = 1.f; /* white: the whole volume-modulation bar */
        return;
    }
    if(kind == kBarModCharacter)
    {
        r = 0.f;
        g = 0.f;
        b = 1.f; /* blue: the whole Character-modulation bar */
        return;
    }
    if(kind == kBarModLength)
    {
        r = 1.f;
        g = 1.f;
        b = 0.f; /* yellow: the whole pipe-length modulation bar */
        return;
    }
    if(kind == kBarDelay)
    {
        r = 1.f;
        g = 1.f;
        b = 0.f; /* yellow: the whole delay mix/time bar */
        return;
    }
    r = 0.f;
    g = 1.f;
    b = 0.f; /* green: first 50% of keys */
    if(w >= 11) /* keys 11-14: above 75% */
    {
        r = 1.f;
        g = 0.f; /* red */
    }
    else if(w >= 7) /* keys 7-10: 50-75% */
    {
        r = 1.f;
        g = .6f; /* orange */
    }
}

/* ---- cross-context (ISR <-> main loop) UI state ---- */
static volatile int      bar_pipe_  = -1;     /* pipe/volume bar showing */
static volatile int      bar_page_  = 0;      /* pipes: 0 = level,
                                               * 1 = Character; the
                                               * Modulation-mode
                                               * destinations: 2 = attack,
                                               * 3 = release, 4 = volume
                                               * modulation, 5 = Character
                                               * modulation; volume bar:
                                               * 0 = master, 1 = drive,
                                               * 2 = delay mix,
                                               * 3 = reverb mix,
                                               * 4 = drive tone,
                                               * 5 = fine tune,
                                               * 6 = reverb decay,
                                               * 7 = delay time */
static volatile uint32_t bar_until_ = 0;      /* ms timestamp for bar timeout */
static const int         kVolBar    = OrganEngine::kNumPipes; /* bar_pipe_: master volume */
static const int         kRotaryBar = OrganEngine::kNumPipes + 1; /* bar_pipe_: wheel (rotary/vibrato) */
static const int         kWheelEnc  = 4; /* enc[4]: transport wheel (ENC_5) */
/** bar_page_ sub-states for kRotaryBar: 0 = rotary amount, 1 = vibrato
 *  depth, 2 = vibrato note-on delay (SHIFT+turn in vibrato mode) */
static const int         kWheelBarVibrato = 1;
static const int         kWheelBarVibratoDelay = 2;
/** Transport-wheel selection: false = rotary (boot default), true =
 *  vibrato. Toggled by the debounced wheel-press edge. Volatile: written
 *  by PollControls (audio ISR), read by UpdateLeds (main loop). */
static volatile bool wheel_vibrato_mode_ = false;
/** Per-pipe Normal-mode encoder pages: 0 = Volume, 1 = Character,
 *  2 = pipe length (the +/-2-octave offset); cycled per pipe by that
 *  pipe's encoder push-switch (attack/release are edited through the
 *  Modulation-mode destination instead).
 *  Independent per pipe and preserved across mode changes. */
static volatile int pipe_page_[OrganEngine::kNumPipes];

/** Modulation Mode: the hardware toggle switch next to SHIFT/CHOMPI
 *  selects Normal Mode (DOWN) or Modulation Mode (UP). Polarity
 *  verified against CHOMPI-Tape: Tape's recording/input monitoring is
 *  active when switch_state == false (NormalPage.h
 *  SetInputMonitor(!switch_state)) and Tape records only with the
 *  switch UP, so UP == hw.GetToggleState() == false. Uses the debounced
 *  GetToggleState() (tog_state hysteresis in ProcessAllControls). */
static bool ModModeSwitchUp() { return !hw.GetToggleState(); }

/** mod_mode_: sampled from the toggle in PollControls (volatile:
 *  ISR-written, main-loop-read for the knob LEDs and bars).
 *  mod_dest_: the ONE shared Modulation-mode destination (ModDest),
 *  selected with SHIFT + a physical black key (see ModDest.h); boots on
 *  volume modulation, is remembered across mode changes until reboot
 *  and is never stored in the presets. The Normal Mode pipe_page_ is
 *  separate state and is preserved across mode changes. */
static volatile bool mod_mode_ = false;
static bool          prev_mod_mode_ = false; /* ISR-only edge tracking */
static volatile int  mod_dest_ = kModDestDefault;
/** bar_page_ sub-states for pipe bars in Modulation Mode: explicit
 *  non-colliding IDs (Normal-mode pipe bars use the pipe_page_ values
 *  0-2 directly: volume, Character, pipe length). */
static const int kModBarVolume    = 4;
static const int kModBarCharacter = 5;
static const int kModBarAttack    = 6;
static const int kModBarRelease   = 7;
static const int kModBarLengthMod = 8;
static const int kModBarDriveMod  = 9;
static const int kModBarDelayMod  = 10;
/** bar_page_ for a pipe-encoder turn, indexed by ModDest: attack and
 *  release reuse the purple/yellow envelope bars; the global drive
 *  modulation gets a red bar (the overdrive color), the global delay
 *  modulation a yellow bar (the stereo-delay color). */
static const int kModDestBarPage[kNumModDests]
    = {kModBarAttack, kModBarRelease, kModBarVolume, kModBarCharacter,
       kModBarLengthMod, kModBarDriveMod, kModBarDelayMod};

/** ENC_6 (volume encoder) pages: master volume -> overdrive -> stereo
 *  delay mix -> plate reverb mix, cycled by the encoder push-switch. */
static const int kVolPageMaster = 0;
static const int kVolPageDrive  = 1;
static const int kVolPageDelay  = 2;
static const int kVolPageReverb = 3;
static volatile int vol_page_ = kVolPageMaster; /* ENC_6 page */
/** bar_page_ sub-states for kVolBar (not vol_page_ values): SHIFT+turn
 *  edits a secondary parameter per page and the bar shows it instead of
 *  the page's main value. Explicit non-colliding IDs, distinct from
 *  every kVolPage* number. */
static const int kVolBarTone      = 4; /* drive page: distortion tone */
static const int kVolBarFineTune  = 5; /* master page: fine tune */
static const int kVolBarDecay     = 6; /* reverb page: decay */
static const int kVolBarDelayTime = 7; /* delay page: time */

/* ---- key/encoder edge tracking (audio ISR only) ---- */
static volatile bool prev_key_[40];
static uint8_t       active_note_[40];      /* note used at note-on, +1 (0 = none) */
static volatile int  octave_ = 0;           /* -2..+2, SHIFT+KEY_16/KEY_17 */
static int      prev_enc_[6];
static bool     prev_enc_sw_[4];            /* ENC_1..4_SW push-switch states */
static uint32_t last_click_t_[6];           /* chatter filter timestamps */
static bool     shift_held_ = false;
static bool     latch_on_   = false;        /* KEY_28 (LOOP) toggles key latch */
static bool     latched_[40];               /* key released while latch on: note
                                             * keeps sounding until unlatch */
/** Audio routing selection (AudioRouting.h): SHIFT + Black 6 (C#4) =
 *  Normal, SHIFT + Black 7 (D#4) = Send FX, Normal Mode only. Volatile:
 *  written by PollControls (audio ISR), read every block by
 *  AudioCallback; Playback Mode forces Normal at the SetRouting call
 *  site without rewriting this selection. Session-only: boots Normal,
 *  never stored in the SD-card presets. */
static volatile int routing_ = kRoutingDefault;

/** MIDI note on/off goes through this queue so DMA transmits only happen
 *  in the main loop, never inside the audio ISR. B2: notes only - a
 *  dropped note-off leaves external gear hanging, so the queue is deep
 *  and CCs never enter it (they coalesce through the dirty flags below:
 *  only the latest step per pipe is sent). */
struct OutMsg
{
    uint8_t kind; /* 0 = note on, 1 = note off */
    uint8_t a;    /* note */
    uint8_t b;    /* unused */
};
static FIFO<OutMsg, 256> out_q;
static volatile uint8_t cc_pipe_step_[OrganEngine::kNumPipes];
static volatile uint8_t cc_character_step_[OrganEngine::kNumPipes];
static volatile bool    cc_pipe_dirty_[OrganEngine::kNumPipes];
static volatile bool    cc_character_dirty_[OrganEngine::kNumPipes];
static volatile int     cc_vibrato_step_  = 0;     /* CC 1 out: vibrato
                                                    * depth, coalesced like
                                                    * the pipe CCs */
static volatile bool    cc_vibrato_dirty_ = false;

/* ---- SD-card presets: one bank of 15 slots (PIPES/P01..P15.json) ---- */
static OrganPresetBank preset_bank_;
static PresetStorage   preset_storage_;

/* Record-to-SD: a global so its FIL/bounce buffers stay in D1 .bss
 * (the SDMMC IDMA cannot reach the DTCMRAM stack - same rule as
 * preset_storage_). Shares the card mounted by PresetStorage. */
static SdRecorder sd_recorder_;

/* SD playback: same global-instance rule as the recorder. */
static SdPlayer sd_player_;

/** Preset UI modes (Tape's gestures): slot-selection modes plus the
 *  >=1 s SAVING/COPYING/ERASING animations. Volatile: written by
 *  PollControls (audio ISR), read by UpdateLeds (main loop); the main
 *  loop only ends the animations and cancels a selection when the card
 *  is removed. */
enum PresetMode
{
    kPresetNone = 0,
    kPresetSaveSel,
    kPresetSaving,
    kPresetEraseSel,
    kPresetErasing,
    kPresetCopySrc,
    kPresetCopyDest,
    kPresetCopying
};
static volatile int      preset_mode_     = kPresetNone;
static volatile int      preset_sel_slot_ = -1; /* chosen slot */
static volatile int      preset_copy_src_ = -1; /* copy source slot */
static volatile int      preset_current_  = -1; /* last loaded/saved slot */
static volatile uint32_t preset_anim_t_   = 0;  /* animation start (ms) */

/* ---- init patch: SHIFT + OVERDUB (KEY_28) held for 2.5 s applies the
 *  init sound live; separate from preset_mode_, so it works without an
 *  SD card and the main loop's no-card cancel never touches it ---- */
static volatile bool     init_holding_ = false; /* hold in progress */
static volatile uint32_t init_hold_t_  = 0;     /* hold start (ms) */
static volatile uint32_t init_done_t_  = 0;     /* last apply (ms; 0 = never) */
static const uint32_t    kInitHoldMs   = 2500;

/* ---- record-to-SD chord (PLAY + LOOP) ----
 * Normal Mode only, no SHIFT (SHIFT+KEY_28 stays the init-patch hold):
 * hold PLAY+LOOP for 2 s (red fill bar) -> 4 s countdown on the first
 * four white keys -> recording starts (PTH 0 blinks red) and normal
 * play resumes. The same chord held 1 s stops (works in both modes).
 * KEY_27/KEY_28's percussion/latch toggles moved to the RELEASE edge
 * so a completed chord has no side effects. Volatile: written by
 * PollControls (audio ISR), read by UpdateLeds (main loop). */
enum RecState
{
    kRecIdle = 0,
    kRecHoldStart, /**< 2 s arm hold */
    kRecCountdown, /**< 4 s countdown */
    kRecActive,    /**< recording */
    kRecHoldStop,  /**< 1 s stop hold */
    kRecError      /**< ~2 s fast red blink on PTH 0 */
};
static volatile int      rec_state_   = kRecIdle;
static volatile uint32_t rec_state_t_ = 0; /* state entry (ms) */
static volatile uint32_t rec_flash_t_ = 0; /* no-card flash (0 = off) */
static volatile bool     play_pending_ = false; /* KEY_27 toggle arms
                                                 * here, fires on
                                                 * release */
static volatile bool     loop_pending_ = false; /* KEY_28: same for the
                                                 * latch */
static bool              play_edge_    = false; /* fresh KEY_27 down
                                                 * this pass */
static bool              loop_edge_    = false; /* fresh KEY_28 down
                                                 * this pass */
static bool              rec_seen_run_ = false; /* take reached
                                                 * kSRecord (for the
                                                 * 6 h auto-stop) */
static const uint32_t    kRecHoldStartMs = 2000;
static const uint32_t    kRecCountdownMs = 4000;
static const uint32_t    kRecHoldStopMs  = 1000;
static const uint32_t    kRecSwallowMs   = 400;
static const uint32_t    kRecErrorMs     = 2000;

/* ---- SD playback mode (hold PLAY for 2 s) ----
 * Plays the SD takes straight from the card, one at a time; pure
 * playback: all 25 keys are inert as notes, MIDI In is ignored, the
 * take bypasses every FX stage and the jam recorder is fully blocked.
 * Entry: PLAY held 2 s in Normal Mode (no preset mode active)
 * - a blue fill bar on the white keys while held; entry silences every
 * note, clears the latch, stops a running take (kPbStopping), scans
 * for takes (kPbScan) and enters on the newest take, paused. Exit: the
 * same hold again, or any flip of the toggle switch. The transport
 * (paused/playing) and the take-loop flag live in sd_player_.
 * Volatile: written by PollControls (audio ISR), read by UpdateLeds
 * (main loop). */
enum PbState
{
    kPbOff = 0,
    kPbEnterHold, /**< 2 s entry/exit hold (blue fill bar) */
    kPbStopping,  /**< entry: waiting for the recorder to close */
    kPbScan,      /**< entry: scanning the takes */
    kPbOn         /**< the mode is active */
};
static volatile int      playback_mode_ = kPbOff;
static volatile uint32_t pb_state_t_    = 0;     /* state entry (ms) */
static volatile bool     pb_hold_exit_  = false; /* the hold exits the
                                                  * mode */
static volatile uint32_t pb_flash_t_    = 0;     /* red flash (0 = off) */
static volatile uint32_t pb_flash_ms_   = 1000;  /* flash duration */
static const uint32_t    kPbHoldMs      = 2000;
static uint32_t          pb_scan_gen_   = 0;     /* scan epoch at entry */
static int               pb_skip_slot_  = -1;    /* slot whose take
                                                  * failed (red flash) */
static uint32_t          pb_skip_t_     = 0;

/** White-key SwIds, slot 0 (C3, leftmost) .. slot 14 (C5, rightmost). */
static const int kWhiteKeySw[kNumWhiteKeys] = {
    static_cast<int>(Hardware::SwId::KEY_1),
    static_cast<int>(Hardware::SwId::KEY_2),
    static_cast<int>(Hardware::SwId::KEY_3),
    static_cast<int>(Hardware::SwId::KEY_4),
    static_cast<int>(Hardware::SwId::KEY_5),
    static_cast<int>(Hardware::SwId::KEY_6),
    static_cast<int>(Hardware::SwId::KEY_7),
    static_cast<int>(Hardware::SwId::KEY_8),
    static_cast<int>(Hardware::SwId::KEY_9),
    static_cast<int>(Hardware::SwId::KEY_10),
    static_cast<int>(Hardware::SwId::KEY_11),
    static_cast<int>(Hardware::SwId::KEY_12),
    static_cast<int>(Hardware::SwId::KEY_13),
    static_cast<int>(Hardware::SwId::KEY_14),
    static_cast<int>(Hardware::SwId::KEY_15),
};

/** A4: incremented by the audio ISR; the IWDG is only refreshed while this
 *  keeps changing (see MainLoop), so any lockup resets the device. */
volatile uint32_t audio_ticks = 0;

/* ---- preset gesture helpers (audio ISR context) ---- */

static int WhiteKeySlot(int swid)
{
    for(int i = 0; i < kNumWhiteKeys; i++)
        if(kWhiteKeySw[i] == swid)
            return i;
    return -1;
}

/** Apply a preset to the live engine: the regular (ramped/
 *  phase-continuous) setters, so held and latched notes keep sounding
 *  click-free. Resyncs the MIDI controllers with the applied values
 *  (the coalesced CCs go out in MainLoop) and cancels any value bar.
 *  Used by slot loads and by the init-patch gesture. */
static void ApplyPresetLive(const OrganPreset &preset)
{
    ApplyToEngine(preset, engine);
    /* resync MIDI controllers with the applied values */
    for(int p = 0; p < OrganEngine::kNumPipes; p++)
    {
        cc_pipe_step_[p]       = engine.GetPipeStep(p);
        cc_pipe_dirty_[p]      = true;
        cc_character_step_[p]  = engine.GetPipeCharacterStep(p);
        cc_character_dirty_[p] = true;
    }
    cc_vibrato_step_  = engine.GetVibratoDepthStep();
    cc_vibrato_dirty_ = true;
    bar_pipe_  = -1; /* cancel any active value bar */
    bar_until_ = 0;
}

/** Load a slot: apply the RAM-cached preset live. Empty slots do
 *  nothing. */
static void PresetLoadSlot(int slot)
{
    if(!preset_bank_.IsValid(slot))
        return;
    ApplyPresetLive(preset_bank_.Get(slot));
    preset_current_ = slot;
}

static void PresetEnterMode(int mode)
{
    preset_mode_     = mode;
    preset_sel_slot_ = -1;
    preset_copy_src_ = -1;
    bar_pipe_        = -1; /* the preset view takes over the white keys */
    bar_until_       = 0;
}

/** A FRESH SHIFT press confirms the pending selection. Entering a mode
 *  needs SHIFT already held, so the entering hold can never double as
 *  the confirm; the user releases and re-presses SHIFT to confirm. */
static void PresetShiftConfirm()
{
    switch(preset_mode_)
    {
        case kPresetSaveSel:
            if(preset_sel_slot_ >= 0)
            {
                OrganPreset p;
                CaptureFromEngine(p, engine);
                preset_bank_.Save(preset_sel_slot_, p);
                preset_storage_.ClearIncompatibleSlot(preset_sel_slot_);
                preset_current_ = preset_sel_slot_;
                preset_mode_    = kPresetSaving;
                preset_anim_t_  = System::GetNow();
            }
            break;
        case kPresetEraseSel:
            if(preset_sel_slot_ >= 0)
            {
                preset_bank_.Erase(preset_sel_slot_);
                preset_storage_.ClearIncompatibleSlot(preset_sel_slot_);
                if(preset_current_ == preset_sel_slot_)
                    preset_current_ = -1; /* deleting the current slot
                                           * clears "current" */
                preset_mode_   = kPresetErasing;
                preset_anim_t_ = System::GetNow();
            }
            break;
        case kPresetCopyDest:
            if(preset_sel_slot_ >= 0 && preset_copy_src_ >= 0)
            {
                preset_bank_.Copy(preset_copy_src_, preset_sel_slot_);
                preset_storage_.ClearIncompatibleSlot(preset_sel_slot_);
                preset_mode_   = kPresetCopying;
                preset_anim_t_ = System::GetNow();
            }
            break;
        default: break; /* COPY_SRC: a destination is still needed */
    }
}

/** Preset gestures. Returns true when the key event is consumed (it never
 *  reaches the note/latch logic). Releases of notes started before a
 *  mode was entered fall through, so held notes cannot hang. Preset
 *  management is Normal-mode-only: while the switch is UP every event
 *  falls through (white keys stay playable with SHIFT, and notes
 *  release and latch normally, whatever the SD-card state). */
static bool PresetKeyEvent(int swid, bool down)
{
    if(mod_mode_)
        return false;
    if(preset_mode_ != kPresetNone)
    {
        if(!down)
            return active_note_[swid] == 0; /* live note: fall through */
        const int slot = WhiteKeySlot(swid);
        if(slot >= 0)
        {
            switch(preset_mode_)
            {
                case kPresetSaveSel:
                    preset_sel_slot_ = slot; /* any slot */
                    break;
                case kPresetEraseSel:
                    if(preset_bank_.IsValid(slot)) /* filled slots only */
                        preset_sel_slot_ = slot;
                    break;
                case kPresetCopySrc:
                    if(preset_bank_.IsValid(slot))
                    {
                        preset_copy_src_ = slot;
                        preset_mode_     = kPresetCopyDest;
                    }
                    break;
                case kPresetCopyDest:
                    if(slot != preset_copy_src_) /* a different slot */
                        preset_sel_slot_ = slot;
                    break;
                default: break; /* animations: consume everything */
            }
            return true;
        }
        /* the matching operation key cancels; the others are inert */
        if(swid == static_cast<int>(Hardware::SwId::KEY_23))
        {
            if(preset_mode_ == kPresetEraseSel)
                preset_mode_ = kPresetNone;
            return true;
        }
        if(swid == static_cast<int>(Hardware::SwId::KEY_24))
        {
            if(preset_mode_ == kPresetCopySrc
               || preset_mode_ == kPresetCopyDest)
                preset_mode_ = kPresetNone;
            return true;
        }
        if(swid == static_cast<int>(Hardware::SwId::KEY_25))
        {
            if(preset_mode_ == kPresetSaveSel)
                preset_mode_ = kPresetNone;
            return true;
        }
        return true; /* every other key is consumed while a mode is active */
    }

    /* NONE mode: SHIFT + key preset gestures (unshifted keys, and SHIFT
     * with the octave keys, fall through to the normal handling) */
    if(!down || !shift_held_)
        return false;
    const bool oct = swid == static_cast<int>(Hardware::SwId::KEY_16)
                     || swid == static_cast<int>(Hardware::SwId::KEY_17);
    if(preset_storage_.NoSdCard())
        return !oct; /* red-key view: gestures do nothing, octave works */
    if(swid == static_cast<int>(Hardware::SwId::KEY_23))
    {
        PresetEnterMode(kPresetEraseSel);
        return true;
    }
    if(swid == static_cast<int>(Hardware::SwId::KEY_24))
    {
        PresetEnterMode(kPresetCopySrc);
        return true;
    }
    if(swid == static_cast<int>(Hardware::SwId::KEY_25))
    {
        PresetEnterMode(kPresetSaveSel);
        return true;
    }
    const int slot = WhiteKeySlot(swid);
    if(slot >= 0)
    {
        PresetLoadSlot(slot);
        return true;
    }
    return false;
}

/** Key + encoder polling, called from the audio ISR once per block (2 kHz). */
static void PollControls()
{
    /* shift key (CHOMPI key) */
    shift_held_ = hw.button_sr.State(static_cast<int>(Hardware::SwId::KEY_26));

    /* Modulation Mode toggle (UP = modulation) - sampled BEFORE the
     * preset confirmation below, so a fresh SHIFT press on the same
     * polling pass that enters Modulation Mode can never confirm a
     * pending selection. Flipping the switch mid-bar cancels the old
     * mode's bar; the knob LEDs follow mod_mode_ directly on the next
     * UpdateLeds pass. Entering Modulation Mode cancels any unconfirmed
     * preset selection and ends any preset animation (already-confirmed
     * storage writes keep running in the background from the main
     * loop); the shared destination and the Normal-mode pages are
     * preserved. */
    mod_mode_ = ModModeSwitchUp();
    if(mod_mode_ != prev_mod_mode_)
    {
        prev_mod_mode_ = mod_mode_;
        bar_pipe_      = -1; /* cancel the other mode's bar */
        bar_until_     = 0;
        if(mod_mode_)
        {
            preset_mode_     = kPresetNone;
            preset_sel_slot_ = -1;
            preset_copy_src_ = -1;
        }
        /* flipping the toggle switch either way exits playback mode */
        if(playback_mode_ != kPbOff)
        {
            sd_player_.RequestStop();
            playback_mode_ = kPbOff;
            play_pending_  = false; /* held PLAY/LOOP releases are
                                     * inert */
            loop_pending_  = false;
        }
    }

    /* a fresh SHIFT press confirms a pending preset selection (preset
     * management is Normal-mode-only: gated while the switch is UP) */
    {
        static bool prev_shift_held_ = false;
        if(shift_held_ && !prev_shift_held_ && !mod_mode_
           && playback_mode_ == kPbOff)
            PresetShiftConfirm();
        prev_shift_held_ = shift_held_;
    }

    /* pipe encoder push-switches (ENC_1..4_SW = SwId 0..3). Normal Mode:
     * press cycles that pipe's encoder through page 1 (Volume), page 2
     * (Character) and page 3 (pipe length) and the newly active page's
     * value bar shows as
     * confirmation. Modulation Mode: unshifted presses do nothing (the
     * shared destination is selected with SHIFT + a black key). Either
     * way SHIFT+press toggles that pipe's mute (the press is fully
     * consumed: no page cycle); the persistent red knob LED is the mute
     * feedback, so no value bar is re-armed for it. */
    for(int i = 0; i < 4; i++)
    {
        const bool down = hw.button_sr.State(i);
        if(down && !prev_enc_sw_[i] && playback_mode_ == kPbOff)
        {
            for(int p = 0; p < OrganEngine::kNumPipes; p++)
            {
                if(pipe_encoder[p] == i)
                {
                    switch(PipeEncoderPress(mod_mode_, shift_held_))
                    {
                        case kPipePressMute:
                            engine.TogglePipeMuted(p);
                            break;
                        case kPipePressCyclePage:
                            pipe_page_[p] = NextNormalPipePage(pipe_page_[p]);
                            bar_pipe_     = p;
                            bar_page_     = pipe_page_[p];
                            bar_until_    = System::GetNow() + 500;
                            break;
                        default: break; /* kPipePressNothing */
                    }
                }
            }
        }
        prev_enc_sw_[i] = down;
    }

    /* ENC_6_SW (volume-knob push, SwId 32): press cycles the volume
     * encoder through master volume -> overdrive -> stereo delay mix ->
     * plate-reverb mix; show the newly active page's value bar as
     * confirmation */
    {
        const int  vsw  = static_cast<int>(Hardware::SwId::ENC_6_SW);
        const bool down = hw.button_sr.State(vsw);
        if(down && !prev_key_[vsw] && playback_mode_ == kPbOff)
        {
            vol_page_  = (vol_page_ + 1) % 4;
            bar_pipe_  = kVolBar;
            bar_page_  = vol_page_;
            bar_until_ = System::GetNow() + 500;
        }
        prev_key_[vsw] = down;
    }

    /* KEY_27 (Play/Pause): toggle the Hammond-style percussion on each
     * RELEASE edge, in Normal and Modulation modes alike; a 2 s hold
     * enters/exits playback mode instead (see below), and in playback
     * mode the release tap pauses/resumes the take. The toggle moved
     * from the down-edge
     * to the release so the PLAY+LOOP record chord is deterministic:
     * the press only arms the toggle (play_pending_), the release fires
     * it, and a completed (or >400 ms aborted) record hold consumes the
     * press so it never toggles. The engine only restarts the shared
     * decay on an all-notes-released note-on, so toggling while notes
     * are held synthesizes no attack. The settled boot baseline
     * captured in main() means a key held at power-on never toggles,
     * and this read does not disturb the KEY_26+27+28 shipping-mode
     * chord checked before audio starts. Solid amber on the PTH 7 LED
     * while enabled (UpdateLeds). */
    {
        const int  play_sw = static_cast<int>(Hardware::SwId::KEY_27);
        const bool down    = hw.button_sr.State(play_sw);
        if(down && !prev_key_[play_sw])
        {
            play_pending_ = true;
            play_edge_    = true; /* for the record chord below */
        }
        else if(!down && prev_key_[play_sw] && play_pending_)
        {
            if(playback_mode_ == kPbEnterHold)
            {
                /* a playback entry/exit hold is in progress: leave
                 * play_pending_ armed - the hold-cancel path below
                 * decides whether this release is a tap (its toggle
                 * fires there) or a cancelled hold (>400 ms: swallow) */
            }
            else
            {
                play_pending_ = false;
                if(playback_mode_ == kPbOn)
                {
                    /* playback mode: a PLAY tap pauses/resumes the
                     * take (reached when the exit hold never armed,
                     * e.g. with LOOP held) */
                    if(sd_player_.IsPlaying())
                        sd_player_.RequestPause();
                    else
                        sd_player_.RequestResume();
                }
                else if(playback_mode_ == kPbOff)
                    engine.SetPercussionEnabled(
                        !engine.GetPercussionEnabled());
                /* kPbStopping/kPbScan: the tap is inert */
            }
        }
        prev_key_[play_sw] = down;
    }

    /* KEY_28 (LOOP). Without SHIFT: toggles the key latch on the
     * RELEASE edge (moved from the down-edge for the PLAY+LOOP record
     * chord, same as KEY_27 above). While
     * latched, releasing a key keeps its note sounding; re-pressing a
     * key whose note is latched releases that note (toggle-off) instead
     * of retriggering it; unlatching releases every latched note whose
     * key is not physically held down again (those keep sounding and
     * release normally on key release). Applies to physical keys only,
     * never MIDI. With SHIFT held a press does NOT toggle the latch -
     * it starts the init-patch hold: keep both held for 2.5 s to apply
     * the init sound. Releasing either key, or a preset mode becoming
     * active, cancels the hold; at 2500 ms the init patch is applied
     * live (a fresh press is needed to fire again). */
    {
        const int      loop_sw = static_cast<int>(Hardware::SwId::KEY_28);
        const bool     down    = hw.button_sr.State(loop_sw);
        const uint32_t now     = System::GetNow();
        if(down && !prev_key_[loop_sw])
        {
            loop_edge_ = true; /* for the record chord below */
            if(playback_mode_ != kPbOff)
            {
                /* playback mode: the release tap toggles the take
                 * loop; the latch and the init-patch hold are gated */
                loop_pending_ = true;
            }
            else if(shift_held_)
            {
                /* SHIFT + OVERDUB: init-patch hold, no latch toggle */
                if(preset_mode_ == kPresetNone)
                {
                    init_holding_ = true;
                    init_hold_t_  = now;
                }
            }
            else
            {
                loop_pending_ = true; /* latch toggle fires on release */
            }
        }
        else if(!down && prev_key_[loop_sw] && loop_pending_)
        {
            loop_pending_ = false;
            if(playback_mode_ == kPbOn)
            {
                /* playback mode: a LOOP tap toggles the take loop */
                sd_player_.SetLoop(!sd_player_.GetLoop());
            }
            else if(playback_mode_ == kPbOff)
            {
                latch_on_ = !latch_on_;
                if(!latch_on_)
                {
                    for(int swid = 7; swid < 32; swid++)
                    {
                        if(latched_[swid] && active_note_[swid] != 0
                           && !hw.button_sr.State(swid))
                        {
                            const uint8_t note = active_note_[swid] - 1;
                            engine.NoteOff(note);
                            out_q.PushBack({1, note, 0});
                        }
                        latched_[swid] = false;
                    }
                }
            }
        }
        prev_key_[loop_sw] = down;

        if(init_holding_)
        {
            if(!down || !shift_held_ || preset_mode_ != kPresetNone)
            {
                init_holding_ = false; /* cancelled */
            }
            else if(now - init_hold_t_ >= kInitHoldMs)
            {
                OrganPreset p;
                p.SetInit();
                ApplyPresetLive(p);
                preset_current_ = -1; /* no slot matches the sound now */
                init_done_t_    = now;
                init_holding_   = false;
            }
        }
    }

    /* ---- SD playback mode (hold PLAY for 2 s) ----
     * Entry: PLAY held 2 s in Normal Mode, no preset mode active (a
     * blue bar fills the white keys while held; early release cancels;
     * an aborted >400 ms hold swallows the PLAY toggle, same rule as
     * the record chord). LOOP joining the hold cancels it, so the
     * PLAY+LOOP record chord keeps working even when PLAY comes down
     * first. The completed entry silences every note, clears the
     * latch, stops a running take (kPbStopping waits for the
     * recorder's clean close), scans the takes (kPbScan) and enters
     * the mode on the newest take, paused. Exit: the same hold again
     * (handled here via pb_hold_exit_), or any flip of the toggle
     * switch (handled at the mod_mode_ edge above). No card or zero
     * takes: ~1 s red flash, the mode stays off; a player error
     * inside the mode (card pulled, every take failed): ~2 s red
     * flash and the mode exits. */
    {
        const bool play_down
            = hw.button_sr.State(static_cast<int>(Hardware::SwId::KEY_27));
        const bool loop_down
            = hw.button_sr.State(static_cast<int>(Hardware::SwId::KEY_28));
        const uint32_t now = System::GetNow();

        switch(playback_mode_)
        {
            case kPbOff:
                if(play_down && !loop_down && play_edge_ && !mod_mode_
                   && preset_mode_ == kPresetNone)
                {
                    pb_hold_exit_  = false;
                    playback_mode_ = kPbEnterHold;
                    pb_state_t_    = now;
                }
                break;

            case kPbEnterHold:
                if(!play_down || loop_down)
                {
                    /* cancelled: early release, or LOOP joined (the
                     * PLAY+LOOP record chord takes over and owns the
                     * pending toggles from then on). A release within
                     * 400 ms is a tap, not a hold: its normal action
                     * fires (an entry hold taps percussion, an exit
                     * hold taps pause/resume); a longer cancelled
                     * hold swallows the press. */
                    if(!play_down)
                    {
                        if(now - pb_state_t_ <= kRecSwallowMs)
                        {
                            if(pb_hold_exit_)
                            {
                                if(sd_player_.IsPlaying())
                                    sd_player_.RequestPause();
                                else
                                    sd_player_.RequestResume();
                            }
                            else
                                engine.SetPercussionEnabled(
                                    !engine.GetPercussionEnabled());
                        }
                        play_pending_ = false;
                    }
                    /* an exit hold falls back to the mode, an entry
                     * hold to off */
                    playback_mode_ = pb_hold_exit_ ? kPbOn : kPbOff;
                }
                else if(now - pb_state_t_ >= kPbHoldMs)
                {
                    play_pending_ = false; /* the hold consumes PLAY */
                    loop_pending_ = false;
                    if(pb_hold_exit_)
                    {
                        sd_player_.RequestStop();
                        playback_mode_ = kPbOff;
                    }
                    else if(preset_storage_.NoSdCard())
                    {
                        pb_flash_t_    = now; /* ~1 s red, stay off */
                        pb_flash_ms_   = 1000;
                        playback_mode_ = kPbOff;
                    }
                    else
                    {
                        /* all-notes-off: release every sounding note
                         * (physical or MIDI) and clear the latch, so
                         * nothing rings on under the take */
                        for(int n = 0; n < 128; n++)
                        {
                            if(engine.IsNoteHeld(static_cast<uint8_t>(n)))
                            {
                                const uint8_t note
                                    = static_cast<uint8_t>(n);
                                engine.NoteOff(note);
                                out_q.PushBack({1, note, 0});
                            }
                        }
                        for(int swid = 7; swid < 32; swid++)
                        {
                            active_note_[swid] = 0;
                            latched_[swid]     = false;
                        }
                        latch_on_     = false;
                        loop_pending_ = false;
                        bar_pipe_     = -1;
                        bar_until_    = 0;
                        if(sd_recorder_.GetState() != SdRecorder::kSIdle)
                        {
                            /* a take is opening/recording/closing:
                             * stop it and wait for the clean close */
                            sd_recorder_.RequestStop();
                            playback_mode_ = kPbStopping;
                        }
                        else
                        {
                            pb_scan_gen_ = sd_player_.ScanGen();
                            sd_player_.RequestRescan();
                            playback_mode_ = kPbScan;
                        }
                        pb_state_t_ = now;
                    }
                }
                break;

            case kPbStopping:
                if(sd_recorder_.GetState() == SdRecorder::kSIdle)
                {
                    pb_scan_gen_ = sd_player_.ScanGen();
                    sd_player_.RequestRescan();
                    playback_mode_ = kPbScan;
                    pb_state_t_    = now;
                }
                break;

            case kPbScan:
                if(sd_player_.GetState() == SdPlayer::kSError)
                {
                    pb_flash_t_    = now;
                    pb_flash_ms_   = 2000;
                    playback_mode_ = kPbOff;
                }
                else if(sd_player_.ScanGen() != pb_scan_gen_
                        && sd_player_.GetState() == SdPlayer::kSIdle)
                {
                    /* the scan completed (the generation moved) */
                    if(sd_player_.TakeCount() <= 0)
                    {
                        pb_flash_t_    = now; /* no takes: ~1 s red */
                        pb_flash_ms_   = 1000;
                        playback_mode_ = kPbOff;
                    }
                    else
                    {
                        sd_player_.RequestPlay(0); /* newest, paused */
                        playback_mode_ = kPbOn;
                        pb_state_t_    = now;
                    }
                }
                break;

            case kPbOn:
            default:
                if(sd_player_.GetState() == SdPlayer::kSError)
                {
                    /* card removed mid-playback, or every take
                     * failed: ~2 s red flash, the mode exits */
                    pb_flash_t_    = now;
                    pb_flash_ms_   = 2000;
                    playback_mode_ = kPbOff;
                    play_pending_  = false;
                    loop_pending_  = false;
                }
                else if(play_down && !loop_down && play_edge_)
                {
                    pb_hold_exit_  = true; /* the exit hold */
                    playback_mode_ = kPbEnterHold;
                    pb_state_t_    = now;
                }
                break;
        }
    }

    /* ---- record-to-SD chord (PLAY + LOOP) ----
     * Start: hold PLAY+LOOP for 2 s in Normal Mode (no SHIFT, no preset
     * mode active) -> 4 s countdown -> the recorder starts. Stop: hold
     * the same chord for 1 s (works in both modes). A completed chord
     * consumes both keys' pending percussion/latch toggles; an aborted
     * hold that survived >400 ms swallows them too (an aborted gesture
     * is not two quick presses). With no SD card the four countdown
     * keys flash red for ~1 s and the gesture stays inert. The trigger
     * needs a fresh down-edge on one of the keys, so a chord held at
     * boot (baselined in prev_key_) or still held from the start
     * gesture can never fire. */
    {
        const bool play_down
            = hw.button_sr.State(static_cast<int>(Hardware::SwId::KEY_27));
        const bool loop_down
            = hw.button_sr.State(static_cast<int>(Hardware::SwId::KEY_28));
        const uint32_t now = System::GetNow();

        /* a recorder error (no free take number, full/failed card)
         * surfaces in every UI state */
        if(rec_state_ != kRecError
           && sd_recorder_.GetState() == SdRecorder::kSError)
        {
            rec_state_   = kRecError;
            rec_state_t_ = now;
        }

        switch(rec_state_)
        {
            case kRecIdle:
                if(play_down && loop_down && (play_edge_ || loop_edge_)
                   && !mod_mode_ && !shift_held_
                   && preset_mode_ == kPresetNone
                   && playback_mode_ == kPbOff)
                {
                    if(preset_storage_.NoSdCard())
                    {
                        rec_flash_t_  = now; /* red flash, stay idle */
                        play_pending_ = false;
                        loop_pending_ = false;
                    }
                    else
                    {
                        rec_state_   = kRecHoldStart;
                        rec_state_t_ = now;
                    }
                }
                break;

            case kRecHoldStart:
                if(!play_down || !loop_down || shift_held_ || mod_mode_)
                {
                    /* cancelled: early release, SHIFT mid-hold or the
                     * switch flipped to Modulation */
                    if(now - rec_state_t_ > kRecSwallowMs)
                    {
                        play_pending_ = false;
                        loop_pending_ = false;
                    }
                    rec_state_ = kRecIdle;
                }
                else if(now - rec_state_t_ >= kRecHoldStartMs)
                {
                    play_pending_ = false; /* the chord consumes both */
                    loop_pending_ = false;
                    rec_state_    = kRecCountdown;
                    rec_state_t_  = now;
                }
                break;

            case kRecCountdown:
                if(play_edge_ || loop_edge_)
                {
                    /* a fresh PLAY or LOOP press cancels; that press
                     * toggles nothing */
                    play_pending_ = false;
                    loop_pending_ = false;
                    rec_state_    = kRecIdle;
                }
                else if(now - rec_state_t_ >= kRecCountdownMs)
                {
                    sd_recorder_.RequestStart();
                    rec_seen_run_ = false;
                    rec_state_    = kRecActive;
                    rec_state_t_  = now;
                }
                break;

            case kRecActive:
                if(sd_recorder_.IsRecording())
                    rec_seen_run_ = true;
                else if(rec_seen_run_
                        && sd_recorder_.GetState() == SdRecorder::kSIdle)
                {
                    /* the recorder stopped on its own: the 6 h take
                     * limit hit and the file was closed cleanly */
                    rec_state_   = kRecIdle;
                    rec_state_t_ = now;
                    break;
                }
                /* a FRESH chord starts the 1 s stop hold (keys still
                 * held from the start gesture have no fresh edge and
                 * do not stop the take) */
                if(play_down && loop_down && (play_edge_ || loop_edge_))
                {
                    rec_state_   = kRecHoldStop;
                    rec_state_t_ = now;
                }
                break;

            case kRecHoldStop: /* both modes, no gate */
                if(!play_down || !loop_down)
                {
                    if(now - rec_state_t_ > kRecSwallowMs)
                    {
                        play_pending_ = false;
                        loop_pending_ = false;
                    }
                    rec_state_ = kRecActive;
                }
                else if(now - rec_state_t_ >= kRecHoldStopMs)
                {
                    play_pending_ = false;
                    loop_pending_ = false;
                    sd_recorder_.RequestStop();
                    rec_state_   = kRecIdle;
                    rec_state_t_ = now;
                }
                break;

            case kRecError:
            default:
                if(now - rec_state_t_ >= kRecErrorMs)
                    rec_state_ = kRecIdle;
                break;
        }
        play_edge_ = false; /* edges live for exactly one pass */
        loop_edge_ = false;
    }

    /* playable keys: SwId 7..31 (SHIFT+KEY_16/KEY_17 = octave down/up,
     * Normal Mode only; Modulation Mode routes SHIFT + black keys to
     * the destination selection) */
    for(int swid = 7; swid < 32; swid++)
    {
        const bool down = hw.button_sr.State(swid);
        if(down == prev_key_[swid])
            continue;

        /* Playback mode: every key is inert as a note; a white-key
         * press directly selects that take (slot 0 = C3 = newest).
         * Releases of notes started before the mode fall through, so
         * nothing hangs. prev_key_ keeps tracking every key, so no
         * phantom edge fires when the mode exits. */
        if(playback_mode_ != kPbOff)
        {
            if(!down && active_note_[swid] != 0)
            {
                /* fall through to the normal note-off below */
            }
            else
            {
                if(down && playback_mode_ == kPbOn)
                {
                    const int slot = WhiteKeySlot(swid);
                    if(slot >= 0 && slot < sd_player_.TakeCount())
                        sd_player_.RequestPlay(slot);
                }
                prev_key_[swid] = down;
                continue;
            }
        }

        /* Presets: while a selection mode (or its animation) is active,
         * key presses select/confirm/cancel and never play notes or
         * touch the latch; in NONE mode SHIFT+white-key loads a slot and
         * SHIFT+KEY_23/24/25 enters erase/copy/save select. Releases of
         * notes started before the mode fall through, so nothing hangs. */
        if(PresetKeyEvent(swid, down))
        {
            prev_key_[swid] = down;
            continue;
        }
        prev_key_[swid] = down;

        const bool oct_key = (swid == static_cast<int>(Hardware::SwId::KEY_16))
                             || (swid == static_cast<int>(Hardware::SwId::KEY_17));
        if(down)
        {
            /* Modulation Mode with SHIFT held, routed BEFORE the octave
             * and musical note-on handling: the first four physical
             * black keys select the shared destination (a stale value
             * bar from the old destination is cancelled), the fifth and
             * later black keys are inert - neither plays a note. White
             * keys fall through and play normally, whatever the SD-card
             * state. Octave changes stay Normal-mode-only (the octave
             * keys are black keys 1-2, so they reach the octave branch
             * only when the switch is DOWN). */
            if(mod_mode_ && shift_held_ && WhiteKeySlot(swid) < 0)
            {
                const int prev_dest = mod_dest_;
                mod_dest_           = ModDestAfterKey(mod_dest_, swid);
                if(mod_dest_ != prev_dest)
                {
                    bar_pipe_  = -1; /* cancel the old destination's bar */
                    bar_until_ = 0;
                }
                continue;
            }
            /* SHIFT + Black 6 (C#4) / Black 7 (D#4): audio routing
             * selection, Normal Mode only (the Modulation-Mode branch
             * above already consumed black keys with SHIFT). Consumed
             * before note-on, so these combos no longer play a note;
             * releases are no-ops (active_note_ never set). */
            if(shift_held_
               && (swid == static_cast<int>(Hardware::SwId::KEY_21)
                   || swid == static_cast<int>(Hardware::SwId::KEY_22)))
            {
                routing_ = RoutingAfterKey(routing_, swid);
                continue;
            }
            if(shift_held_ && oct_key)
            {
                int oct = octave_
                          + (swid == static_cast<int>(Hardware::SwId::KEY_16)
                                 ? -1
                                 : 1);
                octave_ = oct < -2 ? -2 : (oct > 2 ? 2 : oct);
                continue;
            }
            /* Re-pressing a key whose note is currently latched releases
             * that note from the latch (toggle-off) instead of retriggering
             * it. latched_ is only set while active_note_ != 0, so the old
             * note is always valid; the later key-up edge finds
             * active_note_ == 0 and does nothing. */
            if(latch_on_ && latched_[swid])
            {
                const uint8_t old  = active_note_[swid] - 1;
                active_note_[swid] = 0;
                latched_[swid]     = false;
                engine.NoteOff(old);
                out_q.PushBack({1, old, 0});
                continue;
            }
            int n = key_map[swid] + 12 * octave_;
            n              = n < 0 ? 0 : (n > 127 ? 127 : n);
            const uint8_t note = static_cast<uint8_t>(n);
            /* B3: re-pressing a latched key after an octave change used to
             * overwrite active_note_ and leak the old note (no NoteOff, no
             * MIDI note-off): release the old note first. With the
             * toggle-off branch above this only covers non-latch edge
             * cases, kept as a safety net. */
            if(active_note_[swid] != 0 && active_note_[swid] - 1 != note)
            {
                const uint8_t old = active_note_[swid] - 1;
                engine.NoteOff(old);
                out_q.PushBack({1, old, 0});
            }
            active_note_[swid] = note + 1;
            latched_[swid]     = false;
            engine.NoteOn(note, 1.f);
            out_q.PushBack({0, note, 0});
        }
        else if(active_note_[swid] != 0)
        {
            /* note-off uses the note that was started (octave may have
             * changed, or shift may now be held, while the key was down) */
            if(latch_on_)
            {
                /* latched: keep sounding; note-off is deferred to unlatch */
                latched_[swid] = true;
            }
            else
            {
                const uint8_t note   = active_note_[swid] - 1;
                active_note_[swid]   = 0;
                engine.NoteOff(note);
                out_q.PushBack({1, note, 0});
            }
        }
    }

    /* wheel press (ENC_5 push-switch via the ChompiEncoder click pin D10):
     * toggle the wheel between rotary-speaker and vibrato control and
     * show the newly selected control's value bar as confirmation.
     * RisingEdge is a const read of the debounced switch state, so this
     * coexists with the tracker reset inside the encoder loop below. */
    if(hw.enc[kWheelEnc].RisingEdge() && playback_mode_ == kPbOff)
    {
        wheel_vibrato_mode_ = !wheel_vibrato_mode_;
        bar_pipe_           = kRotaryBar;
        bar_page_           = wheel_vibrato_mode_ ? kWheelBarVibrato : 0;
        bar_until_          = System::GetNow() + 500;
    }

    /* encoders: enc_trackers accumulate clicks and reset to 0 on press */
    for(int i = 0; i < 6; i++)
    {
        if(hw.enc[i].RisingEdge())
            prev_enc_[i] = 0;

        const int delta = hw.enc_trackers[i] - prev_enc_[i];
        prev_enc_[i]    = hw.enc_trackers[i];
        if(delta == 0)
            continue;

        /* Chatter filter: the detent-less volume encoder can rest on a
         * quadrature transition and emit increment bursts at ~1 ms spacing,
         * which would re-arm the level bar forever. Accept at most one click
         * per 8 ms per encoder (fast hand turns are >10 ms per detent). */
        const uint32_t now_ms = System::GetNow();
        if(now_ms - last_click_t_[i] < 8)
            continue; /* dropped for good: prev_enc_ already synced */
        last_click_t_[i] = now_ms;

        if(i == kVolEnc)
        {
            if(playback_mode_ != kPbOff)
            {
                /* playback mode: ENC_6 is the master volume on every
                 * page (no page cycling, no SHIFT secondary, no value
                 * bar - the take window owns the white keys) */
                if(!shift_held_)
                {
                    engine.SetMasterStep(engine.GetMasterStep() + delta);
                }
            }
            else if(shift_held_)
            {
                if(vol_page_ == kVolPageDrive)
                {
                    /* SHIFT+turn on page 2: distortion tone, 1
                     * percentage point per click, clamped 0..100
                     * without wrapping (FR-4b): 0% = 500 Hz per stage
                     * (dark), 100% = 10 kHz per stage (two cascaded
                     * one-pole stages, ~12 dB/octave). The tone shows
                     * as a red value bar for 500 ms like page 3's
                     * decay bar; the amount is not touched. */
                    engine.SetDriveToneStep(engine.GetDriveToneStep() + delta);
                    bar_pipe_  = kVolBar;
                    bar_page_  = kVolBarTone;
                    bar_until_ = System::GetNow() + 500;
                }
                else if(vol_page_ == kVolPageReverb)
                {
                    /* SHIFT+turn on page 4: plate-reverb decay, 100
                     * log-spaced steps from 0.2 s to 20 s (fine tune
                     * stays on page 1) */
                    engine.SetReverbDecayStep(engine.GetReverbDecayStep()
                                              + delta);
                    bar_pipe_  = kVolBar;
                    bar_page_  = kVolBarDecay;
                    bar_until_ = System::GetNow() + 500;
                }
                else if(vol_page_ == kVolPageDelay)
                {
                    /* SHIFT+turn on page 3: stereo delay time, 5 ms per
                     * click, clamped 40..500 ms; the read position
                     * slews with a smooth BBD-like pitch glide, the
                     * echo history is never cleared */
                    engine.SetDelayTimeStep(engine.GetDelayTimeStep() + delta);
                    bar_pipe_  = kVolBar;
                    bar_page_  = kVolBarDelayTime;
                    bar_until_ = System::GetNow() + 500;
                }
                else
                {
                    /* SHIFT+ENC_6 on page 1: fine tune, +/-50 cents.
                     * The bar centers on white key 8: off-center spans
                     * red toward the offset direction, exactly
                     * centered is green */
                    engine.AddFineTuneStep(delta);
                    bar_pipe_  = kVolBar;
                    bar_page_  = kVolBarFineTune;
                    bar_until_ = System::GetNow() + 500;
                }
            }
            else if(vol_page_ == kVolPageDrive)
            {
                /* volume-encoder page 2: overdrive amount, 32 steps */
                engine.SetDriveStep(engine.GetDriveStep() + delta);
                bar_pipe_  = kVolBar;
                bar_page_  = kVolPageDrive;
                bar_until_ = System::GetNow() + 500;
            }
            else if(vol_page_ == kVolPageDelay)
            {
                /* volume-encoder page 3: combined stereo-delay control,
                 * 1% per click, clamped 0..100. 0..50 sweeps the mix
                 * 0..50% (0% is exact dry); above 50 the mix locks at
                 * 50% and the feedback rises 35%..100%, so a full bar
                 * is sustained "full delay feedback" */
                engine.SetDelayMixStep(engine.GetDelayMixStep() + delta);
                bar_pipe_  = kVolBar;
                bar_page_  = kVolPageDelay;
                bar_until_ = System::GetNow() + 500;
            }
            else if(vol_page_ == kVolPageReverb)
            {
                /* volume-encoder page 4: plate-reverb mix, 32 steps
                 * (same granularity as the overdrive amount) */
                engine.SetReverbMixStep(engine.GetReverbMixStep() + delta);
                bar_pipe_  = kVolBar;
                bar_page_  = kVolPageReverb;
                bar_until_ = System::GetNow() + 500;
            }
            else
            {
                engine.SetMasterStep(engine.GetMasterStep() + delta);
                bar_pipe_  = kVolBar;
                bar_page_  = kVolPageMaster;
                bar_until_ = System::GetNow() + 500;
            }
        }
        else if(i == kWheelEnc && playback_mode_ == kPbOff)
        {
            if(wheel_vibrato_mode_ && shift_held_)
            {
                /* wheel in vibrato mode, SHIFT+turn: vibrato note-on
                 * delay, 0..50 steps of 100 ms (0..5 s), clamped, no
                 * wrap; 0 = the vibrato starts with the note */
                engine.SetVibratoDelayStep(engine.GetVibratoDelayStep()
                                           + delta);
                bar_pipe_  = kRotaryBar;
                bar_page_  = kWheelBarVibratoDelay;
                bar_until_ = System::GetNow() + 500;
            }
            else if(wheel_vibrato_mode_)
            {
                /* wheel in vibrato mode: depth 0..50 cents peak */
                engine.SetVibratoDepthStep(engine.GetVibratoDepthStep()
                                           + delta);
                cc_vibrato_step_  = engine.GetVibratoDepthStep();
                cc_vibrato_dirty_ = true; /* coalesced, sent in MainLoop */
                bar_pipe_  = kRotaryBar;
                bar_page_  = kWheelBarVibrato;
                bar_until_ = System::GetNow() + 500;
            }
            else
            {
                /* transport wheel (ENC_5): rotary-speaker amount, 32 steps */
                engine.SetRotaryStep(engine.GetRotaryStep() + delta);
                bar_pipe_  = kRotaryBar;
                bar_page_  = 0;
                bar_until_ = System::GetNow() + 500;
            }
        }
        else if(playback_mode_ == kPbOff)
        {
            for(int p = 0; p < OrganEngine::kNumPipes; p++)
            {
                if(pipe_encoder[p] != i)
                    continue;
                if(mod_mode_)
                {
                    /* Distortion and delay modulation are GLOBAL (the
                     * drive and the stereo delay are shared by all
                     * four pipes): only the leftmost pipe encoder
                     * (pipe 1) adjusts the one amount; the other three
                     * knobs are inert. SHIFT+turn behaves the same as
                     * an unshifted turn - there is nothing to
                     * broadcast. No MIDI CC. */
                    if(mod_dest_ == kModDestDriveMod
                       || mod_dest_ == kModDestDelayMod)
                    {
                        if(p == 0)
                        {
                            if(mod_dest_ == kModDestDriveMod)
                                engine.SetDriveModStep(
                                    engine.GetDriveModStep() + delta);
                            else
                                engine.SetDelayModStep(
                                    engine.GetDelayModStep() + delta);
                            bar_pipe_  = p;
                            bar_page_  = kModDestBarPage[mod_dest_];
                            bar_until_ = System::GetNow() + 500;
                        }
                        continue;
                    }
                    /* Modulation Mode turn: adjust the SHARED
                     * destination's parameter for this pipe - attack
                     * and release edit the actual envelope times (the
                     * existing 32-step logarithmic 5 ms..10 s mapping),
                     * the modulation amounts keep their 32-step
                     * 0..100% range, clamped, no wrap. SHIFT+turn
                     * applies the delta to the turned pipe and
                     * broadcasts the clamped result to all 4 pipes
                     * (the same absolute-broadcast pattern as Normal
                     * Mode). No MIDI CCs for any of these. */
                    if(shift_held_)
                    {
                        int v;
                        switch(mod_dest_)
                        {
                            case kModDestAttack:
                                v = BroadcastStep(
                                    engine.GetPipeAttackStep(p),
                                    delta,
                                    OrganEngine::kMaxEnvSteps);
                                for(int q = 0; q < OrganEngine::kNumPipes;
                                    q++)
                                    engine.SetPipeAttackStep(q, v);
                                break;
                            case kModDestRelease:
                                v = BroadcastStep(
                                    engine.GetPipeReleaseStep(p),
                                    delta,
                                    OrganEngine::kMaxEnvSteps);
                                for(int q = 0; q < OrganEngine::kNumPipes;
                                    q++)
                                    engine.SetPipeReleaseStep(q, v);
                                break;
                            case kModDestVolMod:
                                v = BroadcastStep(
                                    engine.GetPipeVolModStep(p),
                                    delta,
                                    OrganEngine::kMaxModSteps);
                                for(int q = 0; q < OrganEngine::kNumPipes;
                                    q++)
                                    engine.SetPipeVolModStep(q, v);
                                break;
                            case kModDestCharMod:
                                v = BroadcastStep(
                                    engine.GetPipeCharModStep(p),
                                    delta,
                                    OrganEngine::kMaxModSteps);
                                for(int q = 0; q < OrganEngine::kNumPipes;
                                    q++)
                                    engine.SetPipeCharModStep(q, v);
                                break;
                            default: /* kModDestLengthMod */
                                v = BroadcastStep(
                                    engine.GetPipeLengthModStep(p),
                                    delta,
                                    OrganEngine::kMaxModSteps);
                                for(int q = 0; q < OrganEngine::kNumPipes;
                                    q++)
                                    engine.SetPipeLengthModStep(q, v);
                                break;
                        }
                    }
                    else
                    {
                        switch(mod_dest_)
                        {
                            case kModDestAttack:
                                engine.SetPipeAttackStep(
                                    p, engine.GetPipeAttackStep(p) + delta);
                                break;
                            case kModDestRelease:
                                engine.SetPipeReleaseStep(
                                    p, engine.GetPipeReleaseStep(p) + delta);
                                break;
                            case kModDestVolMod:
                                engine.SetPipeVolModStep(
                                    p, engine.GetPipeVolModStep(p) + delta);
                                break;
                            case kModDestCharMod:
                                engine.SetPipeCharModStep(
                                    p, engine.GetPipeCharModStep(p) + delta);
                                break;
                            default: /* kModDestLengthMod */
                                engine.SetPipeLengthModStep(
                                    p,
                                    engine.GetPipeLengthModStep(p) + delta);
                                break;
                        }
                    }
                    bar_pipe_  = p;
                    bar_page_  = kModDestBarPage[mod_dest_];
                    bar_until_ = System::GetNow() + 500;
                    continue;
                }
                if(shift_held_)
                {
                    /* SHIFT+turn: apply the delta to the turned pipe,
                     * read back its clamped value, then write that same
                     * value to ALL 4 pipes (absolute broadcast, per the
                     * turned pipe's current page). The bar shows the
                     * value all four pipes now share. */
                    switch(pipe_page_[p])
                    {
                        case 1: /* Character */
                        {
                            engine.SetPipeCharacterStep(
                                p, engine.GetPipeCharacterStep(p) + delta);
                            const int v = engine.GetPipeCharacterStep(p);
                            for(int q = 0; q < OrganEngine::kNumPipes; q++)
                            {
                                engine.SetPipeCharacterStep(q, v);
                                cc_character_step_[q]  = v;
                                cc_character_dirty_[q]
                                    = true; /* coalesced, sent in MainLoop */
                            }
                            break;
                        }
                        case 2: /* pipe length */
                        {
                            engine.SetPipeLengthStep(
                                p, engine.GetPipeLengthStep(p) + delta);
                            const int v = engine.GetPipeLengthStep(p);
                            for(int q = 0; q < OrganEngine::kNumPipes; q++)
                                engine.SetPipeLengthStep(q, v);
                            break;
                        }
                        default: /* level */
                        {
                            engine.SetPipeStep(
                                p, engine.GetPipeStep(p) + delta);
                            const int v = engine.GetPipeStep(p);
                            for(int q = 0; q < OrganEngine::kNumPipes; q++)
                            {
                                engine.SetPipeStep(q, v);
                                cc_pipe_step_[q]  = v;
                                cc_pipe_dirty_[q]
                                    = true; /* coalesced, sent in MainLoop */
                            }
                            break;
                        }
                    }
                }
                else
                {
                    switch(pipe_page_[p])
                    {
                        case 1: /* Character */
                            engine.SetPipeCharacterStep(
                                p, engine.GetPipeCharacterStep(p) + delta);
                            cc_character_step_[p]
                                = engine.GetPipeCharacterStep(p);
                            cc_character_dirty_[p]
                                = true; /* coalesced, sent in MainLoop */
                            break;
                        case 2: /* pipe length */
                            engine.SetPipeLengthStep(
                                p, engine.GetPipeLengthStep(p) + delta);
                            break;
                        default: /* level */
                            engine.SetPipeStep(p, engine.GetPipeStep(p) + delta);
                            cc_pipe_step_[p]  = engine.GetPipeStep(p);
                            cc_pipe_dirty_[p] = true; /* coalesced, sent in MainLoop */
                            break;
                    }
                }
                bar_pipe_  = p;
                bar_page_  = pipe_page_[p];
                bar_until_ = System::GetNow() + 500;
            }
        }
    }
}

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
{
    audio_ticks++; /* A4: IWDG liveness proof */
#ifdef PIPES_CPU_METER
    cpu_meter.OnBlockStart();
#endif
    /* Program Change preset loads: the preset bank lives here, so
     * ProcessMidiIn only queues the slot. Applied before the MIDI drain
     * so CCs that arrive after the PC land on top of the loaded preset.
     * Playback mode ignores every MIDI In effect: the drain and the PC
     * slot loads are gated together. */
    if(playback_mode_ == kPbOff)
    {
        const int pc_slot = midi.TakePendingPresetSlot();
        if(pc_slot >= 0)
            PresetLoadSlot(pc_slot);
        midi.ProcessMidiIn();
    }
    hw.ProcessAllControls();
    PollControls();
    engine.Prepare(size);
    /* decode one block of the current take (silence while the mode is
     * off, paused or the ring is empty) */
    sd_player_.PopBlock(play_mix_l, play_mix_r, size);
    /* Playback Mode forces Normal routing (the takes must be audible
     * in the headphones); the stored selection resumes on exit */
    engine.SetRouting(playback_mode_ == kPbOff ? routing_
                                               : kRoutingNormal);
    engine.Process(out, size, in[2], in[3]);
    /* capture the pre-gain record tap (fixed -6 dB take level) */
    sd_recorder_.PushBlock(rec_tap_l, rec_tap_r, size);
#ifdef PIPES_CPU_METER
    cpu_meter.OnBlockEnd();
#endif
}

/** Key LEDs: preset views/modes while active, level bar while a pipe
 *  knob turns, the SHIFT slot overview, else held notes. */
static void UpdateLeds()
{
    const uint32_t now = System::GetNow();

    /* 250 ms blink for the preset UI */
    static bool     pre_blink   = false;
    static uint32_t pre_blink_t = 0;
    if(now - pre_blink_t > 250)
    {
        pre_blink   = !pre_blink;
        pre_blink_t = now;
    }

    /* no-card record flash: ~1 s on the four countdown keys */
    if(rec_flash_t_ != 0 && now - rec_flash_t_ >= 1000)
        rec_flash_t_ = 0;

    /* playback-mode flash: no card / no takes (~1 s) or player error
     * (~2 s) on all 15 white keys */
    if(pb_flash_t_ != 0 && now - pb_flash_t_ >= pb_flash_ms_)
        pb_flash_t_ = 0;

    /* a take that failed to open or failed validation flashes red on
     * its slot for ~0.5 s */
    {
        const int skipped = sd_player_.ConsumeSkipSlot();
        if(skipped >= 0)
        {
            pb_skip_slot_ = skipped;
            pb_skip_t_    = now;
        }
    }

    if(playback_mode_ == kPbEnterHold)
    {
        /* playback entry/exit hold progress: the same left->right
         * fill as the record chord's red bar, but blue */
        bar_pipe_ = -1;
        for(int i = 0; i < 25; i++)
            SetSmtLedFloat(i, 0.f, 0.f, 0.f);
        int lit = static_cast<int>((now - pb_state_t_) * kNumWhiteKeys
                                   / kPbHoldMs);
        if(lit > kNumWhiteKeys)
            lit = kNumWhiteKeys;
        for(int w = 0; w < lit; w++)
            SetSmtLedFloat(white_key_led[w], 0.f, 0.f, 1.f);
    }
    else if(playback_mode_ == kPbStopping || playback_mode_ == kPbScan)
    {
        /* entry in progress: a dim blue chase across the white keys
         * while the recorder closes / the take scan runs */
        bar_pipe_ = -1;
        for(int i = 0; i < 25; i++)
            SetSmtLedFloat(i, 0.f, 0.f, 0.f);
        const int pos = static_cast<int>((now / 80) % kNumWhiteKeys);
        SetSmtLedFloat(white_key_led[pos], 0.f, 0.f, .45f);
    }
    else if(playback_mode_ == kPbOn)
    {
        /* the take window: an unselected take's key is dim solid
         * white; the selected take's key is blue - solid while
         * paused, blinking at the 250 ms preset rate while playing;
         * empty slots stay off. The keys are inert as notes - a
         * white-key press directly selects its take. */
        bar_pipe_ = -1;
        for(int i = 0; i < 25; i++)
            SetSmtLedFloat(i, 0.f, 0.f, 0.f);
        const int count   = sd_player_.TakeCount();
        const int sel     = sd_player_.CurrentSlot();
        const bool playing = sd_player_.IsPlaying();
        for(int w = 0; w < kNumWhiteKeys && w < count; w++)
        {
            float r = .35f, g = .35f, b = .35f; /* unselected take */
            if(w == sel)
            {
                r = g = 0.f;
                b = playing ? (pre_blink ? 1.f : 0.f) : 1.f; /* blue */
            }
            if(w == pb_skip_slot_ && now - pb_skip_t_ < 500)
            {
                r = 1.f; /* this take failed to open: brief red */
                g = b = 0.f;
            }
            SetSmtLedFloat(white_key_led[w], r, g, b);
        }
    }
    else if(pb_flash_t_ != 0)
    {
        /* playback refusal (no card / no takes) or player error: all
         * 15 white keys red for the flash duration */
        for(int i = 0; i < 25; i++)
            SetSmtLedFloat(i, 0.f, 0.f, 0.f);
        for(int w = 0; w < kNumWhiteKeys; w++)
            SetSmtLedFloat(white_key_led[w], 1.f, 0.f, 0.f);
    }
    else if(preset_mode_ != kPresetNone && !mod_mode_)
    {
        /* A preset selection mode (or animation) takes over the white
         * keys; first match wins: during the SAVING/COPYING/ERASING
         * animations the slots show their new state steadily; in
         * copy-destination select the copy source is green; the
         * selected slot is blue (save / copy destination) or red
         * (erase target); the current slot (last loaded/saved) is
         * white when filled. The rest depends on the mode: in SAVE
         * select / copy-destination select filled slots are solid
         * purple, the current slot solid white and empty slots blink
         * dim grey; in ERASE select / copy-source select filled slots
         * blink dim purple, the current slot blinks white and empty
         * slots stay off. */
        for(int i = 0; i < 25; i++)
            SetSmtLedFloat(i, 0.f, 0.f, 0.f);
        const bool anim = preset_mode_ == kPresetSaving
                          || preset_mode_ == kPresetCopying
                          || preset_mode_ == kPresetErasing;
        const bool save_like = preset_mode_ == kPresetSaveSel
                               || preset_mode_ == kPresetCopyDest;
        for(int w = 0; w < kNumWhiteKeys; w++)
        {
            float      r = 0.f, g = 0.f, b = 0.f;
            const bool filled = preset_bank_.IsValid(w);
            if(anim)
            {
                if(filled)
                {
                    r = .55f;
                    b = 1.f; /* steady purple */
                }
            }
            else if(preset_mode_ == kPresetCopyDest && w == preset_copy_src_)
            {
                g = 1.f; /* copy source: green */
            }
            else if(w == preset_sel_slot_)
            {
                if(preset_mode_ == kPresetEraseSel)
                    r = 1.f; /* erase target: red */
                else
                    b = 1.f; /* save / copy destination: blue */
            }
            else if(filled && w == preset_current_)
            {
                if(save_like || pre_blink)
                    r = g = b = 1.f; /* current: solid white (save /
                                      * copy dest), blinking white
                                      * (erase / copy source) */
            }
            else if(preset_storage_.IsIncompatibleSlot(w) && pre_blink)
            {
                r = 1.f; /* unsupported preset schema */
            }
            else if(filled)
            {
                if(save_like)
                {
                    r = .55f;
                    b = 1.f; /* solid purple */
                }
                else if(pre_blink)
                {
                    r = .22f;
                    b = .4f; /* 0.4x purple, blinking */
                }
            }
            else if(save_like && pre_blink)
            {
                r = g = b = .4f; /* selectable empty slots: blinking grey */
            }
            SetSmtLedFloat(white_key_led[w], r, g, b);
        }
    }
    else if(rec_state_ == kRecHoldStart || rec_state_ == kRecHoldStop)
    {
        /* record arm/stop hold progress: the same red left->right fill
         * as the init bar (2 s to arm, 1 s to stop) */
        const uint32_t hold_ms = rec_state_ == kRecHoldStart
                                     ? kRecHoldStartMs
                                     : kRecHoldStopMs;
        for(int i = 0; i < 25; i++)
            SetSmtLedFloat(i, 0.f, 0.f, 0.f);
        int lit = static_cast<int>((now - rec_state_t_) * kNumWhiteKeys
                                   / hold_ms);
        if(lit > kNumWhiteKeys)
            lit = kNumWhiteKeys;
        for(int w = 0; w < lit; w++)
            SetSmtLedFloat(white_key_led[w], 1.f, 0.f, 0.f);
    }
    else if(rec_state_ == kRecCountdown)
    {
        /* 4 s record countdown on the first four white keys: all four
         * light white; every 1 s the next one turns red (key 1 red at
         * +1 s .. key 4 at +4 s, when the recording starts). Notes
         * keep playing under the overlay. */
        for(int i = 0; i < 25; i++)
            SetSmtLedFloat(i, 0.f, 0.f, 0.f);
        int red = static_cast<int>((now - rec_state_t_) / 1000);
        if(red > 4)
            red = 4;
        for(int w = 0; w < 4; w++)
        {
            const bool r = w < red;
            SetSmtLedFloat(white_key_led[w], 1.f, r ? 0.f : 1.f,
                           r ? 0.f : 1.f);
        }
    }
    else if(rec_flash_t_ != 0)
    {
        /* record chord with no SD card: the four countdown keys flash
         * red for ~1 s, nothing is armed */
        for(int i = 0; i < 25; i++)
            SetSmtLedFloat(i, 0.f, 0.f, 0.f);
        for(int w = 0; w < 4; w++)
            SetSmtLedFloat(white_key_led[w], 1.f, 0.f, 0.f);
    }
    else if(init_holding_)
    {
        /* init-patch hold progress: a yellow bar fills the 15 white
         * keys left to right over the 2.5 s hold (in Modulation Mode
         * the destination overlay below redraws the black keys) */
        for(int i = 0; i < 25; i++)
            SetSmtLedFloat(i, 0.f, 0.f, 0.f);
        int lit = static_cast<int>((now - init_hold_t_) * kNumWhiteKeys
                                   / kInitHoldMs);
        if(lit > kNumWhiteKeys)
            lit = kNumWhiteKeys;
        for(int w = 0; w < lit; w++)
            SetSmtLedFloat(white_key_led[w], 1.f, 1.f, 0.f);
    }
    else if(bar_pipe_ >= 0 && now < bar_until_)
    {
        const int p = bar_pipe_;
        /* value bar spans the 15 white keys only: leftmost white = 0%,
         * rightmost white = 100%. Volume bars (page-1 pipes + master) use
         * the green/orange/red position gradient; the page-2 pipe
         * Character bar is uniformly blue, the page-3 pipe-length bar
         * lights the current 3-key octave group purple, the
         * Modulation-mode attack destination bar is purple and the
         * release destination bar yellow, the overdrive bar uniformly
         * red and the rotary bar uniformly purple. */
        int    lit;
        BarKind kind = kBarVolume;
        if(p == kVolBar)
        {
            if(bar_page_ == kVolBarFineTune)
            {
                /* volume-knob page 1, SHIFT+turn: fine tune +/-50
                 * cents; lit = the offset in keys (0..7) from the
                 * center white key (the custom center-out draw below) */
                int offset = engine.GetFineTuneStep() - 16;
                if(offset < 0)
                    offset = -offset;
                lit  = (offset * 7 + 8) / 16;
                kind = kBarFineTune;
            }
            else if(bar_page_ == kVolBarTone)
            {
                /* volume-knob page 2, SHIFT+turn: distortion tone
                 * percent across the 15 white keys, same red as the
                 * overdrive bar */
                  lit  = (engine.GetDriveToneStep() * kNumWhiteKeys + 16) / 32;
                kind = kBarDrive;
            }
            else if(bar_page_ == kVolBarDecay)
            {
                /* volume-knob page 4, SHIFT+turn: reverb decay step
                 * (log-spaced 0.2..20 s) */
                  lit  = (engine.GetReverbDecayStep() * kNumWhiteKeys + 16) / 32;
                kind = kBarReverb;
            }
            else if(bar_page_ == kVolBarDelayTime)
            {
                /* volume-knob page 3, SHIFT+turn: delay time, 40..500 ms
                 * mapped to the 15 white keys */
                     lit  = (engine.GetDelayTimeStep() * kNumWhiteKeys + 16) / 32;
                kind = kBarDelay;
            }
            else if(bar_page_ == kVolPageReverb)
            {
                /* volume-knob page 4: plate-reverb mix */
                lit  = (engine.GetReverbMixStep() * kNumWhiteKeys + 16)
                       / OrganEngine::kMaxSteps;
                kind = kBarReverb;
            }
            else if(bar_page_ == kVolPageDelay)
            {
                /* volume-knob page 3: combined delay mix/feedback
                 * (0..50 = mix 0..50%, 50..100 = feedback 35%..100%) */
                  lit  = (engine.GetDelayMixStep() * kNumWhiteKeys + 16) / 32;
                kind = kBarDelay;
            }
            else if(bar_page_ == kVolPageDrive)
            {
                /* volume-knob page 2: overdrive amount */
                lit  = (engine.GetDriveStep() * kNumWhiteKeys + 16)
                       / OrganEngine::kMaxSteps;
                kind = kBarDrive;
            }
            else
            {
                lit = (engine.GetMasterStep() * kNumWhiteKeys + 16) / 32;
            }
        }
        else if(p == kRotaryBar)
        {
            if(bar_page_ == kWheelBarVibrato)
            {
                /* wheel in vibrato mode: depth 0..50 cents mapped across
                 * the 15 white keys (not 50 display segments) */
                  lit  = (engine.GetVibratoDepthStep() * kNumWhiteKeys + 16) / 32;
                kind = kBarVibrato;
            }
            else if(bar_page_ == kWheelBarVibratoDelay)
            {
                /* SHIFT+turn in vibrato mode: note-on delay 0..50 steps
                 * (100 ms each) mapped across the 15 white keys, same
                 * pink as the vibrato-depth bar */
                  lit  = (engine.GetVibratoDelayStep() * kNumWhiteKeys + 16) / 32;
                kind = kBarVibrato;
            }
            else
            {
                /* transport wheel: rotary-speaker amount */
                lit  = (engine.GetRotaryStep() * kNumWhiteKeys + 16)
                       / OrganEngine::kMaxSteps;
                kind = kBarRotary;
            }
        }
        else if(bar_page_ == kModBarVolume)
        {
            /* Modulation Mode, volume-modulation destination: the
             * pipe's volume-modulation amount */
            lit  = (engine.GetPipeVolModStep(p) * kNumWhiteKeys + 16)
                   / OrganEngine::kMaxModSteps;
            kind = kBarModVolume;
        }
        else if(bar_page_ == kModBarCharacter)
        {
            /* Modulation Mode, Character-modulation destination: the
             * pipe's Character-modulation amount */
            lit  = (engine.GetPipeCharModStep(p) * kNumWhiteKeys + 16)
                   / OrganEngine::kMaxModSteps;
            kind = kBarModCharacter;
        }
        else if(bar_page_ == kModBarLengthMod)
        {
            /* Modulation Mode, pipe-length-modulation destination: the
             * pipe's length-modulation amount */
            lit  = (engine.GetPipeLengthModStep(p) * kNumWhiteKeys + 16)
                   / OrganEngine::kMaxModSteps;
            kind = kBarModLength;
        }
        else if(bar_page_ == kModBarDriveMod)
        {
            /* Modulation Mode, distortion-modulation destination: the
             * GLOBAL drive-modulation amount (red, the overdrive
             * color) */
            lit  = (engine.GetDriveModStep() * kNumWhiteKeys + 16)
                   / OrganEngine::kMaxModSteps;
            kind = kBarDrive;
        }
        else if(bar_page_ == kModBarDelayMod)
        {
            /* Modulation Mode, delay-modulation destination: the
             * GLOBAL delay-modulation amount (yellow, the stereo-delay
             * color) */
            lit  = (engine.GetDelayModStep() * kNumWhiteKeys + 16)
                   / OrganEngine::kMaxModSteps;
            kind = kBarDelay;
        }
        else if(bar_page_ == 1)
        {
                 lit  = (engine.GetPipeCharacterStep(p) * kNumWhiteKeys + 16) / 32;
            kind = kBarCharacter;
        }
        else if(bar_page_ == 2)
        {
            /* Normal Mode page 3: pipe length, the -2..+2 octave
             * offset; lit is unused - the custom draw below lights the
             * current 3-key octave group */
            lit  = 0;
            kind = kBarLength;
        }
        else if(bar_page_ == kModBarAttack)
        {
            /* Modulation Mode, attack destination: actual attack time,
             * 32 log-spaced steps 5 ms..10 s */
            lit  = (engine.GetPipeAttackStep(p) * kNumWhiteKeys + 16)
                   / OrganEngine::kMaxEnvSteps;
            kind = kBarAttack;
        }
        else if(bar_page_ == kModBarRelease)
        {
            /* Modulation Mode, release destination: actual release
             * time, 32 log-spaced steps 5 ms..10 s */
            lit  = (engine.GetPipeReleaseStep(p) * kNumWhiteKeys + 16)
                   / OrganEngine::kMaxEnvSteps;
            kind = kBarRelease;
        }
        else
        {
            lit = (engine.GetPipeStep(p) * kNumWhiteKeys + 16)
                  / OrganEngine::kMaxSteps;
        }
        for(int i = 0; i < 25; i++)
            SetSmtLedFloat(i, 0.f, 0.f, 0.f);
        if(kind == kBarFineTune)
        {
            /* fine-tune bar, centered on white key 8 (index 7):
             * exactly centered = the center key alone in green;
             * off-center = a red span from the center key toward the
             * offset direction (left = down in pitch, right = up),
             * lit keys long */
            const int center = 7;
            const int cents  = engine.GetFineTuneCents();
            if(cents == 0)
            {
                SetSmtLedFloat(white_key_led[center], 0.f, 1.f, 0.f);
            }
            else
            {
                const int lo = cents < 0 ? center - lit : center;
                const int hi = cents < 0 ? center : center + lit;
                for(int w = lo; w <= hi && w < kNumWhiteKeys; w++)
                    SetSmtLedFloat(white_key_led[w], 1.f, 0.f, 0.f);
            }
        }
        else if(kind == kBarLength)
        {
            /* pipe-length bar: the 15 white keys are five 3-key octave
             * groups - keys 1-3 = -2 octaves, 4-6 = -1, 7-9 = the
             * default length, 10-12 = +1, 13-15 = +2; the current
             * group's three keys light yellow */
            const int base = (engine.GetPipeLengthOctave(p) + 2) * 3;
            for(int w = base; w < base + 3 && w < kNumWhiteKeys; w++)
                SetSmtLedFloat(white_key_led[w], 1.f, 1.f, 0.f);
        }
        else
        {
            for(int w = 0; w < lit && w < kNumWhiteKeys; w++)
            {
                float r, g, b;
                BarKeyColor(w, kind, r, g, b);
                SetSmtLedFloat(white_key_led[w], r, g, b);
            }
        }
    }
    else if(shift_held_ && !mod_mode_)
    {
        /* SHIFT preset view (NONE mode, Normal Mode only): the 15 white keys are the 15
         * preset slots - filled purple, the current slot white, empty
         * off; all red with no SD card. The two octave-key hints stay
         * lit; the erase/copy/save operation LEDs are added below. */
        for(int i = 0; i < 25; i++)
            SetSmtLedFloat(i, 0.f, 0.f, 0.f);
        if(preset_storage_.NoSdCard())
        {
            for(int w = 0; w < kNumWhiteKeys; w++)
                SetSmtLedFloat(white_key_led[w], 1.f, 0.f, 0.f);
        }
        else
        {
            for(int w = 0; w < kNumWhiteKeys; w++)
            {
                if(w == preset_current_ && preset_bank_.IsValid(w))
                    SetSmtLedFloat(white_key_led[w], 1.f, 1.f, 1.f);
                else if(preset_bank_.IsValid(w))
                    SetSmtLedFloat(white_key_led[w], .55f, 0.f, 1.f);
            }
        }
        /* the two octave keys (C#3/D#3) are always lit dim white so the
         * control is discoverable; the active direction turns orange
         * (dim = 1 octave, bright = 2) */
        const float dn = octave_ < 0 ? (octave_ < -1 ? 1.f : .45f) : 0.f;
        const float up = octave_ > 0 ? (octave_ > 1 ? 1.f : .45f) : 0.f;
        SetSmtLedFloat(0, dn > 0.f ? dn : .10f,
                       dn > 0.f ? .5f * dn : .10f, 0.f);
        SetSmtLedFloat(1, up > 0.f ? up : .10f,
                       up > 0.f ? .5f * up : .10f, 0.f);
        /* the active routing's selection key (Black 6/7) glows pink
         * while SHIFT is held - live feedback for the selection
         * gesture itself */
        float rr, rg, rb;
        RoutingColor(rr, rg, rb);
        SetSmtLedFloat(led_map[RoutingKeySw(routing_)], rr, rg, rb);
    }
    else
    {
        bar_pipe_ = -1;
        /* the active routing's selection key (Black 6/7) glows pink;
         * a held note on that key wins while pressed */
        const int routing_swid = RoutingKeySw(routing_);
        for(int swid = 7; swid < 32; swid++)
        {
            /* physical key held (octave-shifted note) or MIDI-held note
             * (C3: compare against the octave-shifted note) */
            int held_note = key_map[swid] + 12 * octave_;
            held_note = held_note < 0 ? 0 : (held_note > 127 ? 127 : held_note);
            const bool on
                = prev_key_[swid]
                  || engine.IsNoteHeld(static_cast<uint8_t>(held_note));
            if(!on && swid == routing_swid)
            {
                float r, g, b;
                RoutingColor(r, g, b);
                SetSmtLedFloat(led_map[swid], r, g, b);
            }
            else
            {
                SetSmtLedFloat(led_map[swid], on ? 1.f : 0.f, on ? 1.f : 0.f,
                               on ? 1.f : 0.f);
            }
        }
    }

    /* preset operation LEDs on the three top black keys (SMT 7/8/9):
     * erase = red, copy = green, save = blue, shown while SHIFT is held
     * or a preset mode is active; during an operation only that
     * operation's LED is lit; all off with no SD card. Normal Mode
     * only: in Modulation Mode the destination overlay below owns every
     * black key. */
    if(!mod_mode_ && playback_mode_ == kPbOff
       && (shift_held_ || preset_mode_ != kPresetNone))
    {
        float er = 0.f, cp = 0.f, sv = 0.f;
        if(!preset_storage_.NoSdCard())
        {
            switch(preset_mode_)
            {
                case kPresetNone: er = cp = sv = 1.f; break;
                case kPresetEraseSel:
                case kPresetErasing: er = 1.f; break;
                case kPresetCopySrc:
                case kPresetCopyDest:
                case kPresetCopying: cp = 1.f; break;
                default: sv = 1.f; break; /* save select / saving */
            }
        }
        SetSmtLedFloat(7, er, 0.f, 0.f);
        SetSmtLedFloat(8, 0.f, cp, 0.f);
        SetSmtLedFloat(9, 0.f, 0.f, sv);
    }

    /* Modulation Mode's persistent black-key destination display,
     * applied AFTER every keyboard overlay above so the value bars, the
     * distortion-tone bar, held notes, the octave hints and any preset
     * indicator can never overwrite it (the white keys are left to
     * those overlays): the seven destination keys glow in their
     * destination color - the selected key at exactly 100%, the other
     * six at exactly 75%; every other black key stays off, even while
     * its key plays a note. */
    if(mod_mode_)
    {
        for(int d = 0; d < kNumModDests; d++)
        {
            float r, g, b;
            ModDestColor(d, r, g, b);
            const float s = d == mod_dest_ ? kModDestSelected
                                           : kModDestUnselected;
            SetSmtLedFloat(led_map[kModDestKeySw[d]], r * s, g * s, b * s);
        }
        /* the seven destination keys occupy SMT LEDs 0-4, 7 and 8
         * (led_map); the active routing's selection key (SMT 5 or 6)
         * shows the routing pink; force off the remaining black-key
         * LEDs, even while those keys play notes */
        bool dest_led[10] = {false};
        for(int d = 0; d < kNumModDests; d++)
        {
            const int led = led_map[kModDestKeySw[d]];
            if(led < 10)
                dest_led[led] = true;
        }
        const int routing_led = led_map[RoutingKeySw(routing_)];
        if(routing_led < 10)
            dest_led[routing_led] = true;
        for(int i = 0; i < 10; i++)
            if(!dest_led[i])
                SetSmtLedFloat(i, 0.f, 0.f, 0.f);
        float rr, rg, rb;
        RoutingColor(rr, rg, rb);
        SetSmtLedFloat(routing_led, rr, rg, rb);
    }

    /* knob LEDs: pipe values on PTH 1-4, master volume on PTH 9.
     * Normal Mode: page 2 (Character) pipes glow blue with a brightness
     * floor so the page stays visible at step 0; page 1 (Volume) is
     * white; page 3 (pipe length) is yellow with brightness tracking
     * |octave| (a floor keeps it visible at the default). Modulation
     * Mode: all four encoders use the shared
     * destination's color with brightness = 0.2 + 0.8 x that pipe's
     * value for the destination parameter. A muted pipe's knob LED is
     * solid red in both modes, overriding the page/destination color. */
    for(int p = 0; p < OrganEngine::kNumPipes; p++)
    {
        if(engine.GetPipeMuted(p))
        {
            SetPthLedFloat(p + 1, 1.f, 0.f, 0.f); /* solid red: muted */
            continue;
        }
        if(mod_mode_)
        {
            float r, g, b;
            ModDestColor(mod_dest_, r, g, b);
            /* global distortion and delay modulation: only the
             * leftmost knob (pipe 1) shows the shared amount; knobs
             * 2-4 stay off */
            if((mod_dest_ == kModDestDriveMod
                || mod_dest_ == kModDestDelayMod)
               && p != 0)
            {
                SetPthLedFloat(p + 1, 0.f, 0.f, 0.f);
                continue;
            }
            int step, max;
            switch(mod_dest_)
            {
                case kModDestAttack:
                    step = engine.GetPipeAttackStep(p);
                    max  = OrganEngine::kMaxEnvSteps;
                    break;
                case kModDestRelease:
                    step = engine.GetPipeReleaseStep(p);
                    max  = OrganEngine::kMaxEnvSteps;
                    break;
                case kModDestVolMod:
                    step = engine.GetPipeVolModStep(p);
                    max  = OrganEngine::kMaxModSteps;
                    break;
                case kModDestCharMod:
                    step = engine.GetPipeCharModStep(p);
                    max  = OrganEngine::kMaxModSteps;
                    break;
                case kModDestDriveMod:
                    step = engine.GetDriveModStep();
                    max  = OrganEngine::kMaxModSteps;
                    break;
                case kModDestDelayMod:
                    step = engine.GetDelayModStep();
                    max  = OrganEngine::kMaxModSteps;
                    break;
                default: /* kModDestLengthMod */
                    step = engine.GetPipeLengthModStep(p);
                    max  = OrganEngine::kMaxModSteps;
                    break;
            }
            const float s = .2f + .8f * step * (1.f / max);
            SetPthLedFloat(p + 1, r * s, g * s, b * s);
            continue;
        }
        switch(pipe_page_[p])
        {
            case 1:
            {
                const float s = .2f
                                  + .8f * engine.GetPipeCharacterStep(p)
                                      * (1.f / OrganEngine::kMaxSteps);
                SetPthLedFloat(p + 1, 0.f, 0.f, s); /* blue: Character */
                break;
            }
            case 2:
            {
                const int oct = engine.GetPipeLengthOctave(p);
                const int mag = oct < 0 ? -oct : oct;
                const float s = .2f + .8f * mag * .5f;
                SetPthLedFloat(p + 1, s, s, 0.f); /* yellow: length */
                break;
            }
            default:
            {
                const float s = engine.GetPipeStep(p)
                                * (1.f / OrganEngine::kMaxSteps);
                SetPthLedFloat(p + 1, s, s, s); /* white: level */
                break;
            }
        }
    }
    /* master-volume knob LED (PTH 9): volume-encoder page 4 tracks the
     * plate-reverb mix in blue, page 3 the stereo-delay mix in yellow
     * and page 2 the overdrive amount in red
     * (all with a brightness floor so the page stays visible at 0%). On
     * page 1 brightness tracks the level and the color follows the same
     * position gradient as the white-key volume bar (green up to 50%,
     * orange 50-75%, red above 75%) */
    if(playback_mode_ != kPbOff)
    {
        /* playback mode: the knob is the master volume on every page
         * (the take is injected before the master gain) - the same
         * position gradient as the page-1 display */
        const float v = engine.GetMasterGain();
        if(v > .75f)
            SetPthLedFloat(9, v, 0.f, 0.f); /* red */
        else if(v > .5f)
            SetPthLedFloat(9, v, .6f * v, 0.f); /* orange */
        else
            SetPthLedFloat(9, 0.f, v, 0.f); /* green */
    }
    else if(vol_page_ == kVolPageReverb)
    {
        const float s = .2f
                        + .8f * engine.GetReverbMixStep()
                                  * (1.f / OrganEngine::kMaxSteps);
        SetPthLedFloat(9, 0.f, 0.f, s);
    }
    else if(vol_page_ == kVolPageDelay)
    {
        const float s = .2f
                        + .8f * engine.GetDelayMixStep()
                                  * (1.f / OrganEngine::kMaxSteps);
        SetPthLedFloat(9, s, s, 0.f); /* yellow */
    }
    else if(vol_page_ == kVolPageDrive)
    {
        const float s = .2f
                        + .8f * engine.GetDriveStep()
                                  * (1.f / OrganEngine::kMaxSteps);
        SetPthLedFloat(9, s, 0.f, 0.f);
    }
    else
    {
        const float v = engine.GetMasterGain();
        if(v > .75f)
            SetPthLedFloat(9, v, 0.f, 0.f); /* red */
        else if(v > .5f)
            SetPthLedFloat(9, v, .6f * v, 0.f); /* orange */
        else
            SetPthLedFloat(9, 0.f, v, 0.f); /* green */
    }

    /* wheel-mode indicators on the PTH pair at the wheel column (Rev4
     * PCB: the PTH chain runs LED1..LED10 = indices 0..9; LED6 = PTH 5
     * at x=200.1 mm and LED7 = PTH 6 at x=220.2 mm flank the wheel SW5
     * at x=210.1 mm). Left LED purple = rotary selected, right LED pink
     * = vibrato selected; the selected mode's LED stays lit until the
     * next toggle, the other is off. Boots on rotary. */
    if(wheel_vibrato_mode_)
    {
        SetPthLedFloat(5, 0.f, 0.f, 0.f);
        SetPthLedFloat(6, 1.f, 0.f, .5f); /* pink */
    }
    else
    {
        SetPthLedFloat(5, .55f, 0.f, 1.f); /* purple */
        SetPthLedFloat(6, 0.f, 0.f, 0.f);
    }

    /* CHOMPI/SHIFT key LED: blinking white for ~1 s after the init
     * patch applies (priority over everything below); then a fast red
     * blink on a recorder error and the 2 Hz red recording blink
     * (SHIFT-held purple wins while SHIFT is held); purple while SHIFT
     * is held with no preset mode active; blinking red once a slot is
     * selected in SAVE_SEL, ERASE_SEL or COPY_DEST; blinking white
     * during the SAVING, COPYING and ERASING animations; off otherwise
     * (copy-source select has no slot yet) */
    if(init_done_t_ != 0 && now - init_done_t_ < 1000)
    {
        const float w = pre_blink ? 1.f : 0.f;
        SetPthLedFloat(0, w, w, w);
    }
    else if(rec_state_ == kRecError)
    {
        /* recorder error (no free take number, full/failed card):
         * ~2 s of fast red blink */
        SetPthLedFloat(0, ((now / 125) & 1) ? 1.f : 0.f, 0.f, 0.f);
    }
    else if((rec_state_ == kRecActive || rec_state_ == kRecHoldStop)
            && !shift_held_)
    {
        /* recording: 2 Hz red blink on the 250 ms preset toggle; the
         * SHIFT-held purple below wins while SHIFT is held, so shift
         * operations keep their feedback */
        SetPthLedFloat(0, pre_blink ? 1.f : 0.f, 0.f, 0.f);
    }
    else if(preset_mode_ == kPresetNone)
    {
        if(shift_held_)
            SetPthLedFloat(0, .67f, 0.f, 1.f);
        else
            SetPthLedFloat(0, 0.f, 0.f, 0.f);
    }
    else if(preset_mode_ == kPresetSaving || preset_mode_ == kPresetCopying
            || preset_mode_ == kPresetErasing)
    {
        const float w = pre_blink ? 1.f : 0.f;
        SetPthLedFloat(0, w, w, w);
    }
    else if(preset_sel_slot_ >= 0
            && (preset_mode_ == kPresetSaveSel
                || preset_mode_ == kPresetEraseSel
                || preset_mode_ == kPresetCopyDest))
    {
        SetPthLedFloat(0, pre_blink ? 1.f : 0.f, 0.f, 0.f);
    }
    else
    {
        SetPthLedFloat(0, 0.f, 0.f, 0.f);
    }

    /* latch indicator on the LOOP key LED (PTH 8): solid red while
     * latched; in playback mode the LED is solid blue while the take
     * loop is on, off otherwise */
    if(playback_mode_ != kPbOff)
        SetPthLedFloat(8, 0.f, 0.f, sd_player_.GetLoop() ? 1.f : 0.f);
    else
        SetPthLedFloat(8, latch_on_ ? 1.f : 0.f, 0.f, 0.f);

    /* percussion indicator on the Play/Pause key LED (PTH 7): solid
     * amber while enabled; in playback mode the LED mirrors the
     * transport in blue (blinking = playing, solid = paused) */
    if(playback_mode_ != kPbOff)
    {
        const float b = sd_player_.IsPlaying() ? (pre_blink ? 1.f : 0.f)
                                               : 1.f;
        SetPthLedFloat(7, 0.f, 0.f, b);
    }
    else
    {
        SetPthLedFloat(7,
                       engine.GetPercussionEnabled() ? 1.f : 0.f,
                       engine.GetPercussionEnabled() ? .6f : 0.f,
                       0.f);
    }

#ifdef PIPES_CPU_METER
    /* B4: peak audio CPU load on PTH LED 0 (overrides shift indicator) */
    const float peak = cpu_meter.GetMaxCpuLoad();
    if(peak > .85f)
        SetPthLedFloat(0, 1.f, 0.f, 0.f);
    else if(peak > .70f)
        SetPthLedFloat(0, 1.f, .6f, 0.f);
    else
        SetPthLedFloat(0, 0.f, 1.f, 0.f);
#endif

    fill_led_data();
}

uint32_t ledt = 0, batt = 0;
static uint32_t midi_svc = 0;

void MainLoop(void *data)
{
    /* A4: refresh the IWDG only while the audio ISR keeps running - a
     * stuck ISR, a stuck main loop, or IRQs left disabled all reset the
     * device within ~2 s instead of hanging until a power cycle. */
    static uint32_t wd_audio_ticks = 0;
    if(audio_ticks != wd_audio_ticks)
    {
        wd_audio_ticks = audio_ticks;
        IWDG1->KR      = 0x0000AAAA;
    }

    /* staged, non-blocking preset SD writer: one small FatFS step per
     * pass, at thread level only (never in the audio ISR) */
    preset_storage_.Service(preset_bank_);

    /* staged WAV recorder drain: open/write/sync/close in small steps,
     * same thread-level-only rule as the preset writer */
    sd_recorder_.Service(System::GetNow());

    /* staged WAV player: scan/open/read in small steps, same
     * thread-level-only rule; all FatFS users (presets, recorder,
     * player) stay sequential on this one thread */
    sd_player_.Service(System::GetNow());

    /* drain queued MIDI note on/off messages (B2: notes only; CCs below) */
    while(!out_q.IsEmpty())
    {
        const OutMsg msg = out_q.PopFront();
        if(msg.kind == 0)
            midi.SendNoteOn(msg.a);
        else
            midi.SendNoteOff(msg.a);
    }

    /* B2: coalesced CCs - only the latest step per pipe goes out */
    for(int p = 0; p < OrganEngine::kNumPipes; p++)
    {
        if(cc_pipe_dirty_[p])
        {
            cc_pipe_dirty_[p] = false;
            midi.SendPipeCC(p, cc_pipe_step_[p]);
        }
        if(cc_character_dirty_[p])
        {
            cc_character_dirty_[p] = false;
            midi.SendCharacterCC(p, cc_character_step_[p]);
        }
    }
    if(cc_vibrato_dirty_)
    {
        cc_vibrato_dirty_ = false;
        midi.SendVibratoCC(cc_vibrato_step_);
    }

    /* A2: kick the DMA queue after draining so a message queued above
     * leaves in this same loop pass */
    midi.ProcessMidiOut();

    const uint32_t now = System::GetNow();

    /* the SAVING/COPYING/ERASING animations show for at least 1 s; a
     * removed SD card cancels any pending preset selection */
    if((preset_mode_ == kPresetSaving || preset_mode_ == kPresetCopying
        || preset_mode_ == kPresetErasing)
       && now - preset_anim_t_ >= 1000)
        preset_mode_ = kPresetNone;
    else if(preset_mode_ != kPresetNone && preset_storage_.NoSdCard())
        preset_mode_ = kPresetNone;

    if(now - ledt > 16)
    {
        ledt = now;
        UpdateLeds();
    }
    LedChainWatchdog();

    /* B1: service the MIDI-in listener (UART error recovery) */
    if(now - midi_svc > 50)
    {
        midi_svc = now;
        midi.ServiceMidiIn();
    }

#if !NO_BATT
    /* B6: 250 ms is plenty for a battery gauge; the I2C calls in here can
     * block the main loop, so don't run them at 50 Hz */
    if(now - batt > 250)
    {
        batt = now;
        hw.LowBatteryLockoutCheck();
    }
#endif

    System::DelayUs(10);
}

int main(void)
{
    hw.Init();
    midi.Init(&engine);

    hw.MpWrite(0x0c, 0B01010001); /* set BATT_LOW to 3V */
    hw.MpReadAll();
    for(size_t i = 0; i < 10; i++)
    {
        hw.LowBatteryLockoutCheck();
        System::Delay(10);
    }

    engine.Init(hw.seed.AudioSampleRate(), plate_delay_mem,
                delay_mem_l, delay_mem_r);

    /* SD-card presets: mount, create PIPES/ if missing, and read all 15
     * slots into the RAM cache before audio starts. With no card (or no
     * files) the cache stays empty and the organ runs on its factory
     * defaults; a cached preset is only applied when a slot is loaded. */
    preset_bank_.Init();
    preset_storage_.Init();
    preset_storage_.LoadAll(preset_bank_);

    /* record-to-SD: the recorder shares the card mounted above; the
     * tap captures the pre-master-gain bus at a fixed -6 dB level */
    sd_recorder_.Init(rec_ring, 262144, hw.seed.AudioSampleRate());
    engine.SetRecordTap(rec_tap_l, rec_tap_r);

    /* SD playback: the player shares the card mounted above; its
     * output is injected after the record tap, before the master
     * gain, so the take bypasses every FX stage, is never re-recorded
     * and the volume knob still scales it */
    sd_player_.Init(play_ring, 262144, hw.seed.AudioSampleRate());
    engine.SetPlaybackIn(play_mix_l, play_mix_r);

    LedSetup();

    /* Flush the shift registers for 0.5 s BEFORE starting audio;
     * KEY_26+27+28 held = shipping mode. The 4021 debounce counters start
     * at 0, which State() reads as "pressed", so polling controls before
     * they settle would fire bogus key/LOOP presses at boot. */
    uint32_t sleep_state = 0;
    for(int i = 0; i < 5000; i++)
    {
        hw.ProcessAllControls();
        sleep_state += hw.button_sr.State(int(Hardware::SwId::KEY_26))
                       && hw.button_sr.State(int(Hardware::SwId::KEY_27))
                       && hw.button_sr.State(int(Hardware::SwId::KEY_28));
        System::DelayUs(100);
    }
    if(sleep_state > 4000)
        hw.MpWrite(0x08, 0B10111111); /* SHIPPING MODE */

    /* Capture the settled control states as the edge baseline: nothing held
     * at power-on fires a note-on or toggles the latch, so the organ always
     * boots silent with the latch off. */
    for(int i = 0; i < 40; i++)
    {
        prev_key_[i]    = hw.button_sr.State(i);
        active_note_[i] = 0;
        latched_[i]     = false;
    }
    latch_on_ = false;
    for(int i = 0; i < 6; i++)
    {
        prev_enc_[i]     = hw.enc_trackers[i];
        last_click_t_[i] = 0;
    }
    for(int p = 0; p < OrganEngine::kNumPipes; p++)
    {
        pipe_page_[p]     = 0;
        prev_enc_sw_[p]   = hw.button_sr.State(p);
    }
    mod_dest_            = kModDestDefault; /* boot on volume modulation */
    vol_page_            = kVolPageMaster;
    wheel_vibrato_mode_  = false; /* boot with the wheel on rotary */
    mod_mode_            = ModModeSwitchUp(); /* settled toggle baseline */
    prev_mod_mode_       = mod_mode_;

    hw.StartAudio(AudioCallback);

#ifdef PIPES_CPU_METER
    cpu_meter.Init(hw.seed.AudioSampleRate(), 24); /* block size, hardware.h */
#endif

    /* A4: start the independent watchdog (~2 s): LSI ~32 kHz, /64 -> 500 Hz,
     * 1000 ticks ~ 2 s. Refreshed from MainLoop only while AudioCallback
     * keeps incrementing audio_ticks, so a stuck ISR, a stuck main loop or
     * IRQs-left-disabled all reset the device instead of hanging until a
     * power cycle. The bootloader is unaffected (IWDG is off until here). */
    IWDG1->KR  = 0x0000CCCC; /* start */
    IWDG1->KR  = 0x00005555; /* unlock PR/RLR */
    IWDG1->PR  = 4;          /* prescaler /64 */
    IWDG1->RLR = 1000;
    while(IWDG1->SR != 0) {} /* wait for PR/RLR to latch */
    IWDG1->KR  = 0x0000AAAA; /* first refresh */

    /* give USB control, enable AutoDPDM, take USB control (as chompi-wave) */
    hw.usb_sw.Write(false);
    System::Delay(1);
    hw.MpWrite(0x0a, 0B00100100);
    System::Delay(1);
    hw.usb_sw.Write(true);

    while(1)
    {
        MainLoop(nullptr);
    }
}
