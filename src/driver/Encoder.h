/**
 * @file Encoder.h
 * @brief Single-edge detent decoder for this project's rotary encoder (see
 * App.cpp: `Encoder encoder(ENCODER_A, ENCODER_B, ENCODER_BTN);`).
 *
 * Only pin A is watched, on RISING (ENCODER_A picked in boardConfig.h as
 * whichever physical line gives the cleaner edge on this hardware -- see
 * its comment there). Each detent produces exactly one A rising edge, so
 * one interrupt = one detent -- direction comes from B's level (classic
 * low-cost mechanical-encoder pattern: B leads/lags A by 90 degrees
 * depending on rotation direction). This replaces an earlier 4x quadrature
 * decode (both pins on CHANGE, a 16-entry transition table, position >>= 2
 * to get detents) that could drift out of phase with the physical detents
 * whenever the encoder's resting state between detents wasn't exactly the
 * same every time -- not all detented encoders guarantee that, so the
 * divide-by-4 could silently miscount. Single-edge decode has nothing to
 * divide, so there's no phase to drift.
 *
 * Debounced two ways, both in isrA():
 *  1. A time-based deadzone (DEBOUNCE_US) rejects any A edge arriving
 *     implausibly soon after the last *accepted* one outright.
 *  2. A short settle-confirmation: after the RISING edge fires, the ISR
 *     busy-waits SETTLE_US then re-reads pin A, only counting the detent
 *     if it's still HIGH -- a pure elapsed-time deadzone alone doesn't
 *     catch a pin that bounces LOW again before the next RISING trigger
 *     re-arms (each of those looks like a fresh, well-spaced edge to a
 *     timer-only check), whereas requiring the level to have actually
 *     settled HIGH catches that case too. SETTLE_US is intentionally short
 *     (double-digit microseconds) -- long enough to skip past contact
 *     bounce, short enough that busy-waiting inside the ISR is harmless.
 */
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include "boardConfig.h"

/// The menu-navigation rotary encoder + its push button. Single instance (`encoder` in App.h/.cpp).

class Encoder
{
public:
    Encoder(uint8_t pinA, uint8_t pinB, uint8_t pinBtn = ENCODER_BTN);

    // Configures pins (including the encoder's push button) and attaches
    // interrupts. Call once from setup(), after the object exists - keeping
    // hardware side effects out of the constructor avoids them running
    // during global static init (before setup()), ahead of anything else
    // the sketch still needs to set up.
    void begin();

    /// Current absolute position (detent count since last write()).
    int16_t read();
    /// Change in position since the last getDelta() call.
    int8_t getDelta();
    bool isButtonPressed();
    bool isButtonLongPressed();

    /// Resets the absolute position to p.
    void write(int16_t p);
    /// ISR: debounces and updates button-pressed/long-pressed state. Attached to the button pin.
    static void IRAM_ATTR updateButton();

private:
    // Minimum microseconds between accepted A rising edges -- anything
    // faster is contact bounce/ringing on the same physical detent, not a
    // second one, and is dropped rather than counted outright (no settle
    // wait even attempted). 3ms comfortably exceeds typical mechanical-
    // encoder bounce without being anywhere close to the fastest plausible
    // deliberate detent-to-detent spin rate.
    static constexpr uint32_t DEBOUNCE_US = 3000;

    // How long isrA() busy-waits after a RISING trigger before re-checking
    // pin A's level to confirm the edge settled HIGH rather than having
    // already bounced back LOW (see this header's file comment, point 2).
    // Short enough that spending it in a busy-wait inside the ISR is a
    // non-issue for the rest of the system.
    static constexpr uint32_t SETTLE_US = 150;

    static uint8_t _pinBtn;
    static uint8_t _pinA;
    static uint8_t _pinB;
    static volatile int16_t _position;
    static volatile int16_t _prvposition;
    static volatile uint32_t _lastEdgeUs;
    static bool _buttonPressed;
    static bool _buttonLongPressed;
    static portMUX_TYPE _mux;

    static void IRAM_ATTR isrA();
};
