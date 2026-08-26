/**
 * @file track.h
 * @brief The Track class hierarchy skeleton -- see PROJECT.md for the
 * ControlType concept overview. Being reworked around sequencer.h's data
 * structs (SeqParams/StepNote/StepMotion); behavior bodies are stubs.
 */
#pragma once
#include <Arduino.h>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include "FastLED.h"
#include "./driver/quadrature.h"
#include "sequencer.h"

extern uint8_t trackCount;

/// Track behavior kind, per the main.cpp control types.
enum class ControlType : uint8_t
{
    DUAL, // 2 midi cc assign list(one per knob)
    LFO, // 1 midi assign list(moving automatically via tick events)
    STEPSEQ, // 128 notes aviable to populate up to 64 steps no assign list, just note sequence.
    MOTIONSEQ, // up to 64 motion points (1 per step max) 1 midi cc assign list
};
#define CONTROL_TYPE_COUNT 4

#define TRACK_MAX_STEPS 64
#define TRACK_MAX_CCS 8

/// Optional curve applied between the raw pot value and the outgoing CC value.
enum class TransferFunction : uint8_t
{
    LINEAR,
    BELL,
    EXP,
    LOG,
};

/// One CC slot: channel/CC number plus its own start/end output range.
struct CCAssignment
{
    uint8_t channel = 0;
    uint8_t cc = 0;
    uint8_t start = 0;
    uint8_t end = 127;
    TransferFunction tf = TransferFunction::LINEAR;
};

/// Fixed-capacity list of CCAssignments plus how many of them are active.
struct CCAssignmentList
{
    CCAssignment ccs[TRACK_MAX_CCS] = {};
    uint8_t length = 0;
};

/// ControlType::DUAL params -- upper/lower knobs are independent CC outputs.
struct DualParams
{
    CCAssignmentList assignmentList1;
    CCAssignmentList assignmentList2;
    CRGB color1; //knob1 color 
    CRGB color2; //knob2 color

};

/// ControlType::LFO params.
enum class Waveform : uint8_t
    {
        SINE,
        TRIANGLE,
        SQUARE,
        SAW,
    };
struct LfoParams
{
    CCAssignmentList assignmentList1;
    uint16_t rate = 0; // 0-1023 sync rate, 1024-2047 free LFO.
    Waveform waveform = Waveform::SINE;
    bool resetOnPlay = false; // does play/stop midi in signals reset the phase setting.
    CRGB color1; //knob1 color

    /// True while rate encodes a clock-synced division (0-1023) rather than
    /// a free-running speed (1024-2047) -- see rate's comment above.
    bool isSync() const { return rate < 1024; }

    /// rate's synced half (0-1023) mapped onto sequencer.h's StepRate range
    /// (7 divisions, _1_32.._1_4), only meaningful while isSync().
    StepRate syncStepRate() const
    {
        uint16_t span = 1024 / 7;
        uint8_t idx = (uint8_t)std::min<uint16_t>(rate / span, 6);
        return (StepRate)idx;
    }
};

/// ControlType::STEPSEQ params -- step grid plus TRACK_MAX_STEPS notes.
struct StepSeqParams
{
    SeqParams sequence;
    StepNote notes[TRACK_MAX_STEPS] = {};
    uint8_t channel = 0;
    CRGB color1; //knob1 color
};

/// ControlType::MOTIONSEQ params -- step grid plus TRACK_MAX_STEPS motion
/// points, routed through a shared CC list.
struct MotionSeqParams
{
    SeqParams sequence;
    StepMotion motion[TRACK_MAX_STEPS] = {};
    CCAssignmentList ccs;
    CRGB color1; //knob1 color
};

/// Construction-time payload for makeTrack(): carries every ControlType's
/// params so a caller can fill in one (matching controlType) without
/// knowing yet which Track subclass will end up reading it. Not stored on
/// Track itself -- each subclass owns only its own param struct (dualCfg/
/// lfoCfg/stepCfg/motionCfg), see below.
struct TrackConfig
{
    ControlType controlType = ControlType::DUAL;

    DualParams dual;
    LfoParams lfo;
    StepSeqParams stepSeq;
    MotionSeqParams motionSeq;
};

