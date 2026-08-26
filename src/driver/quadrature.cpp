/**
 * @file quadrature.cpp
 * @brief Quadrature implementation: atan2-based sin/cos angle decode,
 * unwrapped into an accumulated position via _acc/_accDelta.
 */
#include "quadrature.h"





Quadrature::Quadrature(ReadFunc readSin, ReadFunc readCos, int16_t center,
                       double gain, double minValue, double maxValue)
    : _readSin(readSin), _readCos(readCos), _center(center), _gain(gain), _accel(0.0),
      _minValue(minValue), _maxValue(maxValue), _lastRad(0.0), _acc(1.0), _accDelta(0.0)
{
    outOfBound = false;
}

void Quadrature::begin()
{
    int16_t sin_v = (int16_t)_readSin();
    int16_t cos_v = (int16_t)_readCos();
    _lastRad = atan2(sin_v - _center, cos_v - _center);
}

double Quadrature::update()
{
    int16_t sin_v = (int16_t)_readSin();
    int16_t cos_v = (int16_t)_readCos();
    double rad = atan2(sin_v - _center, cos_v - _center);

    double delta = rad - _lastRad;
    if (delta > PI)
        delta -= 2 * PI;
    if (delta < -PI)
        delta += 2 * PI;
    _lastRad = rad;

    portENTER_CRITICAL(&_mux);
    _accDelta += delta;
    _acc += delta * _gain;

    bool outOfBoundPositive = false;
    bool outOfBoundNegative = false;

    if (_acc > _maxValue)
    {
        _acc = _maxValue;
        outOfBoundPositive = true;
    }
    if (_acc < _minValue)
    {
        _acc = _minValue;
        outOfBoundNegative = true;
    }
    double ret = _acc;
    portEXIT_CRITICAL(&_mux);

    if (outOfBoundNegative || outOfBoundPositive)
        outOfBound = true;

    return ret;
}

double Quadrature::getValue() const
{
    portENTER_CRITICAL(&_mux);
    double ret = _acc;
    portEXIT_CRITICAL(&_mux);
    return ret;
}

double Quadrature::getDelta(bool autoReset)
{
    portENTER_CRITICAL(&_mux);
    double ret = abs(_accDelta) > 0.008 ? _accDelta : 0.0; // ignore small deltas
    if (autoReset && ret != 0.0)
        _accDelta = 0.0;
    portEXIT_CRITICAL(&_mux);
    return ret;
}

void Quadrature::resetDelta()
{
    portENTER_CRITICAL(&_mux);
    _accDelta = 0.0;
    portEXIT_CRITICAL(&_mux);
}
