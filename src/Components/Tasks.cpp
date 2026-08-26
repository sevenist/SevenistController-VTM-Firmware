/**
 * @file Tasks.cpp
 * @brief Task bodies for the entry points declared in Tasks.h.
 */
#include "Components/Tasks.h"
#include "app/App.h"

#define APP_TASK_PERIOD_MS 50     // ~20Hz -- menu/state logic, not latency-sensitive
#define RENDER_TASK_PERIOD_MS 16  // ~60Hz -- pot-driven track update + OLED/LED render
#define TRACK_CLOCK_PERIOD_MS 3   // as fast as feasible -- LFO/sequencer tick + change-detected MIDI send

void muxPollTask(void *pvParameters)
{
    for (;;)
    {
        encoder.updateButton();
        // Encoder rotation is decoded entirely by Encoder::isrA(), attached
        // to pin A's rising edge in Encoder::begin() -- nothing to poll here.
        mux.poll();
        for (int i = 0; i < POT_NBR; i++)
        {
            pots[i]->update();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void appTask(void *pvParameters)
{
    for (;;)
    {
        app.update();
        vTaskDelay(pdMS_TO_TICKS(APP_TASK_PERIOD_MS));
    }
}

void renderTask(void *pvParameters) 
{
    for (;;)
    {
        app.render();
        vTaskDelay(pdMS_TO_TICKS(RENDER_TASK_PERIOD_MS));
    }
}

#if 0 // TODO: re-enable once Track's outbound MIDI signature is redesigned --
      // midiMessage/midiType went away with the track rework, see track.h.
namespace
{
    // Last MIDI output sent per track, so trackClockTask only re-sends on
    // change. Sized TRACK_MAX_CCS per track; lives here (not on Track)
    // since it's a send-timing concern of this task, not track state.
    midiMessage lastSent[TOTAL_TRACKS][TRACK_MAX_CCS] = {};
    uint8_t lastSentCount[TOTAL_TRACKS] = {};

    bool sameMessage(const midiMessage &a, const midiMessage &b)
    {
        return a.type == b.type && a.channel == b.channel && a.data1 == b.data1 && a.data2 == b.data2;
    }

    void sendMessage(const midiMessage &m)
    {
        switch (m.type)
        {
        case CC:
            midi1.sendControlChange(m.data1, m.data2, m.channel + 1);
            usbMidiSend(0xB0 | m.channel, m.data1, m.data2);
            break;
        case NoteOn:
            midi1.sendNoteOn(m.data1, m.data2, m.channel + 1);
            usbMidiSend(0x90 | m.channel, m.data1, m.data2);
            break;
        case NoteOff:
            midi1.sendNoteOff(m.data1, m.data2, m.channel + 1);
            usbMidiSend(0x80 | m.channel, m.data1, m.data2);
            break;
        case SysEx:
            // TODO: not implemented -- no Track::getMidiOutput() path emits SysEx yet.
            break;
        }
    }
}
#endif

void trackClockTask(void *pvParameters)
{
    static double lastMs = 0;
    for (;;)
    {
        double nowMs = (double)millis();
        double dtMs = nowMs - lastMs;
        lastMs = nowMs;

        // Quarter-note phase delta for this cycle, fed into every synced
        // track's tick(). Unsynced: derived from dtMs and internalBpm.
        // MIDI/USB: drained from consumeQuarterNotePhaseDelta().
        double qnPhaseDelta = (syncSource == SyncSource::NONE)
                                   ? (internalBpm / 60000.0) * dtMs
                                   : consumeQuarterNotePhaseDelta();

        // tick() advances every tickable track (not just the visible ones,
        // and not DUO tracks -- see App.h's tickableTracks[]) so LFO phase /
        // sequencer step keeps exact timing regardless of pot input or
        // scroll position -- see Track::tick()'s doc comment.
        for (uint8_t i = 0; i < tickableTrackCount; i++)
        {
            Track *t = tickableTracks[i];
            t->tick(dtMs, qnPhaseDelta);

#if 0 // TODO: re-enable with the change-detected send above.
            midiMessage out[TRACK_MAX_CCS];
            uint8_t count = t->getMidiOutput(out);

            for (uint8_t c = 0; c < count; c++)
            {
                bool changed = c >= lastSentCount[i] || !sameMessage(out[c], lastSent[i][c]);
                if (changed)
                {
                    sendMessage(out[c]);
                    lastSent[i][c] = out[c];
                }
            }
            lastSentCount[i] = count;
#endif
        }

        vTaskDelay(pdMS_TO_TICKS(TRACK_CLOCK_PERIOD_MS));
    }
}
