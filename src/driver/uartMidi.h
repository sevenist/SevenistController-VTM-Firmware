/**
 * @file uartMidi.h
 * @brief DIN/UART MIDI (fortyseveneffects MIDI Library) setup, transport/
 * clock tracking, and inbound message callbacks. `handleNote`/`handleCC`
 * currently just log to Serial -- no real inbound MIDI handling yet (see
 * PROJECT.md implementation status). Outbound sends happen directly via
 * `midi1` from trackClockTask/App, not through this header.
 */
#pragma once
#include <Arduino.h>
#include <MIDI.h>
#include <functional>

#define TEMPO_PRECISION 6
#define DEFAULT_PPQ 24
extern bool midiSync;
extern unsigned long lastQuarterNoteUs; ///< micros() timestamp of the last tempo update in handleClock().
extern int tempoAccum;          ///< Clock-tick accumulator, fires a tempo recompute every TEMPO_PRECISION ticks.
extern double globalTempo;      ///< Smoothed BPM estimate, derived from incoming MIDI clock.
extern bool playing;            ///< Transport state, set by handleStart/Stop/Continue.

/// Which transport's MIDI clock (0xF8, 24 PPQ) drives synced tracks
/// (LFO in sync mode, StepSeq, MotionSeq) -- a Config menu setting
/// (see menus.cpp's menuSyncSource). NONE means no external clock drives
/// synced tracks (their tick() sees a zero quarter-note phase delta every
/// cycle, so they simply don't advance).
enum class SyncSource : uint8_t
{
    NONE,
    MIDI,
    USB,
};

extern SyncSource syncSource; ///< Config menu setting, defaults to MIDI.

/// Pulses-per-quarter-note the incoming clock is assumed to run at --
/// a Config menu setting (see menus.cpp's menuPpqCount). Almost always 24
/// (the MIDI standard), exposed as a setting rather than hardcoded in case a
/// transport ever needs a different resolution. int16_t (not uint8_t)
/// because every MenuNode::actionCtx in this codebase points at an int16_t
/// (see Menu.h's MenuNode::DisplayFunc doc comment) -- menus.cpp's
/// menuPpqCount points straight at this.
extern int16_t ppqSetting;

/// User-set BPM the internal clock runs at while syncSource == NONE --
/// independent of globalTempo, which is a read-only estimate from
/// incoming MIDI clock and stays meaningless while unsynced.
extern int16_t internalBpm;

/// Registers one incoming clock pulse (0xF8) from `source` -- a no-op unless
/// source == syncSource, so pulses from the transport the user hasn't
/// selected are ignored. Called from handleClock() (this file) and the
/// USB-MIDI clock handler (usbMidi.cpp). Thread-safe (may be called from a
/// different task/core than consumeQuarterNotePhaseDelta()).
void onSyncPulse(SyncSource source);

/// Drains every pulse registered via onSyncPulse() since the last call and
/// returns the fraction of a quarter note they represent (pulses /
/// ppqSetting) -- 0.0 if none arrived. Called once per trackClockTask cycle
/// and fed into every synced Track's tick(dtMs, qnPhaseDelta).
double consumeQuarterNotePhaseDelta();

/// DIN/UART MIDI transport, configured by initMidi(). Outbound sends (e.g.
/// trackClockTask, Tasks.cpp) call sendControlChange()/sendNoteOn()/
/// sendNoteOff() on this directly.
extern midi::MidiInterface<midi::SerialMIDI<HardwareSerial>> midi1;

/// Configures `midi1`, enables MIDI Thru, and registers the handle* callbacks below.
void initMidi();

/// MIDI clock (0xF8) callback: accumulates ticks and recomputes globalTempo every TEMPO_PRECISION calls.
void handleClock();
/// Note On callback. @todo Logging only, no real handling yet.
void handleNote(midi::Channel channel, byte note, byte velocity);
/// Control Change callback. @todo Logging only, no real handling yet.
void handleCC(midi::Channel channel, byte cc, byte value);
/// Transport Start callback: sets playing = true.
void handleStart();
/// Transport Stop callback: sets playing = false.
void handleStop();
/// Transport Continue callback: sets playing = true.
void handleContinue();



