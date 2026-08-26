/**
 * @file menus.cpp
 * @brief Builds the concrete menu tree on top of Menu.h's primitives.
 * menuView/menuTrack/menuConfig/menuPresets/menuExit are the top-level
 * siblings (menu is rooted directly at menuView, see App.cpp's MenuManager
 * construction); each of the others enter()s its own child strand, with
 * .parent set here so the shared menuBack can walk back up.
 * Track's New/Delete entries hand off to AppMode::INSERT/DELETE_SELECT (see
 * App.h) to pick the insertion point / track to remove, then call back into
 * app.createTrack()/deleteTrack() on confirm; Presets is wired to the
 * persistence layer's loadPreset()/savePreset(); Config exposes the MIDI
 * sync source (uartMidi.h's syncSource: None/MIDI/USB) and PPQ count
 * (ppqSetting) that drive synced tracks' quarter-note phase -- see
 * Track::tick()'s doc comment in track.h.
 */
#include "menus.h"
#include "../app/App.h"
#include "../driver/persistence.h"
#include <algorithm>

MenuNode menuView("View");
MenuNode menuTrack("Track");
MenuNode menuConfig("Config");
MenuNode menuPresets("Presets");
MenuNode menuExit("Exit");
MenuNode menuBack("Back");

// view entries:
MenuNode menuQuickEdit("Quick Config");
MenuNode menuLive("Live !");
MenuNode menuDebug("Dev/Debug");

// Track entries:
MenuNode menuTrackType("Type:");
MenuNode menuTrackNew("New Track");
MenuNode menuTrackDelete("Delete Track");

// Preset entries:
MenuNode menuPresetSelect("Preset Slot:");
MenuNode menuLoad("Load");
MenuNode menuSave("Save");

// Config entries:
MenuNode menuSyncSource("MIDI Sync:");
MenuNode menuPpqCount("PPQ Count:");
MenuNode menuTempo("Tempo:");

// Selected preset slot, adjusted by menuPresetSelect while latched (see
// buildMenuTree()), clamped to the real slot count by presetSelectEncoder().
int16_t selectedPreset = 0;

namespace
{
    // ControlType the next New Track is built with, cycled by menuTrackType
    // while latched. Held as ControlType (not an int16_t via actionCtx) since
    // the row shows a name, not a number (MenuVarType::TEXT).
    ControlType newTrackType = ControlType::DUAL;

    const char *controlTypeName(ControlType t)
    {
        switch (t)
        {
        case ControlType::DUAL:
            return "DUO";
        case ControlType::LFO:
            return "LFO";
        case ControlType::STEPSEQ:
            return "SEQ";
        case ControlType::MOTIONSEQ:
            return "MOT";
        }
        return "?";
    }

    const char *syncSourceName(SyncSource s)
    {
        switch (s)
        {
        case SyncSource::NONE:
            return "None";
        case SyncSource::MIDI:
            return "MIDI";
        case SyncSource::USB:
            return "USB";
        }
        return "?";
    }

#define SYNC_SOURCE_COUNT 3
#define PPQ_MIN 1
#define PPQ_MAX 96
#define TEMPO_MIN 20
#define TEMPO_MAX 300

    void exitAction(MenuManager &mgr, void *)
    {
        app.changeMode(AppMode::LIVE);
    }

    // Walks up out of the current strand. Reads siblings[0]->parent rather
    // than current->parent so this one node can sit in every strand -- see
    // MenuNode::parent. Top-level strands have no owner, so fall back to the
    // root (menuView).
    void backAction(MenuManager &mgr, void *)
    {
        MenuNode *owner = (mgr.siblingCount > 0) ? mgr.siblings[0]->parent : nullptr;
        if (owner && owner->parent)
            mgr.enter(owner->parent);
        else
            mgr.enter(&menuView);
    }

    void presetSelectAction(MenuManager &mgr, void *)
    {
        mgr.latch(&menuPresetSelect);
    }

    void presetSelectEncoder(MenuManager &mgr, void *, int8_t delta)
    {
        App::adjustValue(selectedPreset, delta);
        selectedPreset = std::clamp<int16_t>(selectedPreset, 0, PRESET_SLOT_COUNT - 1);
    }

    void loadAction(MenuManager &mgr, void *)
    {
        loadPreset((uint8_t)selectedPreset);
    }

    void saveAction(MenuManager &mgr, void *)
    {
        // TODO: no name editor yet -- savePreset() stores the empty string,
        // which peekPresetName() will read back as "".
        savePreset((uint8_t)selectedPreset, "");
    }

    void syncSourceAction(MenuManager &mgr, void *)
    {
        mgr.latch(&menuSyncSource);
    }

    void syncSourceEncoder(MenuManager &mgr, void *, int8_t delta)
    {
        int16_t s = (int16_t)syncSource + delta;
        // Wrap both ways -- only 3 values, so cycling beats clamping here.
        s = (int16_t)((s % SYNC_SOURCE_COUNT + SYNC_SOURCE_COUNT) % SYNC_SOURCE_COUNT);
        syncSource = (SyncSource)s;
        menuSyncSource.textValue = syncSourceName(syncSource);
    }

    void ppqCountAction(MenuManager &mgr, void *)
    {
        mgr.latch(&menuPpqCount);
    }

    void ppqCountEncoder(MenuManager &mgr, void *, int8_t delta)
    {
        App::adjustValue(ppqSetting, delta);
        ppqSetting = std::clamp<int16_t>(ppqSetting, PPQ_MIN, PPQ_MAX);
    }

    void tempoAction(MenuManager &mgr, void *)
    {
        mgr.latch(&menuTempo);
    }

