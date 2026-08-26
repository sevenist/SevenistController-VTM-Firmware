/**
 * @file main.cpp
 * @brief Arduino entry point: brings up the hardware singletons declared in
 * App.h (oled, mux, encoder, pots[]) in dependency order, then hands off to
 * FreeRTOS tasks. setup() runs once on core 1 and returns; loop() (the
 * Arduino framework's own task) immediately deletes itself since this
 * project's real work happens in the tasks created here, not in loop().
 *
 * See Components/Tasks.h for the full task list and why each runs at its
 * own rate (muxPollTask/appTask/renderTask/trackClockTask are all started
 * below; midiInputTask isn't implemented yet).
 */
#include <Arduino.h>
#include "app/App.h"
#include "./driver/persistence.h"
#include "./Components/menus.h"

void setup()
{
  // TODO: temporary debug -- Serial moved to the very top of setup() (was
  // after oled.begin()/initUsbMidi()/USB.begin()) so every LOG_DEBUG below
  // actually reaches the monitor, including ones bracketing steps that run
  // before Serial.begin() used to happen. If setup() hangs, the last
  // "before X" line printed without its matching "after X" pinpoints which
  // init call is blocking. Remove/reorder once boot is confirmed healthy.
  Serial.begin(115200);
  delay(200); // let the USB CDC/Serial connection settle before the first log line
  LOG_DEBUG("setup: start\n");

  // OLED (U8g2 full-buffer mode): configure the font/draw state once here;
  // App::render() (renderTask, ~60Hz) does the per-frame clearBuffer()/
  // sendBuffer() around whatever it draws.
  LOG_DEBUG("setup: before oled.begin()\n");
  oled.begin();
  LOG_DEBUG("setup: after oled.begin()\n");
  oled.setFont(u8g2_font_8x13B_tr);
  oled.setFontRefHeightExtendedText();
  oled.setDrawColor(1);
  oled.setFontPosTop();
  oled.setFontDirection(0);
  oled.setBitmapMode(1);

  playStartupAnim();

  initLeds();

  // First poll primes Multiplexer::_raw[] before any Quadrature reads it below.
  LOG_DEBUG("setup: before mux.init()/poll()\n");
  mux.init();
  mux.poll();
  LOG_DEBUG("setup: after mux.init()/poll()\n");

#ifdef USE_USB_MIDI
  // USB-MIDI interface must be registered before USB.begin() -- that's
  // what calls tinyusb_init() and locks the descriptor set.
  initUsbMidi();
  USB.begin();
#endif

  initMidi();

  // LittleFS must be mounted before any menu Save/Load action -- without
  // this, savePreset()/loadPreset() silently no-op (LittleFS.open() fails
  // on an unmounted filesystem, logged but otherwise invisible). Load the
  // last-saved slot right after so a preset is live at boot too, per
  // PROJECT.md's persistence note.
  LOG_DEBUG("setup: before initStorage()/loadLastPreset()\n");
  initStorage();
  loadLastPreset();
  LOG_DEBUG("setup: after initStorage()/loadLastPreset()\n");

  LOG_DEBUG("setup: before encoder.begin()\n");
  encoder.begin();
  LOG_DEBUG("setup: after encoder.begin()\n");

  // Must run before appTask starts (below) -- appTask navigates the menu
  // tree (menus.h) via encoder input starting from its very first tick.
  buildMenuTree();
  // One Quadrature per physical pot, each closing over its own mux channel
  // pair (mappingLUT: sin/cos channel indices for pot i). begin()+update()
  // take an initial reading so getValue()/getDelta() are sane before
  // muxPollTask starts driving subsequent updates.
  for (int i = 0; i < POT_NBR; i++)
  {
    pots[i] = new Quadrature([i]() -> uint16_t
                             { return mux.read(mappingLUT[2 * i + 1]); },
                             [i]() -> uint16_t
                             { return mux.read(mappingLUT[2 * i]); });
    pots[i]->begin();
    pots[i]->update();
  }

  xTaskCreate(muxPollTask, "multiplexer polling task", 2048, NULL, 2, NULL);
  xTaskCreate(appTask, "app task", 4096, NULL, 2, NULL);
  xTaskCreate(renderTask, "render task", 4096, NULL, 1, NULL);
  xTaskCreatePinnedToCore(trackClockTask, "track clock task", 4096, NULL, 2, NULL, 1);
  LOG_DEBUG("setup: done, tasks started\n");
}

void loop()
{
  // All real work happens in FreeRTOS tasks created in setup(); the
  // Arduino core's own loop task has nothing to do.
  vTaskDelete(NULL);
}