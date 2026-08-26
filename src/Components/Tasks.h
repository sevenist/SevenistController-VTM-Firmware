/**
 * @file Tasks.h
 * @brief FreeRTOS task entry points, created from main.cpp::setup(). Four
 * different update rates, matching how latency-sensitive each concern is:
 *  - muxPollTask:    as fast as the mux/pots can be read (10ms) -- raw input.
 *  - trackClockTask: as fast as feasible (see its doc comment) -- clock-driven
 *    track playback (LFO/sequencer), MIDI sent only when output changes.
 *  - renderTask:     ~60Hz -- pot-driven Track::update() for the visible
 *    tracks + OLED/LED render, all in one loop body so a knob move and the
 *    frame that shows it happen in the same tick (see renderTask()'s comment
 *    for why these two aren't split across tasks).
 *  - appTask:        ~20Hz (50ms) -- menu navigation, encoder-button mode
 *    switching, track create/delete/config edits. Not latency-sensitive.
 */
#pragma once

/// @todo Declared, not yet defined anywhere or started from setup() -- no
/// inbound MIDI task exists yet (see uartMidi.h's handle* callbacks, which
/// are registered but never pumped from a task).
void midiInputTask(void* pvParameters);

/// Polls the encoder button + both multiplexer(s) into Multiplexer::_raw[],
/// then advances every Quadrature from the freshly-polled mux values.
/// Started from main.cpp::setup(); runs on whichever core FreeRTOS schedules
/// it on (not pinned).
void muxPollTask(void *pvParameters);

/// Menu navigation, mode switching, track create/delete/config edits --
/// everything in App::update() outside render(). Runs at APP_TASK_PERIOD_MS.
void appTask(void *pvParameters);

/// Pot-driven Track::update() for the on-screen tracks, plus OLED + LED
/// render, combined in one ~60Hz loop so a knob move and its frame land
/// in the same tick.
void renderTask(void *pvParameters);

/// Clock-driven track playback: drains MIDI/USB clock pulses into a
/// quarter-note phase delta and calls Track::tick() on every tickable
/// track. MIDI output sending is currently disabled (see track.h's TODO).
/// Runs as fast as feasible -- LFO/sequencer latency depends on it.
void trackClockTask(void *pvParameters);