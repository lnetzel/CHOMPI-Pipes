CHOMPI Hardware UI Map
Hardware-oriented vocabulary for people and coding agents working on CHOMPI. Read positions with the instrument facing the player, keyboard at the bottom. This is a cross-reference, not a specification of what another firmware must do. PIPES assignments below are examples of the current firmware's behavior.

Naming and Evidence
Physical encoder 1-4 means the four upper parameter knobs, left to right. It does not mean ENC_1 through ENC_4 in that order.
SW1-SW6 are the encoder hardware reference names exposed by Hardware::EncoderId. ENC_n is the corresponding encoder net/firmware name.
KEY_n and SW_TOG are hardware signal names exposed by Hardware::SwId. They are not independently verified switch-package silkscreen designators.
LED1-LED10 are the Rev4 PTH PCB references recorded in the firmware's wheel-indicator comment. Individual SMT LED PCB designators are not recorded here because the available source does not establish them.
All array, switch-state, and LED API indices are zero-based. Physical encoder numbers and white/black key numbers in this document are one-based.
PTH n means SetPthLed(n, r, g, b) or SetPthLedFloat(n, r, g, b). SMT n means the equivalent SetSmtLed / SetSmtLedFloat call. These are separate address spaces: PTH 7 and SMT 7 are different LEDs.
Sources: hardware definitions, UI mappings and LED assignments, and LED driver. No schematic/PCB layout is supplied with this reference; verify designators against the board for other revisions.

Rotary Controls
Turning and pressing an encoder are separate inputs. hw.enc[n] identifies rotation; most push switches are read from hw.button_sr, not that encoder's click API. The wheel is the exception.

Human name / aliases	Encoder PCB reference	Encoder net name	Rotation object	Push input	Associated LED	Current PIPES assignment
Physical encoder 1; leftmost pipe knob; 8-foot knob	SW4	ENC_4	hw.enc[3]	SwId::ENC_4_SW (3)	LED2 / PTH 1	Pipe 1: 8'
Physical encoder 2; second pipe knob; 4-foot knob	SW1	ENC_1	hw.enc[0]	SwId::ENC_1_SW (0)	LED3 / PTH 2	Pipe 2: 4'
Physical encoder 3; third pipe knob; 2 2/3-foot knob	SW2	ENC_2	hw.enc[1]	SwId::ENC_2_SW (1)	LED4 / PTH 3	Pipe 3: 2 2/3'
Physical encoder 4; rightmost pipe knob; 2-foot knob	SW3	ENC_3	hw.enc[2]	SwId::ENC_3_SW (2)	LED5 / PTH 4	Pipe 4: 2'
Transport wheel; wheel encoder; encoder 5	SW5	ENC_5	hw.enc[4]	hw.enc[4].RisingEdge() via D10	LED6 / PTH 5 (left), LED7 / PTH 6 (right)	Rotary / vibrato
Volume control; volume knob; volume encoder; encoder 6	SW6	ENC_6	hw.enc[5]	SwId::ENC_6_SW (32)	LED10 / PTH 9	Master volume / distortion / delay / reverb
For portable requests, say "physical encoder 1 (SW4 / ENC_4)" instead of just "encoder 1". Likewise, "physical encoder 1 LED (LED2 / PTH 1)" removes the ambiguity between a physical position and a hardware net number.

Rotation Wiring
Seed pin names below are daisy::seed::D<n>, not MCU package pin numbers.

