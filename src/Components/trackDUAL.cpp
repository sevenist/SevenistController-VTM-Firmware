/**
 * @file trackDUAL.cpp
 * @brief TrackDUO behavior: each knob's value is expanded through its own
 * CCAssignmentList (every active CC in parallel, transfer-fn-mapped) and
 * sent as MIDI CC directly on pot movement. See track.h for the class-level
 * skeleton.
 */
#include "track.h"
#include "../driver/uartMidi.h"
#include "../driver/usbMidi.h"
#include <cmath>

namespace
{
    // Maps normalized position t (0-1) onto [start,end] through the given
    // curve. BELL is not a monotonic 0-1 mapping and falls back to a sine
    // shape centered at 0.5 -- see track.h's TransferFunction doc.
    uint8_t applyTransferFunction(double t, uint8_t start, uint8_t end, TransferFunction fn)
    {
        switch (fn)
        {
        case TransferFunction::EXP:
            t = t * t;
            break;
        case TransferFunction::LOG:
            t = std::sqrt(t);
            break;
        case TransferFunction::BELL:
            t = -0.5 * std::cos(M_PI + t * M_PI) + 0.5;
            break;
        case TransferFunction::LINEAR:
        default:
            break;
        }
        return (uint8_t)std::round(start + t * (end - start));
    }

    // Sends one knob's value through every active CC in list, each CC using
    // its own transfer function (CCAssignment::tf).
    void sendKnob(double value, const CCAssignmentList &list)
    {
        double t = std::clamp(value, 0.0, 127.0) / 127.0;
        for (uint8_t i = 0; i < list.length; i++)
        {
            const CCAssignment &a = list.ccs[i];
            uint8_t v = applyTransferFunction(t, a.start, a.end, a.tf);
            midi1.sendControlChange(a.cc, v, a.channel + 1);
            usbMidiSend(0xB0 | a.channel, a.cc, v);
        }
    }
}

void TrackDUO::update(double delta1, double delta2)
{
    Track::update(delta1, delta2);

    double v1, v2;
    getValues(v1, v2);

    sendKnob(v1, dualCfg.assignmentList1);
    sendKnob(v2, dualCfg.assignmentList2);
}
