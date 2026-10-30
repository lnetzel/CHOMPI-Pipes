# Native DSP tests for chompi-pipes (host compiler only, no MCU/Daisy deps).
# Usage: make -C code/tests test

CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -std=c++14 -O2 -Wall -Wextra -I../src

# coreJSON is vendored in this repo's code/libs (needed by OrganPreset.h)
COREJSON_DIR = ../libs/coreJSON/source

parameter_steps_test: parameter_steps_test.cpp ../src/ParameterSteps.h
	$(CXX) $(CXXFLAGS) -o $@ $<

test: parameter_steps_test tonewheel_character_test plate_reverb_test distortion_test vibrato_test envelope_map_test drift_modulator_test organ_preset_test mod_dest_test percussion_test stereo_bbd_delay_test sine_lut_test sd_recorder_test sd_player_test midi_cc_map_test audio_routing_test
	./parameter_steps_test
	./tonewheel_character_test
	./plate_reverb_test
	./distortion_test
	./vibrato_test
	./envelope_map_test
	./drift_modulator_test
	./organ_preset_test
	./mod_dest_test
	./percussion_test
	./stereo_bbd_delay_test
	./sine_lut_test
	./sd_recorder_test
	./sd_player_test
	./midi_cc_map_test
	./audio_routing_test

tonewheel_character_test: tonewheel_character_test.cpp ../src/TonewheelCharacter.h
	$(CXX) $(CXXFLAGS) -o $@ $<

plate_reverb_test: plate_reverb_test.cpp ../src/PlateReverb.h
	$(CXX) $(CXXFLAGS) -o $@ $<

distortion_test: distortion_test.cpp ../src/Distortion.h
	$(CXX) $(CXXFLAGS) -o $@ $<

vibrato_test: vibrato_test.cpp ../src/Vibrato.h
	$(CXX) $(CXXFLAGS) -o $@ $<

envelope_map_test: envelope_map_test.cpp ../src/EnvelopeMap.h
	$(CXX) $(CXXFLAGS) -o $@ $<

drift_modulator_test: drift_modulator_test.cpp ../src/DriftModulator.h
	$(CXX) $(CXXFLAGS) -o $@ $<

organ_preset_test: organ_preset_test.cpp ../src/OrganPreset.h ../src/ParameterSteps.h
	$(CC) -std=c99 -O2 -I$(COREJSON_DIR)/include -c $(COREJSON_DIR)/core_json.c -o core_json_test.o
	$(CXX) $(CXXFLAGS) -I$(COREJSON_DIR)/include -o $@ $< core_json_test.o
	rm -f core_json_test.o

mod_dest_test: mod_dest_test.cpp ../src/ModDest.h
	$(CXX) $(CXXFLAGS) -o $@ $<

# percussion_test includes OrganEngine.h (engine integration tests), so it
# needs DaisySP's headers (daisysp.h for fonepole; everything the engine
# uses is header-inline since the voice oscillators moved to SineLut.h).
# PlateReverb.h pulls the vendored fx_engine.h from ../src. Vendored
# headers trigger -Wunused-parameter etc.
DAISYSP_SRC = ../libs/DaisySP/Source
percussion_test: percussion_test.cpp ../src/Percussion.h ../src/OrganEngine.h ../src/OrganPreset.h ../src/ParameterSteps.h
	$(CXX) $(CXXFLAGS) -I$(DAISYSP_SRC) -I$(DAISYSP_SRC)/Utility \
		-I$(COREJSON_DIR)/include -Wno-unused-parameter -o $@ $<

# audio_routing_test includes OrganEngine.h (output-distribution tests),
# so it needs the same DaisySP header paths and warning relief as
# percussion_test (OrganEngine.h does not pull in coreJSON).
audio_routing_test: audio_routing_test.cpp ../src/AudioRouting.h ../src/OrganEngine.h ../src/ParameterSteps.h
	$(CXX) $(CXXFLAGS) -I$(DAISYSP_SRC) -I$(DAISYSP_SRC)/Utility \
		-Wno-unused-parameter -o $@ $<

stereo_bbd_delay_test: stereo_bbd_delay_test.cpp ../src/StereoBbdDelay.h
	$(CXX) $(CXXFLAGS) -o $@ $<

sine_lut_test: sine_lut_test.cpp ../src/SineLut.h
	$(CXX) $(CXXFLAGS) -o $@ $<

sd_recorder_test: sd_recorder_test.cpp ../src/SdRecorder.h
	$(CXX) $(CXXFLAGS) -o $@ $<

sd_player_test: sd_player_test.cpp ../src/SdPlayer.h ../src/SdRecorder.h
	$(CXX) $(CXXFLAGS) -o $@ $<

midi_cc_map_test: midi_cc_map_test.cpp ../src/MidiCcMap.h ../src/ParameterSteps.h
	$(CXX) $(CXXFLAGS) -o $@ $<

# The vendored fx_engine.h triggers -Wunused-parameter from its DelayLine
# template parameters; keep the test output readable without editing it.
plate_reverb_test: CXXFLAGS += -Wno-unused-parameter

clean:
	rm -f parameter_steps_test parameter_steps_test.exe \
		tonewheel_character_test tonewheel_character_test.exe \
		plate_reverb_test plate_reverb_test.exe \
		distortion_test distortion_test.exe \
		vibrato_test vibrato_test.exe \
		envelope_map_test envelope_map_test.exe \
		drift_modulator_test drift_modulator_test.exe \
		organ_preset_test organ_preset_test.exe core_json_test.o \
		mod_dest_test mod_dest_test.exe \
		percussion_test percussion_test.exe \
		stereo_bbd_delay_test stereo_bbd_delay_test.exe \
		sine_lut_test sine_lut_test.exe \
		sd_recorder_test sd_recorder_test.exe \
	sd_player_test sd_player_test.exe \
	midi_cc_map_test midi_cc_map_test.exe \
	audio_routing_test audio_routing_test.exe
