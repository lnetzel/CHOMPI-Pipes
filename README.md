# CHOMPI-PIPES (chompi-pipes)

Polyphonic organ firmware for the CHOMPI hardware: 6 voices x 4 pipe
registrations (8', 4', 2 2/3', 2') with per-pipe levels, per-pipe
tonewheel-style Character, Hammond-style percussion on the Play/Pause
key, fuzz distortion with a tone control, a rotary-speaker stage, a
warm stereo BBD-inspired delay, a compact vintage-1960s-inspired plate
reverb, global 5 Hz pitch vibrato, a Modulation Mode that drifts
each pipe's volume and Character and pitch slowly and independently, and 15
SD-card preset slots.
Included is also a simple recorder where you can record jams directly to the SD card as 48kHz, 16 bit *.wav files.

Stereo AUX IN is always mixed with the organ on the headphone and master
outputs and included in SD recordings. AUX bypasses the organ effects;
the master volume controls the combined listening level, with no separate
monitor mix. Set the AUX level on the connected source and lower it if
the combined output clips.

## Controls

The hardware toggle switch next to the SHIFT (CHOMPI) key selects the
mode: **DOWN = Normal Mode** (all controls below), **UP = Modulation
Mode** (shared drift-amount destination, see the Modulation Mode
section). The switch changes what the four pipe encoders turn/push and
what their knob LEDs show, and Modulation Mode adds its own SHIFT key
gestures; preset management and octave selection are Normal-Mode-only.
Everything else behaves the same in both modes.

Every active rotary parameter uses 32 accepted encoder ticks across 33
positions (0 through 32), with no acceleration or wrap. MIDI addresses this
same 0..32 grid. Pipe length retains all positions while snapping the sound
to five octave settings, so some ticks advance without changing pitch.

### Pipe encoders - Normal Mode (physical knobs 1-4, left to right)

Each pipe encoder has three independent pages; press the encoder's push
switch to cycle that pipe through them 1 -> 2 -> 3 -> 1 (pages are
per-pipe, not global, and are preserved across mode changes):

- **Page 1 - pipe level** (white knob LED): 32 steps, gain = (step/32)^2.
  Boot default is step 16 (50%). The white-key bar shows the level with a
  green/orange/red position gradient.
- **Page 2 - Character** (blue knob LED, brightness tracks the amount with
  a floor so the page stays visible at 0): 32 positions of tonewheel-inspired
  harmonic coloration. Step 0 is the exact clean sine; step 32 is clearly
  aged but still organ-like. On the 8' pipe the added harmonics are
  progressively boosted below ~400 Hz (full at 150 Hz), so low notes gain
  extra grit and definition as Character rises - the same low-end
  reinforcement real organs get from quint ranks. The white-key bar is
  uniformly blue.
- **Page 3 - pipe length** (yellow knob LED, brightness tracks the
  |octave| with a floor so the page stays visible at the default): 32
  positions span two octaves each way from nominal, snapping to five
  sounding octaves at positions 0, 8, 16, 24 and 32. Position groups
  0-3, 4-11, 12-19, 20-27 and 28-32 select -2, -1, 0, +1 and +2
  octaves respectively. Positions are retained per pipe, so ticks within
  a group still count and SHIFT broadcasts the resulting position. The
  pipe reaches 16' and
  32' turning left, 4' and 2' turning right. The white-key bar shows
  the offset as a 3-key yellow group: keys 1-3 = -2 octaves, 4-6 = -1,
  7-9 = the default length, 10-12 = +1, 13-15 = +2. Held notes move to
  the new pitch click-free, the 20 kHz foldback applies after the
  offset (like a real organ), and the percussion follows the 2 2/3'
  pipe's length. Pipe lengths boot at nominal footage and are stored in
  the SD-card presets; CC 90-93 address all 33 positions.

The attack and release times are edited in Modulation Mode: select the
attack or release destination there (see the Modulation Mode section)
and the pipe encoders edit the actual envelope times - 32 log-spaced
steps from 5 ms (step 0, boot default - the original fixed timing) to
10 s (step 32); the 5 ms release floor keeps the release click-free.
The white-key bars are uniformly purple (attack) / yellow (release).

Each pipe has its own gate envelope, so pipes fade in and out at
different rates - e.g. let the 2' pipe swell up seconds after the 8'
fundamental speaks. A voice stays allocated until every pipe's release
has finished; voice stealing keeps the fixed 5 ms steal fade. Attack and
release boot at step 0 (5 ms), are stored in the SD-card presets (see
the Presets section) and use CC 78-85.

