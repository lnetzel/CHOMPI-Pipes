# Audio routing: normal monitoring and external send-FX loop

## Status and goal

Plan only. Do not implement firmware, test, or documentation changes as part
of creating this document.

Add a second, selectable audio routing so CHOMPI-PIPES can be patched through
an external stereo effects unit without repatching cables:

- **Normal routing** (boot default, today's behavior, bit-exact unchanged):
  the same audio goes to the headphone output and the line/master output; AUX
  IN is mixed into the bus and into SD recordings.
- **Send FX routing**: the line output carries only the dry organ bus (a send
  to the external effects unit); the headphone output carries only what comes
  back on AUX IN (the effected return); SD recordings contain only AUX IN.

Selection (SHIFT = the CHOMPI key, Normal Mode only):

- **SHIFT + Black 6 (C#4, KEY_21, SwId 22)** selects Normal routing.
- **SHIFT + Black 7 (D#4, KEY_22, SwId 23)** selects Send FX routing.

Exactly one routing is active at a time. In **Playback Mode** the effective
routing is always Normal (otherwise takes would not be audible in the
headphones); the user's selection is preserved and resumes when Playback Mode
exits.

## Confirmed decisions

These were settled with the user before this plan was written:

| Question | Decision |
|---|---|
| Which physical keys? | Black 6 (C#4) and Black 7 (D#4) — the 6th/7th black keys from the left. They are the black keys Modulation Mode does NOT use for destination selection (keys 6, 7, 10 are inert there), so there is no conflict. |
| Line-out send level in Send FX mode | Fixed level: the pre-master-gain bus. The volume knob does NOT change the send; it only scales the headphone (AUX return) level. Classic send/return behavior: set the send once, don't disturb the FX unit's input staging. |
| Where the shortcut works | Normal Mode only (toggle switch DOWN). In Modulation Mode SHIFT + Black 6/7 stays inert as today. |
| Visual feedback | None. The routing just changes (like octave shifts today). |
| Persistence | Session-only. Boots to Normal. Never stored in SD-card presets. |

Additional design decisions (defaults, called out for review):

- **Feedback-loop prevention:** in Send FX mode the AUX return is never sent
  back out the line output (that would feed the FX unit its own output). The
  line output carries only the dry organ bus.
- **Record level:** the Send FX recording tap keeps the fixed -6 dB
  `kRecordTapGain` (0.5), same level convention as Normal-mode takes.
- **Switching is instantaneous** (like flipping a hardware routing switch);
  a brief click is possible when switching mid-note. A short output crossfade
  is a possible future refinement, not part of this plan.

## Current implementation

Audio path, per sample, in [OrganEngine.h](../code/src/OrganEngine.h) `Process()`
([the per-sample tail](../code/src/OrganEngine.h#L1246-L1283)):

```
pipes -> fuzz -> rotary -> fuzz leveling -> BBD delay -> plate reverb
      -> += AUX IN (in[2]/in[3], passed as aux_l/aux_r)
      -> record tap write (bus x kRecordTapGain, fixed -6 dB)
      -> += SD playback injection (never re-recorded)
      -> x master_gain_
      -> out[0]=out_l, out[1]=out_r (headphones), out[2]=out_l, out[3]=out_r (line)
```

- Both output pairs always carry the identical signal
  ([OrganEngine.h](../code/src/OrganEngine.h#L1280-L1283)).
- AUX IN reaches the bus, both outputs, and recordings; it bypasses the organ
  FX (mixed in after the plate reverb). Documented in
  [README.md](../README.md) ("Stereo AUX IN is always mixed...").
- The record tap is registered from [chompi_main.cpp](../code/src/chompi_main.cpp)
  via `engine.SetRecordTap(rec_tap_l, rec_tap_r)` and drained by
  `sd_recorder_.PushBlock` in `AudioCallback`
  ([chompi_main.cpp](../code/src/chompi_main.cpp#L1830-L1862)).
- Key handling lives in `PollControls()` in the SwId 7..31 loop
  ([chompi_main.cpp](../code/src/chompi_main.cpp#L1330-L1346)): playback-mode
  gate first, then `PresetKeyEvent()`, then in the down branch the
  Modulation-Mode destination selection (SHIFT + black key, consumes keys 6/7
  as inert), then SHIFT + octave keys, then note-on.
- Playback mode states (`kPbOff`, `kPbEnterHold`, `kPbScan`, `kPbOn`,
  `kPbStopping`) and record states (`kRecIdle` ... `kRecError`) are
  ISR-polled; recording cannot start while playback is active.

## Planned routing behavior

| | Normal routing | Send FX routing |
|---|---|---|
| Line out (`out[2]/out[3]`) | bus x master gain (unchanged) | dry organ bus, pre-master-gain, fixed unity level; no AUX return |
| Headphones (`out[0]/out[1]`) | bus x master gain (unchanged) | AUX IN only, x master gain (volume knob = return level) |
| Record tap | bus incl. AUX, x 0.5 (unchanged) | AUX IN only, x 0.5 |
| AUX IN mixed into organ bus | yes (unchanged) | no |
| Playback injection | unchanged (audible everywhere) | unreachable: playback forces Normal |

Consequences, by design:

- With Send FX selected and nothing plugged into AUX IN, the headphones are
  silent and recordings are silence; the line output keeps sending the dry
  organ to the (absent or bypassed) FX chain.
- The volume knob in Send FX mode scales only the headphone return. The send
  level is the pre-master-gain bus, equivalent to the Normal-mode line-out
  level with the volume knob at maximum.
- MIDI, note handling, latch, percussion, presets, and every encoder behavior
  are unaffected; this is purely an output-distribution and record-tap change.

## Implementation plan

### 1. New pure header `code/src/AudioRouting.h`

Mirror the [ModDest.h](../code/src/ModDest.h) pattern (no Daisy includes, so
the native tests exercise the exact production logic):

- `enum AudioRouting { kRoutingNormal = 0, kRoutingSendFx, kNumRoutings };`
- `constexpr int kRoutingDefault = kRoutingNormal;`
- `constexpr int RoutingAfterKey(int cur, int swid)`: SwId 22 (KEY_21, Black
  6) -> `kRoutingNormal`; SwId 23 (KEY_22, Black 7) -> `kRoutingSendFx`; any
  other key -> `cur` unchanged.
- `chompi_main.cpp` static_asserts the two SwId values against
  `Hardware::SwId::KEY_21` / `KEY_22` (the ModDest.h file does the same for
  its key table).

### 2. `OrganEngine.h`: routing-aware output stage

- Add `void SetRouting(int r)` (plain int store, clamped; no ramps) and
  `int GetRouting() const`. `AudioCallback` calls it once per block, so the
  per-sample loop only reads a cached int.
- In `Process()`, branch on the cached routing in the per-sample tail:
  - `kRoutingNormal`: today's exact code path, bit-exact (aux mix, tap,
    playback injection, master gain, all four outs equal).
  - `kRoutingSendFx`:
    - skip the `out_l += aux_l[i]` bus mix;
    - record tap: `rec_tap_[i] = aux[i] * kRecordTapGain`;
    - playback injection stays in the shared code (unreachable in effect:
      playback forces Normal routing at the call site);
    - `out[0][i] = aux_l[i] * master_gain_`, `out[1][i] = aux_r[i] * master_gain_`;
    - `out[2][i] = out_l`, `out[3][i] = out_r` (pre-master-gain bus, fixed
      unity send).
- Update the `SetRecordTap`, `SetPlaybackIn`, and `Process` doc comments to
  describe both routings.

### 3. `chompi_main.cpp`: selection state and key handling

- New state near the other ISR-polled globals:
  `static volatile int routing_ = kRoutingNormal;`
- In `AudioCallback`, before `engine.Process(...)`:
  `engine.SetRouting(playback_mode_ == kPbOff ? routing_ : kRoutingNormal);`
  — this is the Playback-Mode override; the selection itself is never
  rewritten by playback entry/exit.
- In `PollControls()`, playable-keys loop, down branch: insert the routing
  selection **after** the Modulation-Mode destination branch and **before**
  the octave-key branch:

  ```cpp
  /* SHIFT + Black 6 (C#4) / Black 7 (D#4): audio routing selection,
   * Normal Mode only (the Modulation-Mode branch above already consumed
   * black keys with SHIFT). Consumed before note-on, so these combos no
   * longer play a note; releases are no-ops (active_note_ never set). */
  if(shift_held_
     && (swid == static_cast<int>(Hardware::SwId::KEY_21)
         || swid == static_cast<int>(Hardware::SwId::KEY_22)))
  {
      routing_ = RoutingAfterKey(routing_, swid);
      continue;
  }
  ```

  Placement notes:
  - `PresetKeyEvent()` runs earlier and consumes every key while a preset
    selection/animation is active, so routing cannot change mid-gesture —
    acceptable and consistent with other global gestures.
  - The playback-mode gate runs earlier still and makes all keys inert as
    notes during playback, so the combo is inert there — matching the forced
    Normal routing.
  - In Normal Mode today SHIFT + Black 6/7 plays a note (C#4/D#4); after this
    change the combo selects routing instead. Unshifted play is unaffected.

### 4. Tests: `code/tests/audio_routing_test.cpp`

- Key-mapping unit test mirroring `mod_dest_test.cpp`: both selection keys,
  inert keys (white keys, octave keys, preset keys, SwId bounds), and
  selection persistence across unrelated keys.
- Engine distribution checks are feasible natively: `percussion_test`
  already compiles `OrganEngine.h` with the vendored DaisySP headers. Add
  cases that run `Process()` with a constant AUX input and silence/no notes
  and assert, per routing: output-pair equality (Normal) vs. headphones = AUX
  x master gain and line out = pre-master bus without AUX (Send FX), plus the
  record-tap contents in both modes.
- Register the test in [code/tests/Makefile](../code/tests/Makefile): build
  rule (same include flags as `percussion_test` if it includes
  `OrganEngine.h`), `test:` list entry, and `clean` entry.

### 5. Documentation

- [README.md](../README.md): rewrite the AUX IN paragraph (currently "always
  mixed") to describe the two routings, the SHIFT + Black 6/Black 7 selection
  (Normal Mode only), the recording behavior per routing, the silent-
  headphones-when-unpatched consequence, and the Playback-Mode override.
- [midi.md](../midi.md): no change (no MIDI impact).
- No preset-format change; no EEPROM change.

## Edge cases

- **AUX unplugged in Send FX mode:** headphones and recordings are silent;
  line out still sends the dry organ. By design (user-confirmed).
- **Feedback loops:** impossible in firmware — the AUX return never leaves
  through the line out in Send FX mode. (An FX unit set to 100% wet is the
  normal use case; a wet/dry blend on the unit simply blends at the unit.)
- **Switching while recording:** the in-progress take keeps recording; blocks
  after the switch contain AUX only. No state machine interaction.
- **Switching while notes/latches sound:** instantaneous redistribution; a
  click is possible. No note hangs: the combo's down-edge is consumed before
  note-on, so `active_note_` is never set and the release edge is a no-op.
- **Playback entry/exit:** entry (hold, scan, stopping states included)
  forces Normal at the `SetRouting` call site; the stored selection resumes
  on exit. Recording stays blocked during playback as today.
- **Init patch (SHIFT + LOOP) and preset loads:** do not touch `routing_`;
  routing survives preset loads (it is not a preset parameter).
- **Modulation Mode:** SHIFT + Black 6/7 remains inert; routing selection is
  Normal-Mode-only (user-confirmed).

## Verification

- Build the firmware per the repo build workflow (make in `code/src`) and
  confirm SRAM/SDRAM usage is unchanged (one int + one branch; no new
  buffers).
- `make -C code/tests test` passes, including the new `audio_routing_test`.
- On-device checks: both routings at boot and after selection; send level
  independence from the volume knob; headphone return level tracking the
  knob; recording contents per routing; playback audibility with Send FX
  selected; no feedback with the loop patched.
