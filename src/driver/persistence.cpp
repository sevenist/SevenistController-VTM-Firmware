/**
 * @file persistence.cpp
 * @brief LittleFS backing for the 16 preset slots. One file per slot
 * (/presetN.bin: SaveHeader + trackCount variable-length track records,
 * each a TrackHeader followed by its ControlType's params) plus /lastslot.bin
 * (single uint8_t, the last slot successfully saved or loaded). See
 * persistence.h and PROJECT.md's persistence note for the layout rationale.
 */
#include "./driver/persistence.h"
#include <LittleFS.h>
#include <algorithm>
#include <cstring>
#include "./Components/track.h"
#include "./app/App.h"
#include "logger.h"

namespace
{
    constexpr uint32_t kMagic = 0x53455654; // 'SEVT'
    // v5: variable-length records -- fixed TrackHeader + payloadBytes of
    // that track's own ControlType param struct (no shared/oversized
    // layout). Older on-disk versions fail the version check and are
    // rejected on load, not migrated.
    constexpr uint16_t kVersion = 5;
    constexpr const char *kLastSlotPath = "/lastslot.bin";

    // Fixed-size prefix of one track's on-flash record. Byte-copied, so it
    // must stay layout-stable within a given kVersion.
    struct TrackHeader
    {
        uint8_t colorR, colorG, colorB;
        double value1, value2;
        uint8_t type; // ControlType
        uint16_t payloadBytes;
    };

    // Size of the param struct that follows a TrackHeader of this type.
    size_t payloadSizeFor(ControlType type)
    {
        switch (type)
        {
        case ControlType::LFO:
            return sizeof(LfoParams);
        case ControlType::STEPSEQ:
            return sizeof(StepSeqParams);
        case ControlType::MOTIONSEQ:
            return sizeof(MotionSeqParams);
        default:
            return sizeof(DualParams);
        }
    }

    bool isKnownControlType(uint8_t type)
    {
        return type < CONTROL_TYPE_COUNT;
    }

    struct SaveHeader
    {
        uint32_t magic;
        uint16_t version;
        uint8_t trackCount;
        char name[PRESET_NAME_MAX + 1];
    };

    String presetPath(uint8_t slot)
    {
        return "/preset" + String(slot) + ".bin";
    }

    void writeLastSlot(uint8_t slot)
    {
        File f = LittleFS.open(kLastSlotPath, "w");
        if (!f)
        {
            LOG_DEBUG("persistence: failed to open %s for write\n", kLastSlotPath);
            return;
        }
        size_t written = f.write(&slot, sizeof(slot));
        f.close();
        if (written != sizeof(slot))
            LOG_DEBUG("persistence: short write to %s (%u/%u bytes)\n", kLastSlotPath, (unsigned)written, (unsigned)sizeof(slot));
    }

    // Reads and validates a preset file's track records into tracks[]/
    // trackCount. Returns false (tracks[]/trackCount left untouched) on any
    // missing/corrupt/version-mismatched data.
    bool readPresetFile(uint8_t slot)
    {
        String path = presetPath(slot);
        File f = LittleFS.open(path, "r");
        if (!f)
        {
            LOG_DEBUG("persistence: %s does not exist / failed to open\n", path.c_str());
            return false;
        }

        SaveHeader header{};
        if (f.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header))
        {
            LOG_DEBUG("persistence: %s too short to hold a header\n", path.c_str());
            f.close();
            return false;
        }
        if (header.magic != kMagic || header.version != kVersion)
        {
            LOG_DEBUG("persistence: %s bad header (magic=0x%08X version=%u)\n", path.c_str(), (unsigned)header.magic, header.version);
            f.close();
            return false;
        }

        uint8_t count = std::min<uint8_t>(header.trackCount, TOTAL_TRACKS);

        // Records are variable-length, so the whole body has to be parsed
        // before anything is committed -- decode into a staging array first
        // and only touch tracks[] once every record has validated.
        CRGB *colors = new CRGB[count];
        double *values1 = new double[count];
        double *values2 = new double[count];
        TrackConfig *configs = new TrackConfig[count];
        bool ok = true;