/// Base track type. Holds state shared by every track kind; per-ControlType
/// config/behavior belongs in derived classes (own param struct member,
/// update()/getMidiOutput() overrides).
class Track
{
    public:
    Track(uint8_t index, double value1, double value2, CRGB trackColor)
    : index(index), value1(value1), value2(value2),
      trackColor(trackColor)
    {
    }
    virtual ~Track() = default;

    /// Advances value1/value2 from pot deltas. Called from App::render().
    virtual void update(double delta1, double delta2)
    {
        portENTER_CRITICAL(&_mux);
        value1 = std::clamp(value1 + delta1, 0.0, 127.9);
        value2 = std::clamp(value2 + delta2, 0.0, 127.9);
        portEXIT_CRITICAL(&_mux);
    }

    /// Clock-driven advance, independent of pot input. Called from
    /// trackClockTask for every track each tick; dtMs since the last tick,
    /// qnPhaseDelta is the fraction of a quarter note the active MIDI/USB
    /// clock advanced by since the last call (0.0 if unsynced or no clock
    /// pulses arrived -- see uartMidi.h's consumeQuarterNotePhaseDelta()).
    /// A synced subclass (LFO in sync mode, StepSeq, MotionSeq) advances off
    /// qnPhaseDelta; an unsynced one (LFO free-run) uses dtMs instead.
    virtual void tick(double dtMs, double qnPhaseDelta) { (void)dtMs; (void)qnPhaseDelta; }

    /// This track's ControlType. Each subclass reports its own -- no stored
    /// field, so it cannot desync from the actual class.
    virtual ControlType getControlType() const = 0;

    /// True if this subclass overrides tick() with real clock-driven
    /// behavior (LFO/StepSeq/MotionSeq) rather than the base no-op --
    /// App.cpp's tickableTracks[] uses this to skip DUO tracks entirely in
    /// trackClockTask's hot loop. Rebuilt whenever tracks[] changes (see
    /// App.cpp's rebuildTickableTracks()), not read every cycle.
    virtual bool isTickable() const { return false; }

    /// Expands current state into outbound MIDI. See track.cpp.
    ///virtual uint8_t getMidiOutput(midiMessage (&out)[TRACK_MAX_STEPS]); //TODO: bad signature will rewire later

    /// Computes this track's current upper/lower LED colors.
    virtual void getLedColors(CRGB &outUpper, CRGB &outLower) const
    {
        double v1, v2;
        getValues(v1, v2);
        outUpper = CRGB::blend(CRGB::Black,trackColor,  (uint8_t)(v1 * 2));
        outLower = CRGB::blend(CRGB::Black,trackColor,  (uint8_t)(v2 * 2));
    }

    // Thread-safe accessors -- renderTask and trackClockTask both touch
    // value1/value2 concurrently, see docs/freertos-locking-cheatsheet.md.
    void getValues(double &v1, double &v2) const
    {
        portENTER_CRITICAL(&_mux);
        v1 = value1;
        v2 = value2;
        portEXIT_CRITICAL(&_mux);
    }

    void setValues(double v1, double v2)
    {
        portENTER_CRITICAL(&_mux);
        value1 = v1;
        value2 = v2;
        portEXIT_CRITICAL(&_mux);
    }

    uint8_t index;

    CRGB trackColor;

    protected:
    // Spinlock guarding value1/value2 across cores -- see
    // docs/freertos-locking-cheatsheet.md.
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    double value1;
    double value2;
};

extern Track* tracks[];

/// ControlType::DUAL.
class TrackDUO : public Track
{
    public:
    TrackDUO(uint8_t index, double value1, double value2, CRGB trackColor, DualParams cfg)
    : Track(index, value1, value2, trackColor), dualCfg(cfg)
    {
    }

    void update(double delta1, double delta2) override;
    ControlType getControlType() const override { return ControlType::DUAL; }

    DualParams dualCfg;
};

/// ControlType::LFO.
class TrackLFO : public Track
{
public:
    TrackLFO(uint8_t index, double value1, double value2, CRGB trackColor, LfoParams cfg)
    : Track(index, value1, value2, trackColor), lfoCfg(cfg)
    {
    }

