/**
 * @file usbMidi.cpp
 * @brief TinyUSB MIDI descriptor registration and raw packet I/O. Compiled
 * out to no-ops when USE_USB_MIDI (boardConfig.h) is undefined -- e.g. while
 * debugging over the Serial monitor, since enumerating the USB-MIDI device
 * alongside Serial can make the port unreliable to attach to.
 */
#include "boardConfig.h"
#include "./driver/usbMidi.h"
#include "./driver/uartMidi.h"

#ifdef USE_USB_MIDI
#include "esp32-hal-tinyusb.h"
#include "tusb.h"

#define USB_MIDI_CLOCK 0xF8

#define USB_MIDI_EP_SIZE 64

static bool usb_midi_interface_loaded = false;

extern "C" uint16_t tusb_midi_load_descriptor(uint8_t *dst, uint8_t *itf)
{
    if (usb_midi_interface_loaded)
    {
        return 0;
    }
    usb_midi_interface_loaded = true;

    uint8_t str_index = tinyusb_add_string_descriptor("SEVENIST CONTROLLER MIDI");
    uint8_t ep_out = tinyusb_get_free_out_endpoint();
    uint8_t ep_in = tinyusb_get_free_in_endpoint();
    if (ep_out == 0 || ep_in == 0)
    {
        return 0;
    }

    uint8_t descriptor[TUD_MIDI_DESC_LEN] = {
        TUD_MIDI_DESCRIPTOR(*itf, str_index, ep_out, (uint8_t)(0x80 | ep_in), USB_MIDI_EP_SIZE)};
    *itf += 2; // MIDI consumes two interface numbers: Audio Control + MIDIStreaming
    memcpy(dst, descriptor, TUD_MIDI_DESC_LEN);
    return TUD_MIDI_DESC_LEN;
}

bool initUsbMidi()
{
    return tinyusb_enable_interface(USB_INTERFACE_MIDI, TUD_MIDI_DESC_LEN, tusb_midi_load_descriptor) == ESP_OK;
}

void usbMidiRead()
{
    uint8_t packet[4];
    while (tud_midi_packet_read(packet))
    {
        // packet[0] = cable/code index, packet[1..3] = MIDI status/data bytes.
        // Only the clock byte (0xF8, drives synced tracks via
        // onSyncPulse()) is handled so far -- everything else is still
        // discarded (mirrors uartMidi.h's DIN-side TODOs on note/CC).
        if (packet[1] == USB_MIDI_CLOCK)
            onSyncPulse(SyncSource::USB);
    }
}

void usbMidiSend(uint8_t status, uint8_t data1, uint8_t data2)
{
    uint8_t packet[4] = {(uint8_t)(status >> 4), status, data1, data2};
    tud_midi_packet_write(packet);
}

#else // !USE_USB_MIDI

bool initUsbMidi() { return false; }
void usbMidiRead() {}
void usbMidiSend(uint8_t status, uint8_t data1, uint8_t data2) { (void)status; (void)data1; (void)data2; }

#endif