        for (uint8_t i = 0; i < count && ok; i++)
        {
            TrackHeader rec{};
            if (f.read(reinterpret_cast<uint8_t *>(&rec), sizeof(rec)) != sizeof(rec))
            {
                LOG_DEBUG("persistence: %s truncated at track %u header\n", path.c_str(), i);
                ok = false;
                break;
            }

            if (!isKnownControlType(rec.type))
            {
                LOG_DEBUG("persistence: %s track %u bad ControlType (%u)\n", path.c_str(), i, rec.type);
                ok = false;
                break;
            }

            ControlType type = (ControlType)rec.type;
            size_t expected = payloadSizeFor(type);
            if (rec.payloadBytes != expected)
            {
                LOG_DEBUG("persistence: %s track %u payload size mismatch (have %u, want %u)\n",
                          path.c_str(), i, rec.payloadBytes, (unsigned)expected);
                ok = false;
                break;
            }

            configs[i].controlType = type;
            void *dest = nullptr;
            switch (type)
            {
            case ControlType::LFO:
                dest = &configs[i].lfo;
                break;
            case ControlType::STEPSEQ:
                dest = &configs[i].stepSeq;
                break;
            case ControlType::MOTIONSEQ:
                dest = &configs[i].motionSeq;
                break;
            default:
                dest = &configs[i].dual;
                break;
            }

            if (f.read(reinterpret_cast<uint8_t *>(dest), expected) != expected)
            {
                LOG_DEBUG("persistence: %s truncated at track %u payload\n", path.c_str(), i);
                ok = false;
                break;
            }

            colors[i] = CRGB(rec.colorR, rec.colorG, rec.colorB);
            values1[i] = rec.value1;
            values2[i] = rec.value2;
        }

        // Trailing bytes mean the file doesn't match what the header claims.
        if (ok && f.position() != f.size())
        {
            LOG_DEBUG("persistence: %s has %u trailing bytes after %u tracks\n",
                      path.c_str(), (unsigned)(f.size() - f.position()), count);
            ok = false;
        }
        f.close();

        if (!ok)
        {
            delete[] colors;
            delete[] values1;
            delete[] values2;
            delete[] configs;
            return false; // corrupt/truncated -- don't partially load
        }

        // Free whatever Track* already occupied every slot this load
        // touches -- both the ones about to be overwritten (0..count-1) and
        // any leftover from a larger previous list (count..trackCount-1),
        // which would otherwise never be deleted once trackCount shrinks
        // below them. Read is already fully validated above, so this only
        // runs once the load is known to succeed.
        uint8_t previousCount = std::max(trackCount, count);
        for (uint8_t i = 0; i < previousCount; i++)
        {
            delete tracks[i];
            tracks[i] = nullptr;
        }

        for (uint8_t i = 0; i < count; i++)
            tracks[i] = makeTrack(i, values1[i], values2[i], colors[i], configs[i]);

        delete[] colors;
        delete[] values1;
        delete[] values2;
        delete[] configs;

        trackCount = count;
        rebuildTickableTracks();
        LOG_DEBUG("persistence: loaded %u tracks from %s\n", count, path.c_str());
        return true;
    }
}

void initStorage()
{
    bool mounted = LittleFS.begin(/*formatOnFail=*/true);
    LOG_DEBUG("persistence: LittleFS.begin() -> %s (total=%u used=%u)\n",
              mounted ? "OK" : "FAILED",
              mounted ? (unsigned)LittleFS.totalBytes() : 0,
              mounted ? (unsigned)LittleFS.usedBytes() : 0);
}

