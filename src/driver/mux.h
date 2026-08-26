/**
 * @file mux.h
 * @brief Analog multiplexer driver -- polls up to 16 channels through a
 * shared set of select lines into `_raw[]` for the pot/quadrature code to
 * read back out.
 */
#pragma once
#include <Arduino.h>


#define MUX_PIN_NULL -1
#define MUX_CHANNELS 16

/// SingleMux4Bits: one 16-channel mux, 4 select lines, 1 COM.
/// DualMux3Bits: two 8-channel muxes sharing 3 select lines, 2 COM pins (this board's config, see boardConfig.h).
enum class MuxMode : uint8_t
{
    SingleMux4Bits,
    DualMux3Bits,
};

union Read_pair
{
    int8_t mux1;
    int8_t mux2;
};

/**
 * @brief Drives an analog mux's select lines and polls its channel(s) into
 * `_raw[]`. Single instance (`mux` in App.h/.cpp), polled by
 * `muxPollTask`; other code reads results via read()/getValue() rather than
 * touching `_raw[]` directly.
 */
class Multiplexer
{

public:
    Read_pair rp;
    uint8_t _s0;
    uint8_t _s1;
    uint8_t _s2;
    uint8_t _s3;
    uint8_t _COM;
    uint8_t _COM2;
    uint8_t _mode;
    uint16_t _raw[MUX_CHANNELS]; ///< Last-polled ADC reading per channel, filled by poll().

    /**
     * @brief Configures a mux for the given mode and pin set.
     * @param mode    MuxMode::SingleMux4Bits or MuxMode::DualMux3Bits.
     * @param PIN_s0  Select line S0 (shared by both muxes in dual mode).
     * @param PIN_s1  Select line S1.
     * @param PIN_s2  Select line S2.
     * @param PIN_s3  Select line S3, MuxMode::SingleMux4Bits only (leave -1 for dual mode).
     * @param PIN_COM Common/output pin for mux 1 (ADC input).
     * @param PIN_COM2 Common/output pin for mux 2, MuxMode::DualMux3Bits only.
     */
    Multiplexer(MuxMode mode, int8_t PIN_s0, int8_t PIN_s1, int8_t PIN_s2, int8_t PIN_s3 = -1, int8_t PIN_COM = -1, int8_t PIN_COM2 = -1);

    /// Selects channel addr and takes a fresh ADC reading (bypasses the `_raw[]` cache).
    uint16_t getValue(uint8_t addr);
    /// Configures select/COM pins as In/Out. Call once before poll()/read().
    void init();
    /// Sweeps every channel (0..MUX_CHANNELS-1) into `_raw[]`. Called from muxPollTask.
    void poll();
    /// Returns the last value poll() cached for channel addr, without re-reading hardware.
    uint16_t read(uint8_t addr) const;
    /// Digital-threshold read of channel addr (for button-style inputs behind the mux).
    bool dRead(uint8_t addr);
};