Hardware reference	Rotation A	Rotation B	Push wiring
SW1 / ENC_1	encoder_sr.RawState(0) / EncoderSrId::ENC_1_A	encoder_sr.RawState(1) / EncoderSrId::ENC_1_B	Button chain U5 slot 1
SW2 / ENC_2	encoder_sr.RawState(2) / EncoderSrId::ENC_2_A	encoder_sr.RawState(3) / EncoderSrId::ENC_2_B	Button chain U5 slot 2
SW3 / ENC_3	encoder_sr.RawState(4) / EncoderSrId::ENC_3_A	encoder_sr.RawState(5) / EncoderSrId::ENC_3_B	Button chain U5 slot 3
SW4 / ENC_4	encoder_sr.RawState(6) / EncoderSrId::ENC_4_A	encoder_sr.RawState(7) / EncoderSrId::ENC_4_B	Button chain U5 slot 4
SW5 / ENC_5	D0	D20	D10, direct encoder click input
SW6 / ENC_6	D15	D17	Button chain U10 slot 1; encoder click pin is unassigned
The four pipe encoders use a separate single-CD4021 rotation chain: clock D22, latch D23, data D19. Its IC PCB reference is not established here.

Function Keys and Toggle
Read key state with hw.button_sr.State(static_cast<int>(Hardware::SwId::<name>)). The numeric index is included for cross-language ports; prefer the enum in C++.

Human name / aliases	Hardware signal	SwId index	Button chain location	Associated LED	Current PIPES assignment
CHOMPI key; SHIFT key; modifier key	KEY_26	5	U5 slot 6	LED1 / PTH 0	SHIFT; recorder/preset status LED
Mode toggle; hardware toggle; switch next to SHIFT	SW_TOG	6	U5 slot 7	No dedicated LED assigned	Down: Normal; up: Modulation
Play/Pause key; PLAY key; percussion key	KEY_27	33	U10 slot 2	LED8 / PTH 7	Percussion; playback transport; recording chord
Loop key; LOOP key; OVERDUB alias in source	KEY_28	34	U10 slot 3	LED9 / PTH 8	Key latch; playback loop; init/recording chords
The toggle is filtered by Hardware::ProcessAllControls() and exposed through GetToggleState(); PIPES applies its up/down interpretation in ModModeSwitchUp(). Do not infer physical direction from a raw input bit.

Panel LED Matrix
These are the 10 PTH RGB LEDs, not the 25 SMT keyboard LEDs. The wheel has two indicators, not a single encoder LED. Names describe physical association; LED colors and functions are firmware-defined.

Human name	Rev4 PCB reference	LED API address	Related hardware signal / encoder
SHIFT LED; CHOMPI key LED; recorder/status LED	LED1	PTH 0	KEY_26
Physical encoder 1 LED; leftmost pipe LED	LED2	PTH 1	SW4 / ENC_4
Physical encoder 2 LED	LED3	PTH 2	SW1 / ENC_1
Physical encoder 3 LED	LED4	PTH 3	SW2 / ENC_2
Physical encoder 4 LED; rightmost pipe LED	LED5	PTH 4	SW3 / ENC_3
Wheel left LED; rotary indicator in PIPES	LED6	PTH 5	SW5 / ENC_5
Wheel right LED; vibrato indicator in PIPES	LED7	PTH 6	SW5 / ENC_5
Play/Pause LED; PLAY LED; percussion indicator	LED8	PTH 7	KEY_27
Loop LED; LOOP LED; latch indicator	LED9	PTH 8	KEY_28
Volume control LED; volume encoder LED	LED10	PTH 9	SW6 / ENC_6
Keyboard Matrix
The keyboard has 15 white keys + 10 black keys = 25 playable keys. KEY_1-KEY_15 are white keys left to right; KEY_16-KEY_25 are black keys left to right. This numbering is not chromatic order and is not the shift-register order. The table is in physical pitch order.

Notes are PIPES boot pitches (MIDI note numbers are authoritative; octave labels differ between applications). Octave shift and other firmware can change the notes without changing this hardware mapping. Each row's LED is an SMT chain address, not a verified PCB LED designator.

