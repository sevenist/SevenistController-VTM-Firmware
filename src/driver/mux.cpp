/**
 * @file mux.cpp
 * @brief Multiplexer implementation: select-line bit-banging + analogRead
 * per channel, for both MuxMode::SingleMux4Bits and MuxMode::DualMux3Bits modes.
 */
#include "mux.h"
#include "boardConfig.h"

Multiplexer::Multiplexer(MuxMode mode, int8_t PIN_s0, int8_t PIN_s1, int8_t PIN_s2, int8_t PIN_s3, int8_t PIN_COM, int8_t PIN_COM2)
{
    _s0 = PIN_s0;
    _s1 = PIN_s1;
    _s2 = PIN_s2;
    if (PIN_s3 != -1)
    {
        _s3 = PIN_s3;
        _mode = 1;
    }
    _COM = PIN_COM;
    if (PIN_COM2 != -1)
    {
        _COM2 = PIN_COM2;
        _mode = 2;
    }
    Multiplexer::_raw[MUX_CHANNELS] = {0};
}

uint16_t Multiplexer::getValue(uint8_t addr)
{
    return _raw[addr];
}

void Multiplexer::poll()
{
    for (uint8_t addr = 0; addr < MUX_CHANNELS; addr++)
    {
        if (_mode == 1)
        {
            digitalWrite(this->_s0, (addr >> 0) & 0x01);
            digitalWrite(this->_s1, (addr >> 1) & 0x01);
            digitalWrite(this->_s2, (addr >> 2) & 0x01);
            digitalWrite(this->_s3, (addr >> 3) & 0x01);
            delayMicroseconds(15);
            _raw[addr] = analogRead(_COM);
        }
        else
        {
            digitalWrite(this->_s0, (addr >> 0) & 0x01);
            digitalWrite(this->_s1, (addr >> 1) & 0x01);
            digitalWrite(this->_s2, (addr >> 2) & 0x01);
            delayMicroseconds(15);
            _raw[addr] = (addr >> 3) & 0x01 ? _raw[addr] = analogRead(_COM) : _raw[addr] = analogRead(_COM2);
        }
    }
}

uint16_t Multiplexer::read(uint8_t addr) const
{
    return _raw[addr];
}

void Multiplexer::init()
{
    pinMode(this->_s0, OUTPUT);
    pinMode(this->_s1, OUTPUT);
    pinMode(this->_s2, OUTPUT);
    if (_mode == 1)
        pinMode(this->_s3, OUTPUT);

    pinMode(this->_COM, INPUT);
    if (_mode == 2)
        pinMode(this->_COM2, INPUT);
}

bool Multiplexer::dRead(uint8_t addr)
{
    digitalWrite(this->_s0, (addr >> 0) & 0x01);
    digitalWrite(this->_s1, (addr >> 1) & 0x01);
    digitalWrite(this->_s2, (addr >> 2) & 0x01);
    digitalWrite(this->_s3, (addr >> 3) & 0x01);
    if (digitalRead(_COM))
        return true;
    return false;
}