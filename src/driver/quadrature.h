/**
 * @file quadrature.h
 * @brief Decodes one endless-rotation pot's sin/cos channel pair into an
 * accumulated angle/position. See PROJECT.md for why these pots need this
 * (they aren't standard single-turn pots or optical/magnetic encoders).
 */
#pragma once
#include <Arduino.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include "boardConfig.h"

#define CENTER 2048
/**
 * @brief Tracks an accumulated position from a pair of quadrature-like
 * (sin/cos) uint16_t signals, e.g. two multiplexer channels wired to a
 * pot's sin/cos outputs.
 *
 * One instance per physical pot (`pots[POT_NBR]` in App.h/.cpp), each
 * constructed with lambdas reading its two mux channels. update() is
 * called from potentiometerPollTask; getValue()/getDelta() are read from
 * other tasks/cores, so `_acc`/`_accDelta` are guarded by `_mux` (a
 * spinlock -- see docs/freertos-locking-cheatsheet.md).
 */


class Quadrature
{
public:
    using ReadFunc = std::function<uint16_t()>;
    Quadrature();
    /**
     * @brief Constructs a decoder around two channel-reading callbacks.
     * @param readSin   Returns the raw ADC reading for the sin channel.
     * @param readCos   Returns the raw ADC reading for the cos channel.
     * @param center    ADC midpoint (0 signal level) for both channels.
     * @param gain      Scales the decoded angle into output units (default maps a full turn to ~64 units).
     * @param minValue  Lower clamp for getValue()'s accumulated position.
     * @param maxValue  Upper clamp for getValue()'s accumulated position.
     */
    Quadrature(ReadFunc readSin, ReadFunc readCos, int16_t center = 2048,
               double gain = 64.0 / PI, double minValue = 0.0, double maxValue = 127.0);

    /// One-time init (no hardware side effects here beyond an initial read); call before update().
    void begin();
    /// Reads both channels, advances the accumulated angle/position and delta. Returns the new value. Called from potentiometerPollTask.
    double update();
    /// Thread-safe read of the current accumulated position (clamped to [minValue, maxValue]).
    double getValue(void) const;
    /// Thread-safe read of the change in position since the last call (or since resetDelta()). @param autoReset When true (default), consumes the delta so the next call starts from 0.
    double getDelta(bool autoReset = true);
    /// Zeroes the accumulated delta without affecting getValue()'s position.
    void resetDelta();
    bool outOfBound; ///< Set when the decoded angle jumped outside the expected quadrature range (bad reading / bounce).

    private:
    ReadFunc _readSin;
    ReadFunc _readCos;
    int16_t _center;
    double _accel;
    double _gain;
    double _minValue;
    double _maxValue;
    double _lastRad;
    double _acc;
    double _accDelta;
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED; // guards _acc/_accDelta across cores/tasks
};

extern Quadrature *pots[POT_NBR];