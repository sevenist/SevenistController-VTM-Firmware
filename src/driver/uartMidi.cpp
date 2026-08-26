/**
 * @file uartMidi.cpp
 * @brief `midi1` (DIN/UART transport) setup and its clock/transport/CC/note
 * callbacks. initMidi() wires these into the MIDI library at boot; nothing
 * else in the codebase calls them directly.
 */
#include "./driver/uartMidi.h"
#include "boardConfig.h"
#include <freertos/FreeRTOS.h>

bool midiSync = true;
unsigned long lastQuarterNoteUs = micros();
int tempoAccum = 0;
double globalTempo = 120.0;
bool playing = false;

SyncSource syncSource = SyncSource::MIDI;
int16_t ppqSetting = DEFAULT_PPQ;
int16_t internalBpm = 120;

namespace
{
    // Pulses received via onSyncPulse() since the last
    // consumeQuarterNotePhaseDelta() call -- written from whichever task
    // pumps the active transport (appTask for DIN, via handleClock();
    // midiInputTask for USB, once implemented), read from trackClockTask on
    // the other core. Guarded by a spinlock (short, non-blocking
    // read-and-clear) rather than plain volatile -- see track.h's
    // Track::_mux for the same pattern.
    volatile uint32_t pendingPulses = 0;
    portMUX_TYPE pulseMux = portMUX_INITIALIZER_UNLOCKED;
}

void onSyncPulse(SyncSource source)
{
    if (source != syncSource)
        return; // not the transport the user selected -- ignore
    portENTER_CRITICAL(&pulseMux);
    pendingPulses++;
    portEXIT_CRITICAL(&pulseMux);
}

double consumeQuarterNotePhaseDelta()
{
    portENTER_CRITICAL(&pulseMux);
    uint32_t pulses = pendingPulses;
    pendingPulses = 0;
    portEXIT_CRITICAL(&pulseMux);

    if (pulses == 0 || ppqSetting == 0)
        return 0.0;
    return (double)pulses / (double)ppqSetting;
}

midi::SerialMIDI<HardwareSerial> serialmidi1(Serial0);
midi::MidiInterface<midi::SerialMIDI<HardwareSerial>> midi1((midi::SerialMIDI<HardwareSerial>&)serialmidi1);


void handleClock()
{
  onSyncPulse(SyncSource::MIDI);

  if (++tempoAccum >= TEMPO_PRECISION)
  {
    uint32_t now = micros();
    long delta = (now - lastQuarterNoteUs) * (DEFAULT_PPQ / TEMPO_PRECISION);
    globalTempo = globalTempo * 0.75 + (60000000.0 / delta) * 0.25;
    //LOG_DEBUG("tempo : %.2f \n\r", tempo);
    tempoAccum -= TEMPO_PRECISION;
    lastQuarterNoteUs = now;
  }
}

void handleNote(midi::Channel channel, byte note, byte velocity)
{
  LOG_DEBUG("note : %d, vel : %d \n\r", note, velocity);
}

void handleCC(midi::Channel channel, byte cc, byte value)
{
  LOG_DEBUG("cc : %d, value : %d \n\r", cc, value);
}

void handleStart()
{
  playing = true;
  LOG_DEBUG("start\n");
}

void handleStop()
{
  playing = false;
  LOG_DEBUG("stop\n");
}
void handleContinue()
{
  playing = true;
  LOG_DEBUG("continue\n");
}

void initMidi()
{
  midi1.begin(0);
  midi1.turnThruOn();
  midi1.setHandleClock(handleClock);
  midi1.setHandleControlChange(handleCC);
  midi1.setHandleNoteOn(handleNote);
  midi1.setHandleStart(handleStart);
  midi1.setHandleStop(handleStop);
  midi1.setHandleContinue(handleContinue);
}
