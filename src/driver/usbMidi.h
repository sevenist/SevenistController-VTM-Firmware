/**
 * @file usbMidi.h
 * @brief USB-MIDI device interface (TinyUSB), a separate transport from the
 * DIN/UART MIDI in uartMidi.h.
 */
#pragma once
#include "stdint.h"

/**
 * @brief Registers the USB-MIDI device interface. Must be called before
 * Serial.begin()/USB.begin() runs, since TinyUSB locks its descriptor set
 * once tinyusb_init() has run.
 * @return false if registration failed (e.g. called after tinyusb_init()).
 */
bool initUsbMidi();

/// Pumps the raw USB-MIDI packet FIFO; call from a task loop like midiInputTask().
void usbMidiRead();

/// Sends a 3-byte channel voice message (e.g. Note On, CC) over USB-MIDI.
void usbMidiSend(uint8_t status, uint8_t data1, uint8_t data2);
