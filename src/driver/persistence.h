/**
 * @file persistence.h
 * @brief Save/load the track list (tracks[]/trackCount) to/from flash, as
 * one of PRESET_SLOT_COUNT preset slots on the LittleFS storage partition
 * (see partitions.csv). See PROJECT.md's persistence note.
 */
#pragma once
#include <cstdint>
#include <cstddef>

#define PRESET_SLOT_COUNT 16
// Max preset name length, not counting the null terminator (SaveHeader
// stores PRESET_NAME_MAX + 1 bytes so the on-flash name is always
// null-terminated regardless of what was written).
#define PRESET_NAME_MAX 15

/// Mounts the LittleFS storage partition, formatting it on first boot if
/// it's never been initialized. Call once from setup(), before
/// loadLastPreset() or any menu save/load action.
void initStorage();

/// Loads tracks[]/trackCount from whichever slot was last saved or loaded
/// (see loadPreset()/savePreset()). Leaves tracks[] empty if no slot has
/// ever been saved. Call once from setup(), after initStorage().
void loadLastPreset();

/// Loads tracks[]/trackCount from preset slot (0..PRESET_SLOT_COUNT-1) and
/// records slot as the new "last" slot. No-op (tracks[] unchanged) if that
/// slot was never saved or fails validation, or slot is out of range.
void loadPreset(uint8_t slot);

/// Serializes tracks[0..trackCount-1] into preset slot (0..PRESET_SLOT_COUNT-1)
/// under name (truncated to PRESET_NAME_MAX chars, always null-terminated)
/// and records slot as the new "last" slot. Call from task context only
/// (e.g. a menu action) -- LittleFS calls can block. No-op if slot is out
/// of range.
void savePreset(uint8_t slot, const char *name);

/// Reads just slot's stored name into outName (a buffer of at least
/// outLen bytes, null-terminated on success) without loading its tracks.
/// Returns false (outName untouched) if slot is out of range, was never
/// saved, or fails header validation (magic/version mismatch) -- callers
/// should show a placeholder like "(empty)" in that case. Used by the
/// Save/Load screen to preview a slot's name before acting on it.
bool peekPresetName(uint8_t slot, char *outName, size_t outLen);
