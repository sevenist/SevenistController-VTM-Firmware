/**
 * @file Encoder.cpp
 * @brief Encoder ISR-driven single-edge detent decode and button
 * long-press detection. Both MENU navigation and LIVE-mode cursorIndex
 * movement read detents out of the encoder's position via getDelta() from
 * App::update() (task context, see App.cpp) -- nothing here calls into
 * menu/app code directly from ISR context. See Encoder.h's file comment
 * for why this decodes on A's rising edge only rather than a 4x
 * quadrature table.
 */
#include "Encoder.h"
#include "logger.h"

uint8_t Encoder::_pinBtn = 0;
uint8_t Encoder::_pinA = 0;
uint8_t Encoder::_pinB = 0;
volatile int16_t Encoder::_position = 0;
volatile int16_t Encoder::_prvposition = 0;
volatile uint32_t Encoder::_lastEdgeUs = 0;
bool Encoder::_buttonPressed = false;
bool Encoder::_buttonLongPressed = false;
portMUX_TYPE Encoder::_mux = portMUX_INITIALIZER_UNLOCKED;

Encoder::Encoder(uint8_t pinA, uint8_t pinB, uint8_t pinBtn)
{
    _pinA = pinA;
    _pinB = pinB;
    _pinBtn = pinBtn;
}

void Encoder::begin()
{
    pinMode(_pinA, INPUT);
    pinMode(_pinB, INPUT);
    pinMode(ENCODER_BTN, INPUT);
    attachInterrupt(_pinA, isrA, RISING);
}

uint32_t timestamp = millis();
bool pvbtn = false;
bool lpresslock = false;
void IRAM_ATTR Encoder::updateButton()
{
    bool btn = digitalRead(_pinBtn);

    // TODO: temporary debug -- logs the raw pin level every call so a
    // floating/misconfigured button pin (no pull-up/pull-down; see begin(),
    // plain INPUT) shows up as rapid true/false noise in the Serial monitor
    // instead of a clean, sustained level while idle/pressed. Remove once
    // button detection is confirmed working.
    static bool lastLoggedBtn = false;
    if (btn != lastLoggedBtn)
    {
        LOG_DEBUG("Encoder::updateButton: pin level -> %d\n", (int)btn);
        lastLoggedBtn = btn;
    }

    // Check for button press, by measuring the time since the last press and the current state of the button
    // when the button is pressed we stop updating the timestamp, so the delta increases, when the button is released we reset the timestamp to the current time
    // if the button is pressed for 1000ms it triggers a longpress then locks itself so it doesn't trigger again until the button is released and pressed again.
    // when the button falls below 1000ms it triggers a short press and resets the timestamp to the current time.

    if (!btn && !pvbtn) // button released timestamp = current time
    {
        timestamp = millis();
        lpresslock = false;
        return;
    }

    if (!btn && pvbtn && (millis() - timestamp < BTN_LONG_PRESS_TIME)) // short press
    {
        _buttonPressed = true;
        LOG_DEBUG("Encoder::updateButton: short press detected\n");
        pvbtn = btn;
        return;
    }

    if ((millis() - timestamp >= 1000) && !lpresslock) // long press
    {
        _buttonLongPressed = true;
        LOG_DEBUG("Encoder::updateButton: long press detected\n");
        lpresslock = true;
    }

    pvbtn = btn;
}

void IRAM_ATTR Encoder::isrA()
{
    // One physical detent produces exactly one A rising edge (that's what
    // RISING-only attachInterrupt(), begin(), watches for) -- direction
    // comes from B's level (classic quadrature: B already low means A led
    // B, one direction; B still high means B led A, the other). No
    // transition table, no division, so there's nothing for a stray
    // resting state to desync.
    uint32_t now = micros();
    if (now - _lastEdgeUs < DEBOUNCE_US)
        return; // bounce/ringing on this same detent's edge -- not a second detent

    // Settle-confirm: a purely time-based reject (above) doesn't catch a
    // pin that bounces back LOW before the next RISING trigger re-arms --
    // that looks like a fresh, well-spaced edge to a timer-only check. A
    // short busy-wait then re-reading pinA (and requiring it still HIGH)
    // catches that case too -- see this file's header comment, point 2.
    delayMicroseconds(SETTLE_US);
    if (!digitalRead(_pinA))
        return; // bounced back low within the settle window -- not a real detent

    _lastEdgeUs = now;
    int8_t delta = digitalRead(_pinB) ? 1 : -1;
    portENTER_CRITICAL_ISR(&_mux);
    _position += delta;
    portEXIT_CRITICAL_ISR(&_mux);
}

int16_t Encoder::read()
{
    portENTER_CRITICAL(&_mux);
    int16_t p = _position;
    portEXIT_CRITICAL(&_mux);
    return p;
}

int8_t Encoder::getDelta()
{
    portENTER_CRITICAL(&_mux);
    int8_t ret = (int8_t)(_position - _prvposition);
    _prvposition = _position;
    portEXIT_CRITICAL(&_mux);
    return ret;
}

bool Encoder::isButtonPressed()
{
    bool ret = _buttonPressed;
    _buttonPressed = false;
    return ret;
}

bool Encoder::isButtonLongPressed()
{
    bool ret = _buttonLongPressed;
    _buttonLongPressed = false;
    return ret;
}

void Encoder::write(int16_t p)
{
    portENTER_CRITICAL(&_mux);
    _position = p;
    portEXIT_CRITICAL(&_mux);
}
