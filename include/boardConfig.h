/**
 * @file boardConfig.h
 * @brief Pin assignments, hardware constants, and the track-list capacity.
 *
 * No `Globals.h` dependency here on purpose -- this header sits below
 * `Globals.h` in the include graph (see CLAUDE.md) and must stay that way.
 */
#pragma once
#include <Arduino.h>
#include "logger.h"

#define S1 GPIO_NUM_9
#define S2 GPIO_NUM_8
#define S3 GPIO_NUM_7
#define ADC1 GPIO_NUM_11
#define ADC2 GPIO_NUM_10
#define MIDI_TX 43
#define MIDI_RX 44
#define OLEDSCK 2
#define OLEDSDA 3
#define LEDRGB_PIN GPIO_NUM_1
#define LED_NBR 8
#define POT_NBR 8
#define QUADRATURE_SIG_NBR 2

// GPIO4/5 swapped from silkscreen order: GPIO4 has a cleaner rising edge,
// so it's ENCODER_A (interrupt pin); GPIO5 is polled for direction only.
#define ENCODER_A GPIO_NUM_4
#define ENCODER_B GPIO_NUM_5
#define ENCODER_BTN GPIO_NUM_6
#define BTN_LONG_PRESS_TIME 700

// Comment out to skip USB-MIDI registration -- enumerating it alongside
// Serial can make the port unreliable to attach to during debugging.
// TODO: temporarily disabled while debugging the encoder button over Serial
// (see main.cpp's setup()-tracing LOG_DEBUG lines and Encoder.cpp's
// updateButton() pin-level logging) -- re-enable once that's resolved.
//#define USE_USB_MIDI


// 8 physical pots = 4 pot-pairs, so 4 tracks are visible/controlled at once.
// Tracks are user-created (see App::createTrack()); TOTAL_TRACKS is the max
// capacity of the flat, contiguous track list, not a fixed populated count.
#define VISIBLE_SLOTS 4
#define TOTAL_TRACKS 64

inline uint8_t mappingLUT[2 * POT_NBR] = {13, 15,        // pot1
                                          14, 12,        // pot2
                                          9, 10,         // pot3
                                          8, 11,         // pot4
                                          6, 4,          // pot5
                                          1, 2,          // pot6
                                          0, 3,          // pot7
                                          7, 5};         // pot8
inline uint8_t mappingLED[8] = {0, 1, 2, 3, 7, 6, 5, 4}; // maps pot index in pots quadrature potentiometer array to leds index

#define SCREEN_W 128
#define SCREEN_H 64

// Menu list row height in pixels and how many rows fit on screen at once
// (SCREEN_H / MENU_ROW_HEIGHT) -- used by App.cpp to scroll the menu the
// same way it scrolls the VISIBLE_SLOTS track window (see
// App.cpp's menuWindowStart/menuRowY()).
#define MENU_ROW_HEIGHT 16
#define MENU_VISIBLE_ROWS (SCREEN_H / MENU_ROW_HEIGHT)

// Track column view layout (App.cpp's drawTracks()/drawInsertView()) --
// pixel offsets within a VISIBLE_SLOTS column, not derived from SCREEN_W/H
// since they depend on the icon/font assets' fixed sizes.
#define TRACK_BAR_AREA_TOP 15  // leaves room for the type-abbrev label row
#define TRACK_ICON_Y 18        // upper knob icon's y position within a column

// Menu row layout (App.cpp's defaultMenuRowDisplay()/defaultMenuIconDisplay()).
#define MENU_VALUE_MARGIN 16 // right-edge margin for an INT_4B/INT_7B value
#define MENU_TEXT_MARGIN 24  // right-edge margin for a TEXT node's value
#define MENU_ICON_TEXT_OFFSET 18 // x-offset of a row's label past its icon

// LED strip brightness (0-255, FastLED.setBrightness()).
#define LED_BRIGHTNESS 50

// Pot quadrature delta -> track value gain (App.cpp's App::render()).
#define POT_DELTA_GAIN 32