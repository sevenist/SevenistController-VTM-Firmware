#pragma once
#include <Arduino.h>
#include <algorithm>


#pragma region Step sequencer


/// Step duration, as a note division denominator (1/32 .. 1/4).
enum class StepRate : uint8_t
{
    _1_32,
    _1_16T,
    _1_16,
    _1_8T,
    _1_8,
    _1_4T,
    _1_4,
};

/// One step's length in quarter notes for a given StepRate -- multiply by
/// the incoming quarter-note phase delta (uartMidi.h's
/// consumeQuarterNotePhaseDelta(), fed through Track::tick()) to know how
/// much of a step just elapsed. Triplet divisions (_1_4T/_1_8T/_1_16T) are
/// 2/3 of their even counterpart, per standard note-length convention.
inline double stepRateToQuarterNotes(StepRate rate)
{
    switch (rate)
    {
    case StepRate::_1_32:
        return 0.125;
    case StepRate::_1_16T:
        return 1.0 / 6.0;
    case StepRate::_1_16:
        return 0.25;
    case StepRate::_1_8T:
        return 1.0 / 3.0;
    case StepRate::_1_8:
        return 0.5;
    case StepRate::_1_4T:
        return 2.0 / 3.0;
    case StepRate::_1_4:
        return 1.0;
    }
    return 0.25;
}

/// Scale used to quantize step values to pitches. EDO is an equal division
/// of the octave into edoDivisions steps (5-24); all others span 7 degrees.
enum class ScaleType : uint8_t
{
    MAJOR,
    MINOR,
    MELODIC_MINOR,
    HARMONIC_MINOR,
    DORIAN,
    PHRYGIAN,
    LYDIAN,
    MIXOLYDIAN,
    LOCRIAN,
    WHOLE_TONE,
    HARMONIC_MAJOR,
    EDO,
};

#define EDO_DIVISIONS_MIN 5
#define EDO_DIVISIONS_MAX 24

/// Degree count of one octave for a given scale (7, or edoDivisions for EDO).
inline uint8_t scaleDegreeCount(ScaleType scale, uint8_t edoDivisions)
{
    if (scale == ScaleType::EDO)
        return std::clamp<uint8_t>(edoDivisions, EDO_DIVISIONS_MIN, EDO_DIVISIONS_MAX);
    return 7;
}

/// Per-track sequencer configuration.
struct SeqParams
{
    uint8_t stepCount = 16;              // 1-64
    StepRate stepRate = StepRate::_1_16;
    ScaleType scale = ScaleType::MAJOR;
    uint8_t edoDivisions = 12;           // only used when scale == EDO, 5-24
    uint8_t scaleOffset = 0;             // 0..scaleDegreeCount(scale, edoDivisions)-1
};


#pragma endregion


#pragma region notes and param points


#define STEP_POINT_NONE -1
#define STEP_GLIDE_FLAG 0x80

/// True if point encodes a glide (128-255: STEP_GLIDE_FLAG | value 1-127).
inline bool stepPointIsGlide(int16_t point)
{
    return point >= 128;
}

/// Plain 0-127 value encoded in point, glide flag stripped.
inline uint8_t stepPointValue(int16_t point)
{
    return (uint8_t)(point & 0x7F);
}

/// One step of a note sequencer.
struct StepNote
{
    int16_t note = STEP_POINT_NONE; // STEP_POINT_NONE / 0-127 / STEP_GLIDE_FLAG|1-127
    uint8_t velocity = 127;         // 1-127
    uint8_t length = 4;             // gate length in 1/32nds
};

/// One step of a motion (CC/parameter) sequencer.
union StepMotion
{
    int16_t value = STEP_POINT_NONE; // STEP_POINT_NONE / 0-127 / STEP_GLIDE_FLAG|1-127
};

#pragma endregion