Each pipe has its own Character amount, applied to that pipe across all
six voices; adjusting one pipe never changes the others' amounts or
spectra. Voicing varies subtly and deterministically by note and pipe
(+/-15% harmonic strength), so repeated pitches on a pipe sound identical
regardless of which voice slot renders them. Character is pickup/
tonewheel-*inspired* coloration - it is not an accurate physical Hammond
model and does not simulate neighboring-tonewheel leakage. Muted pipes
and finished envelopes stay silent; there is no idle background tone.

Character amounts boot at step 0 (the exact clean sine) and are stored
in the SD-card presets (see the Presets section).

SHIFT adds two pipe-control gestures on the pipe encoders:

- **SHIFT + press** toggles that pipe's mute (the page does not cycle).
  A muted pipe is silenced click-free (its level step is preserved and
  restored on unmute), and its knob LED goes solid red, overriding the
  page color. Mute boots unmuted, is stored in the SD-card presets and
  has no MIDI CC; level CC 70-73 still edits a muted pipe's step, which
  stays silent until unmuted.
- **SHIFT + turn** sets the value for all four pipes at once, per the
  turned pipe's current page: page 1 sets all four levels to the same
  value, page 2 all four Character amounts, page 3 all four pipe
  lengths. The white-key bar shows the value all four pipes now share.

### Pipe encoders - Modulation Mode (toggle switch UP)

In Modulation Mode PIPES drifts on its own: each pipe's volume,
Character and pipe length slowly and independently wander around their
Normal Mode settings, and the distortion drive wanders around its set
level too, giving PIPES a living, organic movement. The
four pipe encoders all edit ONE shared destination, selected with
SHIFT + a physical black key (the black keys are numbered left to
right):

