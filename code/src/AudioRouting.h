/** @file AudioRouting.h
 *  @brief CHOMPI-PIPES output-routing selection: Normal monitoring vs.
 *  an external send-FX loop, selected with SHIFT + a physical black key
 *  (Normal Mode only).
 *
 *  Normal routing (boot default): the same audio goes to the headphone
 *  output and the line/master output; AUX IN is mixed into the bus and
 *  into SD recordings.
 *
 *  Send FX routing: the line output carries only the dry organ bus at a
 *  fixed pre-master-gain level (a send to an external stereo effects
 *  unit; the volume knob never changes the send level); the headphone
 *  output carries only what comes back on AUX IN (the effected return),
 *  scaled by the master gain; SD recordings contain only AUX IN. The
 *  AUX return never leaves through the line output, so the loop cannot
 *  feed back. With nothing patched into AUX IN the headphones and
 *  recordings are silent - by design.
 *
 *  Black keys are numbered physically from left to right: key 6 (C#4)
 *  selects Normal routing, key 7 (D#4) selects Send FX routing - the
 *  two black keys Modulation Mode does not use for destination
 *  selection, so there is no conflict. In Playback Mode the effective
 *  routing is always Normal (the takes must be audible in the
 *  headphones); the selection itself is preserved and resumes when
 *  Playback Mode exits. The selection is session-only: it boots on
 *  Normal and is never stored in the SD-card presets.
 *
 *  Hardware-independent (no Daisy includes) so the native tests exercise
 *  the exact production logic; chompi_main.cpp static_asserts the key
 *  table against Hardware::SwId.
 */
#pragma once

namespace chompi
{

/** The two output routings. */
enum AudioRouting
{
    kRoutingNormal = 0, /**< identical headphone + line output; AUX IN
                         *  mixed into the bus and recordings */
    kRoutingSendFx,     /**< line out = dry-bus send; headphones and
                         *  recordings = AUX IN only */
    kNumRoutings
};

/** Boot routing: Normal. */
constexpr int kRoutingDefault = kRoutingNormal;

/** SwId values of the two selection keys, in routing order: KEY_21
 *  (C#4, black key 6) selects Normal, KEY_22 (D#4, black key 7) selects
 *  Send FX. */
constexpr int kRoutingKeySw[2] = {22, 23};

/** SHIFT + key routing selection: returns the pressed key's routing, or
 *  `cur` unchanged when the key selects nothing. Nothing else writes
 *  the routing, so the selection persists across mode changes until
 *  reboot. */
constexpr int RoutingAfterKey(int cur, int swid)
{
    return swid == kRoutingKeySw[0]   ? kRoutingNormal
           : swid == kRoutingKeySw[1] ? kRoutingSendFx
                                      : cur;
}

} // namespace chompi
