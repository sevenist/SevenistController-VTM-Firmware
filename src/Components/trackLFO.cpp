/**
 * @file trackLFO.cpp
 * @brief TrackLFO behavior stubs. See track.h for the class-level skeleton.
 */
#include "track.h"
#include <cmath>

void TrackLFO::update(double delta1, double delta2)
{
    Track::update(delta1, delta2);

    double v1, v2;
    getValues(v1, v2);

    // value1 (0-127.9) selects the waveform, quantized into 4 equal bands.
    Waveform w;
    if (v1 < 32.0)
        w = Waveform::SINE;
    else if (v1 < 64.0)
        w = Waveform::TRIANGLE;
    else if (v1 < 96.0)
        w = Waveform::SQUARE;
    else
        w = Waveform::SAW;

    // value2 (0-127.9) sweeps the full packed rate range (0-2047): low end
    // is clock-synced divisions, high end is free-running Hz -- see
    // LfoParams::rate's comment in track.h.
    uint16_t r = (uint16_t)std::clamp(v2 / 127.9 * 2047.0, 0.0, 2047.0);

    portENTER_CRITICAL(&_mux);
    lfoCfg.waveform = w;
    lfoCfg.rate = r;
    portEXIT_CRITICAL(&_mux);
}

void TrackLFO::tick(double dtMs, double qnPhaseDelta)
{
    portENTER_CRITICAL(&_mux);
    if (lfoCfg.isSync())
    {
        // rate encodes one of sequencer.h's StepRate divisions (0-1023,
        // see LfoParams::syncStepRate()) -- one full LFO cycle per that
        // many quarter notes, driven by the incoming MIDI/USB clock.
        double cycleQn = stepRateToQuarterNotes(lfoCfg.syncStepRate());
        if (cycleQn > 0.0)
            phase += qnPhaseDelta / cycleQn;
    }
    else
    {
        // Free-running: rate's 1024-2047 half maps onto a plain
        // cycles-per-second speed so higher rate = faster pulse, matching
        // the packed field's original intent.
        double hz = (double)(lfoCfg.rate - 1024) / 1023.0 * 10.0; // 0-10 Hz
        phase += (dtMs / 1000.0) * hz;
    }
    phase -= (double)(int64_t)phase; // wrap to [0, 1)
    portEXIT_CRITICAL(&_mux);

    // TODO: emit MIDI through lfoCfg.assignmentList1 once getMidiOutput() is redesigned.
}

void TrackLFO::getLedColors(CRGB &outUpper, CRGB &outLower) const
{
    portENTER_CRITICAL(&_mux);
    double p = phase;
    portEXIT_CRITICAL(&_mux);

    double dsample;
    switch (lfoCfg.waveform)
    {
    case Waveform::SINE:
        dsample = 0.5 + 0.5 * std::sin(2.0 * M_PI * p);
        break;
    case Waveform::TRIANGLE:
        dsample = p < 0.5 ? (p * 2.0) : (2.0 - p * 2.0);
        break;
    case Waveform::SQUARE:
        dsample = p < 0.5 ? 1.0 : 0.0;
        break;
    case Waveform::SAW:
    default:
        dsample = p;
        break;
    }

    uint8_t sample = (uint8_t)(255.0 * dsample);

    CRGB cmin = CRGB::blend(CRGB::Black, trackColor, 8);
    CRGB c = CRGB::blend(cmin, trackColor, sample);

    outUpper = c;
    outLower = c;
}

Waveform TrackLFO::waveform() const
{
    return lfoCfg.waveform;
}