Physical key	Hardware signal	Boot note	MIDI note	SwId index	Button chain location	LED API address
White 1 (leftmost)	KEY_1	C3	48	15	U7 slot 8	SMT 24
Black 1	KEY_16	C#3	49	7	U5 slot 8	SMT 0
White 2	KEY_2	D3	50	8	U7 slot 1	SMT 23
Black 2	KEY_17	D#3	51	12	U7 slot 5	SMT 1
White 3	KEY_3	E3	52	9	U7 slot 2	SMT 22
White 4	KEY_4	F3	53	10	U7 slot 3	SMT 21
Black 3	KEY_18	F#3	54	13	U7 slot 6	SMT 2
White 5	KEY_5	G3	55	11	U7 slot 4	SMT 20
Black 4	KEY_19	G#3	56	14	U7 slot 7	SMT 3
White 6	KEY_6	A3	57	16	U8 slot 1	SMT 19
Black 5	KEY_20	A#3	58	21	U8 slot 6	SMT 4
White 7	KEY_7	B3	59	17	U8 slot 2	SMT 18
White 8	KEY_8	C4	60	18	U8 slot 3	SMT 17
Black 6	KEY_21	C#4	61	22	U8 slot 7	SMT 5
White 9	KEY_9	D4	62	19	U8 slot 4	SMT 16
Black 7	KEY_22	D#4	63	23	U8 slot 8	SMT 6
White 10	KEY_10	E4	64	20	U8 slot 5	SMT 15
White 11	KEY_11	F4	65	24	U9 slot 1	SMT 14
Black 8	KEY_23	F#4	66	29	U9 slot 6	SMT 7
White 12	KEY_12	G4	67	25	U9 slot 2	SMT 13
Black 9	KEY_24	G#4	68	30	U9 slot 7	SMT 8
White 13	KEY_13	A4	69	26	U9 slot 3	SMT 12
Black 10	KEY_25	A#4	70	31	U9 slot 8	SMT 9
White 14	KEY_14	B4	71	27	U9 slot 4	SMT 11
White 15 (rightmost)	KEY_15	C5	72	28	U9 slot 5	SMT 10
Shared Wiring and Porting Rules
Interface	Firmware configuration / hardware route
Button input chain	Five CD4021s: U5, U7, U8, U9, U10; clock D8, latch D7, data D9
Button chain slot notation	Slots 1-8 reproduce the source's per-register ordering, not IC package pin numbers; global index = register ordinal (0-4) * 8 + slot - 1
Unused button inputs	Index 4 = NC_6 (U5 slot 5); indices 35-39 = NC_1-NC_5 (U10 slots 4-8)
PTH panel LED output	D16, TIM5 channel 4; 10 physical LEDs; driver emits RGB byte order
SMT keyboard LED output	D18, TIM3 channel 2; 25 physical LEDs; driver emits GRB byte order
Physical pipe order is pipe_encoder = {3, 0, 1, 2}. Never replace it with {0, 1, 2, 3} merely because the knobs look sequential.
The historical led_map mixes associations for different LED chains. Only its playable-key entries (switch indices 7-31) are SMT keyboard addresses. For function keys and encoders, use the PTH matrix above.
The historical key_map comment at index 4 says ENC_5_SW, but the active SwId enum marks that slot NC_6. The wheel push is direct D10, not index 4.
LED API indices exclude DMA porch padding. kNumPthLeds and kNumSmtLeds include two six-slot porches (22 and 37 buffer slots); these are not counts of physical LEDs. Use indices 0-9 and 0-24 respectively for real LEDs.
Update/debounce inputs before reading them and flush LED changes through fill_led_data() using the existing driver lifecycle when reusing this HAL.
Power-management controls, connectors, and any non-addressable indicators have no complete physical-control/designator mapping in these sources. Consult the target PCB schematic rather than inventing references for them.
Suggested agent instruction: "Use HARDWARE_UI_MAP.md as the hardware naming reference. Distinguish physical position, PCB reference, signal name, array index, and LED chain. Preserve physical mappings when changing UI behavior."