void loadLastPreset()
{
    File f = LittleFS.open(kLastSlotPath, "r");
    if (!f)
    {
        LOG_DEBUG("persistence: %s does not exist -- nothing saved yet\n", kLastSlotPath);
        return; // never saved -- leave tracks[] empty (current default)
    }

    uint8_t slot = 0;
    size_t got = f.read(&slot, sizeof(slot));
    f.close();

    if (got != sizeof(slot) || slot >= PRESET_SLOT_COUNT)
    {
        LOG_DEBUG("persistence: %s unreadable or out-of-range slot (%u)\n", kLastSlotPath, slot);
        return;
    }

    LOG_DEBUG("persistence: last slot was %u, loading it\n", slot);
    readPresetFile(slot);
}

void loadPreset(uint8_t slot)
{
    if (slot >= PRESET_SLOT_COUNT)
        return;

    if (readPresetFile(slot))
        writeLastSlot(slot);
}

bool peekPresetName(uint8_t slot, char *outName, size_t outLen)
{
    if (slot >= PRESET_SLOT_COUNT || outLen == 0)
        return false;

    String path = presetPath(slot);
    File f = LittleFS.open(path, "r");
    if (!f)
        return false;

    SaveHeader header{};
    size_t got = f.read(reinterpret_cast<uint8_t *>(&header), sizeof(header));
    f.close();

    if (got != sizeof(header) || header.magic != kMagic || header.version != kVersion)
        return false;

    // header.name is always null-terminated within its own bounds (see
    // savePreset()), so a plain bounded copy is enough.
    size_t n = std::min(outLen - 1, sizeof(header.name));
    strncpy(outName, header.name, n);
    outName[n] = '\0';
    return true;
}

void savePreset(uint8_t slot, const char *name)
{
    if (slot >= PRESET_SLOT_COUNT)
        return;

    SaveHeader header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.trackCount = trackCount;
    strncpy(header.name, name ? name : "", PRESET_NAME_MAX);
    header.name[PRESET_NAME_MAX] = '\0';

    String path = presetPath(slot);
    File f = LittleFS.open(path, "w");
    if (!f)
    {
        LOG_DEBUG("persistence: failed to open %s for write\n", path.c_str());
        return;
    }

    size_t headerWritten = f.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
    bool bodyOk = true;

    for (uint8_t i = 0; i < trackCount && bodyOk; i++)
    {
        Track *t = tracks[i];
        ControlType type = t->getControlType();
        size_t payload = payloadSizeFor(type);

        TrackHeader rec{};
        rec.colorR = t->trackColor.r;
        rec.colorG = t->trackColor.g;
        rec.colorB = t->trackColor.b;
        t->getValues(rec.value1, rec.value2);
        rec.type = (uint8_t)type;
        rec.payloadBytes = (uint16_t)payload;

        // Each subclass owns only its own param struct, so the payload comes
        // straight off the concrete track.
        const void *src = nullptr;
        switch (type)
        {
        case ControlType::LFO:
            src = &static_cast<TrackLFO *>(t)->lfoCfg;
            break;
        case ControlType::STEPSEQ:
            src = &static_cast<TrackStepSeq *>(t)->stepCfg;
            break;
        case ControlType::MOTIONSEQ:
            src = &static_cast<TrackMotionSeq *>(t)->motionCfg;
            break;
        default:
            src = &static_cast<TrackDUO *>(t)->dualCfg;
            break;
        }

        if (f.write(reinterpret_cast<const uint8_t *>(&rec), sizeof(rec)) != sizeof(rec) ||
            f.write(reinterpret_cast<const uint8_t *>(src), payload) != payload)
        {
            LOG_DEBUG("persistence: short write to %s at track %u\n", path.c_str(), i);
            bodyOk = false;
        }
    }
    f.close();

    if (headerWritten != sizeof(header) || !bodyOk)
    {
        LOG_DEBUG("persistence: incomplete write to %s (header %u/%u)\n",
                  path.c_str(), (unsigned)headerWritten, (unsigned)sizeof(header));
        return; // don't record this slot as "last" if the write didn't fully land
    }

    LOG_DEBUG("persistence: saved %u tracks to %s\n", trackCount, path.c_str());
    writeLastSlot(slot);
}