- **Black key 1 (C#3) - attack time** (purple): the actual pipe attack
  envelope time, 32 log-spaced steps from 5 ms to 10 s.
- **Black key 2 (D#3) - release time** (yellow): the actual pipe
  release envelope time, the same 32-step 5 ms..10 s log mapping.
- **Black key 3 (F#3) - volume modulation amount** (white): 32 steps,
  0-100% (~3% per click), clamped, no wrap.
- **Black key 4 (G#3) - Character modulation amount** (blue): 32
  steps, same mapping.
- **Black key 5 (A#3) - pipe-length modulation amount** (yellow): 32
  steps, same mapping. The pipe's pitch drifts in a symmetric window of
  up to +/-20 cents (at 100%) around its set length. Each pipe drifts
  at its own rate (8' slowest, 2' fastest), so the four pipes never
  move in step.
- **Black key 8 (F#4) - distortion modulation amount** (red): 32
  steps, same mapping. The distortion is shared by all four pipes, so
  this is ONE global amount, not per pipe: only the leftmost pipe
  encoder adjusts it (the other three knobs do nothing), and
  SHIFT + turn behaves the same as an unshifted turn. The drive drifts
  around the level set on the volume encoder's overdrive page; at 0%
  drive the window slides up, so the distortion drifts in and out of
  audibility.

Black keys 6, 7, 9 and 10 select nothing. Volume modulation is
selected at boot; the selection is remembered across mode changes until
reboot and is not stored in the presets. The per-pipe modulation amounts
use CC 94-105, with global drive and delay amounts on CC 115-116.

The six destination keys are always lit in Modulation Mode - the
selected key at 100% of its destination color, the other five at
exactly 75% - with or without SHIFT held, during value bars and during
the distortion-tone display; every other black key stays dark, even
while its key plays a note. A turn adjusts the selected parameter for
that pipe and shows the matching white-key value bar in the
destination's color for 500 ms. The knob LEDs use the selected destination's
color with brightness = 0.2 + 0.8 x that pipe's value (the floor keeps
the encoder visible at 0%); a muted pipe's knob LED stays solid red.
Unshifted encoder presses do nothing in Modulation Mode. SHIFT + turn
applies the delta to the turned pipe and broadcasts the resulting value
to all four pipes, like in Normal Mode. SHIFT + press still toggles the
pipe's mute in both modes.

With SHIFT held in Modulation Mode the white keys still play notes
(preset load/save and octave changes are Normal-Mode-only); without
SHIFT all 25 keys play as usual.

the modulation works in each parameter's normalized domain: volume
x = level/32 (the squared gain curve is applied to the modulated
value), Character a = position/32 (mapped to the shaper's 0..64 DSP
range before its smoothstep easing), drive
d = step/32 (shared by all pipes). The
amount scales a window around the user value: at 100% the window is
+/-20% (40% total); the window slides - never clips - to stay inside
0-100%, so a 95% center swings 60-100% and a 0% center swings 0-40%
(a silent pipe becomes audible). Pipe length instead drifts the pitch
directly: a symmetric window of up to +/-20 cents (at 100%) around the
set footage, phase-continuous and click-free. A muted pipe stays
silent. Amount 0 is exactly the unmodulated sound.

Each of the thirteen modulators (4 pipes x volume/Character/pipe
length, plus one global instance for the distortion drive)
is a slow,
free-running raised-cosine swing: every half-cycle draws its own
random 2.5-10 s duration, so a full bottom-to-top-to-bottom cycle
takes 5-20 s, is asymmetric, and never repeats in lockstep with the
other pipes. The modulators run continuously regardless of mode or
amount - flipping the switch or turning an amount never restarts
them; the modulation amounts only scale how much of the ongoing drift
reaches the sound.

All modulation amounts boot at 0 (PIPES sounds exactly like Normal
Mode until amounts are raised) and are stored in the SD-card presets.
Per-pipe amounts use CC 94-105; global drive and delay amounts use CC
115-116. The Normal Mode bars and MIDI CCs keep
showing the un-modulated center values.

### Transport wheel (ENC_5)

The wheel controls one of two effects; pressing the wheel toggles the
assignment (boots on rotary). The two LEDs at the wheel show the
selection steadily until the next toggle: left LED purple = rotary,
right LED pink = vibrato. Turning the wheel changes only the selected
effect and shows its white-key value bar for 500 ms.

- Rotary-speaker amount: 32 steps; 0 is an exact bypass and boots at
  0. Purple bar.
- Vibrato depth: 32 positions across 0-50 cents peak deviation from one
  shared, free-running 5 Hz LFO across all voices (depth 50 = one
  semitone peak-to-peak); 0 is an exact bypass and boots at 0. Pink bar.
  The depth is stored in the SD-card presets.
- Vibrato note-on delay: with vibrato selected on the wheel, SHIFT +
  turn sets 32 positions across 0-5 s (clamped, no wrap). When a
  note is pressed from an all-notes-released state the vibrato stays
  off for the delay time, then fades in over a fixed 250 ms ramp up to
  the set depth; notes pressed while other notes are held do not
  restart it. Pink bar. 0 (boot and init patch) starts the vibrato
  with the note. Both controls are stored in presets and addressed by CC 1 / 106.

### Volume encoder (ENC_6)

Push cycles four pages: master volume (page 1, gradient LED) ->
distortion amount (page 2, red LED with brightness floor; 0 is an exact
bypass) -> stereo delay mix (page 3, yellow LED with brightness floor)
-> plate-reverb mix (page 4, blue LED with brightness floor).
Turning or pressing the encoder shows the current page's white-key value
bar for 500 ms.

Page 2's distortion is a fuzz with a tone control. SHIFT + turn on
page 2 changes the tone through 32 positions across 0-100% (no wrap);
the amount is not changed. The tone sweeps the
cutoff of the fuzz's post low-pass - two identical cascaded one-pole
stages with an approximately 12 dB/octave stopband roll-off -
logarithmically from 500 Hz (0%, dark) to 10 kHz (100%, boot default).
The mapped frequency is the per-stage cutoff; the combined response's
-3 dB point sits lower, and 100% is intentionally a little darker than
the previous single-pole fuzz. Counterclockwise darkens, and 0% is not
a mute. Tone changes are smoothed click-free (~10 ms). While SHIFT +
turning, and for 500 ms after the last click, the tone shows as a red
bar across the 15 white keys (none lit at 0%, eight at 50%, all
fifteen at 100%); the tone bar takes priority over any pipe value bar
while it is active, and in Modulation Mode the destination-key LEDs
stay visible. Holding SHIFT on page 2 in Normal Mode shows the preset
slot overview like on any other page. The tone boots at position 32 and
is stored in presets; CC 109 addresses its 33 positions.

To keep the perceived loudness even at any distortion amount, an
automatic output leveling trims the fuzzed signal after the rotary and
before the plate reverb, so the leveled level also feeds the reverb. The
fuzz is a hard limiter - its output level is nearly constant at high
drive - so a fixed trim cannot match every registration: instead, the
dry and fuzzed signal levels are measured per audio block, smoothed over
~50 ms, and the fuzzed level is matched down to just above the dry
level (+1.5 dB headroom, so the damping reduces that much less; never
boosted, never more than -30 dB, exactly unity for the clean sound).
Quiet single notes get a deep trim, loud full chords get barely any;
clean and distorted sounds keep the same power. The trim is folded
through the fuzz's own wet/bypass fade, so it ramps click-free and is
exactly unity at settled bypass; it introduces no user control, and the
main-volume value and display are unaffected.

Page 3 adjusts the stereo delay with one combined mix/feedback
control: 32 positions across the existing 0-100% curve, with position
16 at the 50% pivot. The lower half of the bar
(0-50%) sweeps the dry/wet mix from 0% to 50%: 0% is exact dry (a
linear complementary crossfade with a ~10 ms click-free ramp). Above
50% the mix locks at 50% and the delay feedback rises from 35% to
120%: around 100% the echoes ring for many seconds, and past that
(the top ~12% of the bar) the delay self-oscillates - a bounded, dark
feedback drone whose pitch follows the delay time, with plenty of
sweet spots to play. Turning back down dials the feedback back to 35%
and below the 50% point goes back to controlling the mix. The
white-key bar is
uniformly yellow: no keys at 0%, half (8 keys) at 50%, all 15 at 100%.

SHIFT + turn on page 3 adjusts the delay time across 32 positions from
40-500 ms (boot default position 15, 256 ms), shown on the same yellow white-key
bar while turning (40 ms = no keys, 500 ms = all 15). Time changes slew
the read position smoothly, giving the pitch-gliding transitions of an
analog BBD clock change; the echo history is never cleared.

The delay is a fixed BBD-inspired voicing apart from the feedback:
two independent delay lines with a subtle fixed +/-0.5% left/right
timing difference plus slow independent clock wobble, so the stereo
image is preserved and gently widened - stereo, not ping-pong. Repeats
run through a darkening low-pass (each repeat darker than the last),
with a DC blocker in the loop and a gentle bounded saturation. The
feedback is 35% by default and rises to 120% in the upper half of the
page-3 control: past ~100% the delay self-oscillates into a drone
that never decays until the feedback is turned down; the bounded
saturation keeps even that from running away. There is no hiss or
clock noise.
The buffers keep running at 0% mix and across page/preset changes, so
tails ring out naturally and old echoes survive a mix reopening; loading
a preset ramps the mix/time without clearing them.

The delay mix/feedback value and time boot at 0% / 256 ms and are
stored in the SD-card presets; CC 113-114 address the shared 0..32 grid.

Page 4 adjusts the reverb mix in 32 steps (one per click, the same
granularity as the overdrive amount): step 0 is exact dry and step 32
is wet only (a linear complementary crossfade with a ~10 ms click-free
ramp). The white-key bar is uniformly blue: no keys at 0, half (8 keys)
at step 16, all 15 at step 32.

SHIFT + turn on page 4 adjusts the plate decay: 32 log-spaced positions
from 0.2 s (boot default) to 20 s, shown on the same blue white-key bar
while turning. The wet level and tank headroom are normalized across the
range, so the decay changes the tail duration, not the loudness.

The plate is a fixed voicing apart from the decay (no tone/predelay
controls): a stereo-input Griesinger/Dattorro tank adapted from
chompi-wave's reverb. Each channel gets its own send and allpass
diffuser chain (different lengths per channel) and excites its own side
of the tank, so the rotary speaker's left/right movement survives in
the reverb; the tank's decay-scaled cross-feedback then progressively
couples the two sides into one diffuse field (well separated at short
decays, blended at long ones). Voiced dark (~4.5 kHz damping) with
subtle slow modulation and a ~120 Hz low-cut per send. Plate-inspired;
not an accurate physical EMT emulation. The tank runs continuously, so
tails ring out naturally after note release and survive page changes;
turning the master volume to zero silences dry and wet alike (the
reverb sits before the master gain).

The mix and decay boot at 0% / 0.2 s and are stored in the SD-card
presets; CC 110-111 address their 0..32 positions.

SHIFT + turn = fine tune (+/-50 cents across 32 positions, center 16) on page 1 only (page 2 adjusts
the distortion tone, page 3 adjusts the delay time, page 4 adjusts the
reverb decay). The fine-tune bar centers
on white key 8: exactly centered is that key alone in green, and any
offset spans red from the center toward the offset (left = down in
pitch, right = up; all seven keys on that side at +/-50 cents). Boot
default volume is 50%.

### Keys

25 playable keys (C3-C5). SHIFT + C#3/D#3 = octave down/up (+/-2,
Normal Mode only - the octave never changes while the switch is UP).
KEY_28 (LOOP) toggles the key latch on release; while latched,
re-pressing a key releases its note. SHIFT + LOOP does not toggle the
latch - it starts the init-patch hold (see the Presets section).
KEY_26+27+28 held at power-on enters shipping mode. With SHIFT held in
Normal Mode the white keys become the 15 preset slots (see the Presets
section); in Modulation Mode SHIFT + a black key selects the
modulation destination and the white keys keep playing.

KEY_27 (Play/Pause) toggles the percussion on release, in both modes;
the key's LED is solid amber while percussion is enabled. Holding
PLAY + LOOP records to the SD card (see the Recording section).

### Percussion (KEY_27)

A Hammond-inspired percussion voice: while enabled, a short decaying
burst of the 2 2/3' pipe (the third-harmonic-like registration) is
added to every sounding voice - a fixed, fast pluck with a ~1 ms attack
and a 60 ms half-life exponential decay (gone by ~600 ms). It uses the
existing pipe oscillator tapped before its level, Character and
envelope, so it adds almost no CPU cost and is completely independent
of the pipe levels, mutes, Character amounts and the long pipe
attack/release settings; it still follows pitch, fine tuning, foldback,
vibrato, velocity and the master gain, and passes through the fuzz,
rotary, delay and reverb like everything else. Hammond-inspired, not a
complete B3 circuit emulation.

The triggering is the classic Hammond single-trigger: the decay only
restarts on a note-on when every note was released - legato notes and
extra chord notes share the remaining tail instead of restarting it.
Latched and MIDI-held notes count as held, so releasing physical keys
while the latch sustains them does not rearm the percussion; voice
stealing and long release tails do not rearm it either. Toggling
percussion on while notes are held does not synthesize an attack - the
next note after a full release gets the ping. Toggling off fades the
burst out in ~5 ms.

Percussion boots off, is stored in the SD-card presets (older presets
without the field load as off) and uses MIDI CC 112. While it is enabled
the dry mix is normalized for the extra voice content (about 1 dB of
attenuation, ramped click-free with the toggle); settled-off renders
exactly like the percussion-less firmware.

## SD-card presets

PIPES keeps 15 preset slots on the SD card (folder `PIPES/`, one
small `P01.json` .. `P15.json` file per slot, ~300 B, human-readable).
All 15 slots are cached in RAM: loading, saving, copying and deleting
act on the cache instantly and never wait for the card, and the
background writer does one small step per main-loop pass, so nothing
freezes or glitches the audio. A save is written as a `.tmp` file that
is then renamed over the old file, so a power loss mid-save can corrupt
at most the slot being written. PIPES always boots on its factory
defaults; a preset is only applied when you load it.

Preset schema version 2 stores rotary controls as 0..32 positions,
including retained pipe-length positions. Version-1 files (including files
without a version field) are converted in RAM to the nearest position and
are not rewritten or marked dirty by loading. A future unsupported version
is left unloaded and its slot blinks red in preset selection. An explicit
save or copy writes version 2. Back up the `PIPES/` preset folder before
upgrading: older firmware cannot safely read version-2 files, so downgrade
requires restoring the version-1 backup. Existing automation retains the
same endpoints and approximate proportions, but values quantize to the new
33-position grid.

Gestures (SHIFT = the CHOMPI key). Load, save, copy, delete and their
confirmations are Normal-Mode-only: entering Modulation Mode cancels
any unconfirmed selection, while an already-confirmed write finishes
in the background without taking over the Modulation-mode LEDs.

- **Load:** hold SHIFT and press a white key (C3-C5 = slots 1-15). A
  filled slot applies instantly and becomes the current slot; an empty
  slot does nothing. Held and latched notes keep sounding - every
  change is ramped or phase-continuous.
- **Save:** SHIFT + KEY_25 enters save select (pressing KEY_25 again
  cancels). Press any white key to choose the slot, then release and
  press SHIFT to confirm. The current engine state is captured, the
  SAVING animation shows for ~1 s and the slot becomes the current slot.
- **Copy:** SHIFT + KEY_24 enters copy select (pressing KEY_24 again
  cancels). Press a filled source slot (it turns green), then a
  different destination slot, then release and press SHIFT to confirm.
- **Delete:** SHIFT + KEY_23 enters erase select (pressing KEY_23 again
  cancels). Press a filled slot, then release and press SHIFT to
  confirm. Deleting the current slot clears "current".
- **Init:** hold SHIFT, then press LOOP and keep both held for 2.5 s.
  A yellow bar fills the 15 white keys while you hold; releasing either
  key early cancels. At 2.5 s the init sound is applied live (held and
  latched notes keep sounding) and the CHOMPI key LED blinks white for
  ~1 s. Init needs no SD card. Init values: pipe levels 16 (50%),
  Character 0 (clean sine), pipe length position 16 (nominal footage), attack
  and release 0 (5 ms), mutes off,
  volume/Character modulation 0, vibrato depth and note-on delay 0,
  rotary and drive 0,
  distortion tone position 32 (100%), reverb mix 0, reverb decay 0 (0.2 s),
  percussion off, delay mix 0% and delay time position 15 (256 ms) - the
  same sound as power-on. Not touched: master volume, fine tune,
  octave, latch state, page selections and the wheel mode. After init
  no slot is "current" (no white slot in the SHIFT slot view).

While a selection mode is active the keys select slots only (no notes,
no latch changes); notes held before entering keep sounding and release
normally, and the encoders, wheel and MIDI keep working throughout.

LEDs, while SHIFT is held or a selection mode is active:

- The 15 white keys are the 15 slots: filled = purple (solid in save
  select / copy-destination select, blinking dim in erase select /
  copy-source select), the current slot = white (solid in save /
  copy-destination select, blinking in erase / copy-source select),
  empty slots blink dim grey in save select / copy-destination select
  and stay off in erase select / copy-source select, the selected slot
  = blue (save / copy destination) or red (erase target), the copy
  source = green.
- The three top black keys show the operations: delete (KEY_23) = red,
  copy (KEY_24) = green, save (KEY_25) = blue; during an operation only
  that operation's LED is lit.
- The CHOMPI key LED is purple while SHIFT is held, blinks red once a
  slot is selected, and blinks white during the ~1 s SAVING / COPYING /
  ERASING animations.

With no SD card the white keys show red while SHIFT is held and all
preset gestures are inert; PIPES stays fully playable. Removing the
card mid-session disables the presets; re-inserting it re-mounts the
card and fills still-empty, untouched slots from it. A card pulled
mid-write can leave a stray `.tmp` file, which is ignored.

Saved in every preset: per-pipe level, Character, pipe length, attack,
release, mute and volume/Character/pipe-length-modulation amounts,
plus vibrato depth and note-on delay, rotary amount, fuzz amount and
tone, the global distortion modulation amount, reverb mix
and decay, percussion on/off, and delay mix and time. NOT saved:
master volume, fine tune, octave shift, latch, page selections, the
Modulation-mode destination and the wheel's rotary/vibrato selection.

## Recording to SD

PIPES records the full organ audio chain - pipes, fuzz, rotary, delay and
reverb - plus dry stereo AUX IN to 48 kHz / 16-bit stereo WAV files on the SD card
(`PIPES/RECORDINGS/Recording 01.wav`, `Recording 02.wav`, ...). The
tap sits BEFORE the master volume knob at a fixed -6 dB level, so
takes land DAW-ready no matter where the knob is; the headphone and
master outputs are unchanged.

- **Start:** hold PLAY + LOOP together for 2 s (Normal Mode only, no
  SHIFT). A red bar fills the 15 white keys while you hold; releasing
  either key early cancels. Then a 4 s countdown runs on the first
  four white keys (all four light white; every 1 s the next one turns
  red) - a fresh PLAY or LOOP press during the countdown cancels.
  Recording then starts and the instrument returns to normal play.
- **While recording** the CHOMPI key LED blinks red. SHIFT keeps
  working normally (the LED stays purple while SHIFT is held), and
  PLAY/LOOP still toggle the percussion and the latch.
- **Stop:** hold PLAY + LOOP together for 1 s (works in both modes).

The percussion and latch toggles moved from the press edge to the
release edge so the chord is deterministic: a completed chord never
toggles anything, and an aborted hold longer than ~0.4 s is not two
quick presses either.

Takes are numbered with the next free number - `Recording 01.wav` ..
`Recording 99.wav`, then plain `Recording 100.wav`, `101.wav`, ... up
to 999 - and an old take is never overwritten. One take can be ~6 h
long (the 4 GiB FAT32/WAV limit stops it automatically). A power cut
mid-take loses at most ~1 s of audio: the take is synced to the card
once per second, and only a clean stop patches the final length into
the WAV header (tolerant players still open an unpatched file). With
no SD card the chord just flashes the four countdown keys red and
stays inert; a full card, a failed write or every take number taken
blinks the CHOMPI key LED red fast for ~2 s and nothing is recorded.
Recordings are session-scope: no preset stores them and there is no
MIDI control.

## Playback mode

Playback mode plays the SD takes (`PIPES/RECORDINGS/Recording NN.wav`)
straight from the card, one at a time - pure playback: the take
bypasses every FX stage (it is injected after the record tap, just
before the master volume knob, so the volume knob still scales it),
all 25 keys are inert as notes, MIDI In is ignored and recording is
fully blocked.

- **Enter/exit:** hold PLAY for 2 s (PLAY alone, Normal Mode only -
  PLAY + LOOP stays the record chord). A blue bar fills the 15 white
  keys while you hold; releasing early cancels. Entering silences
  every note, clears the latch and stops a running take. Exit with
  the same gesture again, or flip the toggle switch either way. With
  no SD card (or no takes) the white keys flash red for ~1 s and the
  mode stays off.
- **Take selection:** the newest 15 takes sit on the 15 white keys,
  C3 = newest. A white-key press directly selects its take; the
  selected key's LED is blue (solid while paused, blinking while
  playing), the unselected takes glow dim white, empty slots stay
  off.
- **Transport:** PLAY taps pause/resume (the PLAY key LED mirrors the
  transport in blue), LOOP taps loop the take (the LOOP key LED is
  solid blue while the loop is on). With loop off a finished take
  advances to the next-newer one and wraps from the newest back to
  the oldest of the 15. Take switches and the loop are gapless.
- **Knobs:** the master volume knob keeps working (on every page);
  every other knob is inert.

A take that fails to open or is not a PIPES WAV is skipped with a
brief red flash on its slot; if every take fails, or the card is
pulled mid-playback, the mode exits with a ~2 s red flash.

## MIDI (channel 1, DIN and USB)

Features a full midi implementation for all preset parameters. read more in [midi.md](midi.md)

## Signal flow

pitch vibrato (shared 5 Hz LFO at the oscillators) -> sine -> Character
shaping (per pipe) -> pipe gain -> voice envelope/velocity -> voice mix
(the percussion taps the raw 2 2/3' pipe sine before Character/gain/
envelope and joins the mix through its own shared decay)
-> fuzz distortion (tone-controlled) -> rotary
speaker -> stereo BBD-style delay -> plate reverb -> master gain (with the automatic fixed fuzz
output trim) -> headphone + master stereo
output pairs. Character 0 exactly bypasses shaping; the clean-sine
engine is bit-identical to the previous firmware's sine-only sound.
Reverb 0% is bit-exact dry; vibrato depth 0 is an exact bypass;
delay 0% is bit-exact dry; percussion off is an exact bypass with the
original 1/24 mix normalization.

## Building on Windows

You need:

- **MSYS2** (https://www.msys2.org) for a shell and GNU Make.
- **Arm GNU Toolchain 10.3-2021.10** (`arm-none-eabi-gcc` 10.3.1), installed to
  `C:\armgnu\10.3-2021.10`. Use exactly this version: the prebuilt libraries
  and the SRAM budget are tuned to it, and newer compilers can overflow SRAM.

Everything else is vendored in this repo: libDaisy, DaisySP and coreJSON
live in `code/libs/` (with the prebuilt `libdaisy.a`/`libdaisysp.a`
committed), so a fresh clone builds with no other checkouts.

Open an **MSYS2 UCRT64** shell, then install Make once:

```sh
pacman -S --needed make
```

In each new shell, put the toolchain on the `PATH`, then build from
`code/src`:

```sh
export PATH="/c/armgnu/10.3-2021.10/bin:$PATH"
cd /path/to/CHOMPI-Pipes/code/src
make check-toolchain   # prints GCC 10.3.1 if the toolchain is right
make -j4 DEBUG=0 OPT=-O3
```

For the audio-CPU meter build (peak block load on PTH LED 0: green
< 70%, amber 70-85%, red > 85%):

```sh
make -j4 DEBUG=0 OPT=-O3 PIPES_CPU_METER=1
```

The output is `code/src/build/CHOMPI_PIPES_1.0.bin` (BOOT_SRAM app type). The
build links the prebuilt, committed `code/libs/libDaisy/build/libdaisy.a`
and `code/libs/DaisySP/build/libdaisysp.a`. To rebuild those libraries from
source instead, run `make` once in `code/libs/libDaisy` and
`code/libs/DaisySP` (with the same toolchain on the PATH).

To install it, copy `CHOMPI_PIPES_1.0.bin` to the root of the SD card
(deleting any other `.bin`), put the card in CHOMPI and power on; do not
flash the bootloader. If the board has never had the CHOMPI bootloader,
install it first with `make program-boot` from `code/src` (this flashes
`bin/CHOMPI_Bootloader_V6_2_0.bin` via DFU).

## Tests

Native mapping, preset, engine and DSP tests live in `code/tests/`:

```sh
make -C code/tests test
```

They need only a host C++ compiler (no MCU/Daisy dependencies). The
Character tests cover step clamping/guards, exact clean endpoints, ramp
convergence and retargeting, voice-slot-independent voicing, cycle-mean
DC, 2nd/3rd harmonic content, amplitude bounds, and high-frequency
anti-alias suppression. The reverb tests cover deterministic
initialization (garbage-prefilled delay buffer), bounded decaying
impulse response, mix clamping, ramp convergence/retargeting, bit-exact
dry at 0%, wet-only 100% against a reference instance, long-silence
stability, and the stereo tank behavior (distinguishable L/R paths with
decay-scaled cross-coupling, decorrelation of identical L/R content,
approximate L/R symmetry, and bounded anti-phase response). The
distortion tests cover the fuzz's tone control (endpoints,
monotonic darkening, click-free ramps), a bit-exact regression of the
two-stage low-pass cascade against an independent reference
implementation, the measured frequency response against the analytical
digital cascade magnitude (2-4 kHz roll-off at 48 kHz and 32 kHz),
exact dry bypass at every tone, and the input-referenced fuzz output
leveling (dry-matched loudness with +1.5 dB headroom, -30 dB floor). The drift-modulator tests cover the sliding-window math
(including the amount-0 exact-center guarantee), the 2.5-10 s random
half durations and 5-20 s cycles, per-block continuity, modulator
independence across the 8 seeds, and fixed-seed determinism at 48 kHz
and 32 kHz. Parameter-step tests exhaust MIDI inputs and 33-position
round trips. Preset tests cover v1 migration and v2 JSON round-trips,
defaults for missing keys, clamping of out-of-range and garbage values,
unsupported-version rejection, the 15-slot bank's save/copy/erase
and dirty tracking, engine capture/apply against a fake engine, and
the init-patch values. The Modulation-mode UI tests cover the physical
black-key destination mapping, the boot default and remembered
selection, the exact 75%/100% destination-key intensities and colors,
the three-page Normal-mode cycling, the encoder press behavior, and the
SHIFT broadcast clamping. The percussion tests cover the helper timing
(sample-rate-independent 60 ms half-life, 1 ms attack, 5 ms fades,
exact settle-to-zero) plus real OrganEngine integration renders:
single-trigger rearm across all-notes-released only, legato/chord tail
sharing, latch-style held-note bookkeeping with duplicate events, no
attack when enabled under held notes, release/steal/toggle gate fades,
the 2 2/3' harmonic content, independence from pipe level/mute/
Character, the 24 -> 27 mix normalization, bounded full-scale output
and bit-exact settled-off equivalence. The delay tests cover buffer
clearing and deterministic reinitialization, mix/time clamping and
defaults, bit-exact dry bypass, wet-only behavior with no dry
feedthrough, mix-ramp and time-slew convergence/retargeting, echo
arrival time, ~35% repeat decay with progressively darker repeats,
stereo (non-ping-pong) separation, ring wrap at 500 ms, history
surviving bypass and time changes, in-place processing and bounded
no-self-oscillation behavior. The recorder tests cover the
float-to-16-bit conversion clamping, the record ring's wrap,
full-refusal and SPSC ordering, the next-free-take-number rule with
the 99 -> 100 rollover and the take name formatting, the canonical
WAV header bytes and patched sizes, and the per-take byte-limit
auto-stop. The player tests cover the 16-bit-to-float round-trip
against the recorder's converter, the stereo deinterleave, the WAV
header validation accept/reject table (foreign files are refused),
the newest-first take window with gappy numbering and the slot
mapping, and the next-slot advance with the wrap at the newest take.

## Notes from the creator
While wanting to create a real instrument this was also an experiment to fully "vibe code" with Github Copilot in VS Code. It therefore comes with all the "downsides" vibe coding generates (code that is sometimes hard to read and maintain and tons of comments in the code)

Also, I also want to give a shout out to the wonderful people of [CHOMPI Club](https://github.com/CHOMPI-Club/CHOMPI) for making CHOMPI firmwares available for the public to explore. 