    void tempoEncoder(MenuManager &mgr, void *, int8_t delta)
    {
        App::adjustValue(internalBpm, delta);
        internalBpm = std::clamp<int16_t>(internalBpm, TEMPO_MIN, TEMPO_MAX);
    }

    void trackTypeAction(MenuManager &mgr, void *)
    {
        mgr.latch(&menuTrackType);
    }

    void trackTypeEncoder(MenuManager &mgr, void *, int8_t delta)
    {
        int16_t t = (int16_t)newTrackType + delta;
        // Wrap both ways -- only 4 values, so cycling beats clamping here.
        t = (int16_t)((t % CONTROL_TYPE_COUNT + CONTROL_TYPE_COUNT) % CONTROL_TYPE_COUNT);
        newTrackType = (ControlType)t;
        menuTrackType.textValue = controlTypeName(newTrackType);
    }

    // Hands off to AppMode::INSERT (see App.h) to pick the insertion
    // point; confirm/cancel happens there.
    void trackNewAction(MenuManager &mgr, void *)
    {
        if (trackCount >= TOTAL_TRACKS)
            return;

        insertIndex = std::clamp<int16_t>(cursorIndex, 0, trackCount);
        pendingInsertType = newTrackType;
        insertOrigin = InsertOrigin::MENU_NEW_TRACK;
        app.changeMode(AppMode::INSERT);
    }

    // Hands off to AppMode::DELETE_SELECT (see App.h) to pick a track to
    // remove; confirm/cancel happens there.
    void trackDeleteAction(MenuManager &mgr, void *)
    {
        if (trackCount == 0)
            return;

        app.changeMode(AppMode::DELETE_SELECT);
    }

    void bindActions()
    {
        menuExit.action = exitAction;
        menuBack.action = backAction;

        menuView.action = [](MenuManager &mgr, void *)
        {
            mgr.enter(&menuQuickEdit);
        };
        menuTrack.action = [](MenuManager &mgr, void *)
        {
            mgr.enter(&menuTrackType);
        };
        menuPresets.action = [](MenuManager &mgr, void *)
        {
            mgr.enter(&menuPresetSelect);
        };
        menuConfig.action = [](MenuManager &mgr, void *)
        {
            mgr.enter(&menuSyncSource);
        };

        menuTrackType.action = trackTypeAction;
        menuTrackNew.action = trackNewAction;
        menuTrackDelete.action = trackDeleteAction;

        menuPresetSelect.action = presetSelectAction;
        menuLoad.action = loadAction;
        menuSave.action = saveAction;

        menuSyncSource.action = syncSourceAction;
        menuPpqCount.action = ppqCountAction;
        menuTempo.action = tempoAction;
    }

    void bindEncoders()
    {
        menuPresetSelect.onEncoder = presetSelectEncoder;
        menuTrackType.onEncoder = trackTypeEncoder;
        menuSyncSource.onEncoder = syncSourceEncoder;
        menuPpqCount.onEncoder = ppqCountEncoder;
        menuTempo.onEncoder = tempoEncoder;
    }

    // actionCtx doubles as "this row shows a value" for the default row
    // renderer (App.cpp), so only nodes whose value is an int16_t get one.
    // menuTrackType/menuSyncSource show a name via textValue instead.
    void bindCtx()
    {
        menuPresetSelect.actionCtx = &selectedPreset;
        menuPpqCount.actionCtx = &ppqSetting;
        menuTempo.actionCtx = &internalBpm;
    }

    // .parent is what backAction() walks up through; the top-level strand's
    // nodes keep parent == nullptr. menuBack needs none -- it's shared across
    // strands and its own parent is never read.
    void bindParents()
    {
        menuQuickEdit.parent = &menuView;
        menuLive.parent = &menuView;
        menuDebug.parent = &menuView;

        menuTrackType.parent = &menuTrack;
        menuTrackNew.parent = &menuTrack;
        menuTrackDelete.parent = &menuTrack;

        menuPresetSelect.parent = &menuPresets;
        menuLoad.parent = &menuPresets;
        menuSave.parent = &menuPresets;

        menuSyncSource.parent = &menuConfig;
        menuPpqCount.parent = &menuConfig;
        menuTempo.parent = &menuConfig;
    }
}

void buildMenuTree()
{
    linkSiblings(&menuView, &menuTrack, &menuConfig, &menuPresets, &menuExit);
    linkSiblings(&menuQuickEdit, &menuLive, &menuDebug, &menuBack);
    linkSiblings(&menuTrackType, &menuTrackNew, &menuTrackDelete, &menuBack);
    linkSiblings(&menuPresetSelect, &menuLoad, &menuSave, &menuBack);
    linkSiblings(&menuSyncSource, &menuPpqCount, &menuTempo, &menuBack);

    menuPresetSelect.varType = MenuVarType::ICON_IDX;
    menuTrackType.varType = MenuVarType::TEXT;
    menuTrackType.textValue = controlTypeName(newTrackType);

    menuSyncSource.varType = MenuVarType::TEXT;
    menuSyncSource.textValue = syncSourceName(syncSource);
    menuPpqCount.varType = MenuVarType::INT_7B;
    menuTempo.varType = MenuVarType::INT_7B;

    menuView.iconIndex = 1;
    menuSave.iconIndex = 35;
    menuLoad.iconIndex = 36;
    menuTrack.iconIndex = 53;
    menuConfig.iconIndex = 9;
    menuPresets.iconIndex = 0;
    menuExit.iconIndex = 41;
    menuBack.iconIndex = 50;

    bindParents();
    bindCtx();
    bindActions();
    bindEncoders();
}
