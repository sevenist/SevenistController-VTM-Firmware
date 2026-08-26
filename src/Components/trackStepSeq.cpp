/**
 * @file trackStepSeq.cpp
 * @brief TrackStepSeq behavior: pot-driven step editor (update()) and
 * clock-synced playhead + MIDI note-on/off (tick()). See track.h for the
 * class-level skeleton.
 */
#include "track.h"
#include "../driver/uartMidi.h"
#include "../driver/usbMidi.h"
#include <cmath>

namespace
{
    // One pot detent moves App.cpp's delta1/delta2 by roughly +-32 (see
    // App::render()'s `pots[i]->getDelta() * 32`) -- dividing back out here
    // makes one detent move editStep/note by exactly one, matching every
    // other pot-as-stepper usage in this codebase (menus.cpp's latched
    // encoders move by whole units per detent too).
    constexpr double POT_DETENT_SCALE = 32.0;
}

void TrackStepSeq::update(double delta1, double delta2)
{
    // Upper knob: move the selected step (wraps within the current
    // stepCount, which stays fixed at SeqParams' default of 16 for now --
    // no menu screen resizes it yet).
    int steps = (int)std::lround(delta1 / POT_DETENT_SCALE);
    if (steps != 0 && stepCfg.sequence.stepCount > 0)
    {
        int next = ((int)editStep + steps) % (int)stepCfg.sequence.stepCount;
        if (next < 0)
            next += stepCfg.sequence.stepCount;
        editStep = (uint8_t)next;
    }

    // Lower knob: edit editStep's note. -1 (STEP_POINT_NONE) sits one below
    // 0 so turning the knob all the way down clears the step; velocity
    // stays pinned at max (127) until a menu screen exposes per-step
    // velocity editing. Guarded: tick() (trackClockTask, other core) reads
    // stepCfg.notes[currentStep] unguarded, and editStep can equal
    // currentStep, so a torn read/write on the same StepNote is possible
    // without the lock (see docs/freertos-locking-cheatsheet.md).
    int notes = (int)std::lround(delta2 / POT_DETENT_SCALE);
    if (notes != 0)
    {
        portENTER_CRITICAL(&_mux);
        StepNote &s = stepCfg.notes[editStep];
        int note = (s.note == STEP_POINT_NONE) ? -1 : (int)s.note;
        note = std::clamp(note + notes, -1, 127);
        s.note = (int16_t)note;
        s.velocity = 127;
        portEXIT_CRITICAL(&_mux);
    }

    // value1/value2 feed the existing OLED bar view / LED brightness
    // (drawTracks()/getLedColors()) -- repurposed here to show editStep and
    // the note currently being edited until a dedicated step-grid UI exists.
    // Guarded like Track::update() since trackClockTask/renderTask both
    // read these via getValues() from the other core.
    portENTER_CRITICAL(&_mux);
    int16_t editedNote = stepCfg.notes[editStep].note;
    portEXIT_CRITICAL(&_mux);
    setValues(editStep, (editedNote == STEP_POINT_NONE) ? 0.0 : (double)editedNote);
}

void TrackStepSeq::tick(double dtMs, double qnPhaseDelta)
{
    (void)dtMs;

    // Gate countdown for whatever note is currently sounding, independent of
    // whether the clock is running -- a note already gated should still cut
    // off on schedule even if sync stops dead.
    if (activeNote != STEP_POINT_NONE)
    {
        gateRemainingQn -= qnPhaseDelta;
        if (gateRemainingQn <= 0.0)
        {
            midi1.sendNoteOff((uint8_t)activeNote, 0, activeChannel + 1);
            usbMidiSend(0x80 | activeChannel, (uint8_t)activeNote, 0);
            activeNote = STEP_POINT_NONE;
        }
    }

    // StepSeq has no free-run mode (sequencer.h's SeqParams) -- always
    // clock-synced, so a zero qnPhaseDelta (no clock selected/no pulses
    // yet) simply means the playhead doesn't advance this cycle.
    if (qnPhaseDelta <= 0.0 || stepCfg.sequence.stepCount == 0)
    {
        justAdvanced = false;
        return;
    }

    double stepLengthQn = stepRateToQuarterNotes(stepCfg.sequence.stepRate);
    if (stepLengthQn <= 0.0)
        return;

    stepAccumQn += qnPhaseDelta;
    justAdvanced = false;

    while (stepAccumQn >= stepLengthQn)
    {
        stepAccumQn -= stepLengthQn;
        previousStep = currentStep;
        currentStep = (uint8_t)((currentStep + 1) % stepCfg.sequence.stepCount);
        justAdvanced = true;
    }

    if (!justAdvanced)
        return;

    // Copy the step out under the lock -- update() (renderTask, other core)
    // can write this same StepNote via editStep concurrently, and the MIDI
    // sends below can't happen inside a spinlock (see
    // docs/freertos-locking-cheatsheet.md).
    portENTER_CRITICAL(&_mux);
    StepNote s = stepCfg.notes[currentStep];
    portEXIT_CRITICAL(&_mux);

    if (s.note == STEP_POINT_NONE)
        return; // empty step -- nothing to sound

    // Monophonic per track: cut off whatever was still gated from a
    // previous step before starting the new one.
    if (activeNote != STEP_POINT_NONE)
    {
        midi1.sendNoteOff((uint8_t)activeNote, 0, activeChannel + 1);
        usbMidiSend(0x80 | activeChannel, (uint8_t)activeNote, 0);
    }

    uint8_t note = stepPointValue(s.note);
    midi1.sendNoteOn(note, s.velocity, stepCfg.channel + 1);
    usbMidiSend(0x90 | stepCfg.channel, note, s.velocity);

    activeNote = note;
    activeChannel = stepCfg.channel;
    // length is a gate in 1/32nds -- convert via the same StepRate table
    // tick() otherwise reads by quarter-note-per-step.
    gateRemainingQn = s.length * stepRateToQuarterNotes(StepRate::_1_32);
}