    void update(double delta1, double delta2) override;
    void tick(double dtMs, double qnPhaseDelta) override;
    ///uint8_t getMidiOutput(midiMessage (&out)[TRACK_MAX_STEPS]) override; //TODO: bad signature will rewire later
    void getLedColors(CRGB &outUpper, CRGB &outLower) const override;
    ControlType getControlType() const override { return ControlType::LFO; }
    bool isTickable() const override { return true; }

    LfoParams lfoCfg;

private:
    double phase = 0.0;
    Waveform waveform() const;
};

/// ControlType::STEPSEQ -- note step sequencer, see sequencer.h's StepNote.
class TrackStepSeq : public Track
{
public:
    TrackStepSeq(uint8_t index, double value1, double value2, CRGB trackColor, StepSeqParams cfg)
    : Track(index, value1, value2, trackColor), stepCfg(cfg)
    {
    }

    void update(double delta1, double delta2) override;
    void tick(double dtMs, double qnPhaseDelta) override;
    ///uint8_t getMidiOutput(midiMessage (&out)[TRACK_MAX_STEPS]) override; //TODO: bad signature will rewire later

    ControlType getControlType() const override { return ControlType::STEPSEQ; }
    bool isTickable() const override { return true; }

    StepSeqParams stepCfg;

protected:
    uint8_t currentStep = 0;
    // Accumulated quarter-note phase since the current step started --
    // StepSeq is always clock-synced (no free-run mode, see sequencer.h's
    // SeqParams), so this accrues from tick()'s qnPhaseDelta rather than
    // dtMs. Advances currentStep once it reaches stepRateToQuarterNotes(stepCfg.sequence.stepRate).
    double stepAccumQn = 0.0;
    bool justAdvanced = false;
    uint8_t previousStep = 0;

    // Which step the pot-driven editor (update()) is currently pointing at
    // -- upper knob (value1) moves this, lower knob (value2) edits
    // stepCfg.notes[editStep].note. Independent of currentStep (the
    // playhead) -- editing doesn't affect playback position.
    uint8_t editStep = 0;

    // Monophonic per-track note-off tracking: the note currently sounding
    // (STEP_POINT_NONE if none) and how much quarter-note phase is left on
    // its gate, decremented in tick() by qnPhaseDelta. A new populated step
    // firing while a note is still gated cuts it off immediately first --
    // see trackStepSeq.cpp's tick().
    int16_t activeNote = STEP_POINT_NONE;
    uint8_t activeChannel = 0;
    double gateRemainingQn = 0.0;
};

/// ControlType::MOTIONSEQ -- CC step sequencer, see sequencer.h's StepMotion.
class TrackMotionSeq : public Track
{
public:
    TrackMotionSeq(uint8_t index, double value1, double value2, CRGB trackColor, MotionSeqParams cfg)
    : Track(index, value1, value2, trackColor), motionCfg(cfg)
    {
    }

    void update(double delta1, double delta2) override;
    void tick(double dtMs, double qnPhaseDelta) override;
    ///uint8_t getMidiOutput(midiMessage (&out)[TRACK_MAX_STEPS]) override; //TODO: bad signature will rewire later

    ControlType getControlType() const override { return ControlType::MOTIONSEQ; }
    bool isTickable() const override { return true; }

    MotionSeqParams motionCfg;

protected:
    uint8_t currentStep = 0;
    // Same accumulation as TrackStepSeq::stepAccumQn -- MotionSeq is also
    // always clock-synced (sequencer.h's SeqParams has no free-run mode).
    double stepAccumQn = 0.0;
};

/// Constructs the Track subclass matching config.controlType, handing it
/// only the one param struct it actually owns.
inline Track *makeTrack(uint8_t index, double value1, double value2,
                         CRGB trackColor,
                         const TrackConfig &config)
{
    switch (config.controlType)
    {
    case ControlType::LFO:
        return new TrackLFO(index, value1, value2, trackColor, config.lfo);
    case ControlType::STEPSEQ:
        return new TrackStepSeq(index, value1, value2, trackColor, config.stepSeq);
    case ControlType::MOTIONSEQ:
        return new TrackMotionSeq(index, value1, value2, trackColor, config.motionSeq);
    default:
        return new TrackDUO(index, value1, value2, trackColor, config.dual);
    }
}
