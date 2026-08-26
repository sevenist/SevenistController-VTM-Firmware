/**
 * @file App.cpp
 * @brief Defines every extern declared in App.h (hardware singletons +
 * app-layer state) and implements App::update()/render()/createTrack()/
 * deleteTrack(). See App.h's file comment for why both live in one pair of
 * files, and its App class comment for the update()/render() rate split.
 */
#include "App.h"
#include "GraphicsAssets.h"
#include "../Components/menus.h"
#include "../Components/GUIWidgets.h"
#include <algorithm>

// Declared here rather than in GUIWidgets.h: that header stays free of
// track.h (see its file comment/CLAUDE.md's back-include rule), and
// DualParams/DualEditState are only usable together once a caller already
// has both track.h (via App.h) and GUIWidgets.h -- App.cpp is that caller.
void buildDualFieldList(QCFieldList &out, DualParams &params, DualEditState &edit);

// ---------------------------------------------------------------------
// Animation opn StartUp
// ---------------------------------------------------------------------

void playStartupAnim()
{
    for (int i = 0; i < LOADING7_FRAME_COUNT; i++)
    {
        oled.clearBuffer();
        oled.drawXBM((SCREEN_W - LOADING7_WIDTH) / 2, 5, LOADING7_WIDTH, LOADING7_HEIGHT, loading7[i]);
        oled.sendBuffer();
        delay(33);
    }
}

// ---------------------------------------------------------------------
// Hardware singletons
// ---------------------------------------------------------------------

Quadrature *pots[] = {nullptr};
Multiplexer mux(MuxMode::DualMux3Bits, S1, S2, S3, -1, ADC1, ADC2);

Encoder encoder(ENCODER_A, ENCODER_B, ENCODER_BTN);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R2, U8X8_PIN_NONE, OLEDSCK, OLEDSDA);

CRGB leds[LED_NBR];

void initLeds()
{
    FastLED.addLeds<WS2812B, LEDRGB_PIN, GBR>(leds, LED_NBR);
    FastLED.setBrightness(LED_BRIGHTNESS);
}

namespace
{
    bool ledsDirty = false;
}

void setLed(CRGB color, uint8_t index)
{
    if (index >= LED_NBR)
        return;
    if (leds[index] == color)
        return; // unchanged -- don't dirty the strip over a no-op write
    leds[index] = color;
    ledsDirty = true;
}

void setLeds(const CRGB *colors, uint8_t colorsLength, uint8_t startIndex)
{
    if (trackCount == 0)
        return;

    // Paired-per-track LED-slot space: slot 2n/2n+1 = track slot n's
    // upper/lower LED. Wrap length is in LED slots (2 per visible track),
    // not raw physical LED_NBR.
    uint8_t wrapLen = 2 * std::min<uint8_t>(trackCount, VISIBLE_SLOTS);
    for (uint8_t i = 0; i < colorsLength; i++)
    {
        uint8_t ledSlot = (startIndex + i) % wrapLen;
        uint8_t trackSlot = ledSlot / 2;
        uint8_t physicalIndex = (ledSlot % 2 == 0) ? mappingLED[trackSlot] : mappingLED[trackSlot + 4];
        setLed(colors[i], physicalIndex);
    }
}

void showLedsIfDirty()
{
    if (!ledsDirty)
        return;
    FastLED.show();
    ledsDirty = false;
}

// ---------------------------------------------------------------------
// App-layer state
// ---------------------------------------------------------------------

CRGB Colors[16] = {
    CRGB::Blue,     // Blue
    CRGB::Magenta,  // Magenta
    CRGB::Green,    // Green
    CRGB::Yellow,   // Yellow
    CRGB::Cyan,     // Cyan
    CRGB::Red,      // Red
    CRGB::Orange,   // Orange
    CRGB::Purple,   // Purple
    CRGB::Teal,     // Teal
    CRGB::Pink,     // Pink
    CRGB::Lime,     // Lime
    CRGB::Lavender, // Lavender
    CRGB::Brown,    // Brown
    CRGB::Beige,    // Beige
    CRGB::Maroon,   // Maroon
    CRGB::Navy      // Navy
};

Track *tracks[TOTAL_TRACKS] = {};
uint8_t trackCount = 0;
Track *tickableTracks[TOTAL_TRACKS] = {};
uint8_t tickableTrackCount = 0;
int16_t cursorIndex = 0;
volatile AppMode appMode = AppMode::LIVE;
ViewMode viewMode = ViewMode::NORMAL;
int16_t insertIndex = 0;
InsertOrigin insertOrigin = InsertOrigin::LIVE_DEBUG;
ControlType pendingInsertType = ControlType::DUAL;

// Constructed with a null strand head -- menuView (menus.cpp) is a global
// in a different translation unit, so its init order relative to this one
// isn't guaranteed. Never read until App::update()'s LIVE -> MENU
// transition calls menu.enter(&menuView), which only happens well after
// setup() (and therefore all static init) has completed.
MenuManager menu(nullptr);

namespace
{
    // First tracks[] index of the on-screen/on-LED VISIBLE_SLOTS window.
    // Distinct from cursorIndex (the selected track): the window stays put
    // while cursorIndex moves within it in ViewMode::NORMAL, sliding only by
    // exactly as much as the cursor overshoots an edge by (see
    // updateScrollWindow() below); ViewMode::PAGED instead jumps a full page.
    int16_t windowStart = 0;

    // Same on-screen-window concept as windowStart above, but for the menu
    // list (MENU_VISIBLE_ROWS rows of MENU_ROW_HEIGHT px each) instead of
    // the VISIBLE_SLOTS track columns -- MenuManager (Menu.h/.cpp) only
    // tracks which sibling is selected (index), not which ones are
    // on-screen, so App.cpp owns this the same way it owns windowStart.
    // First siblings[] index in the on-screen window; see
    // updateMenuScrollWindow().
    int16_t menuWindowStart = 0;

    // siblings[0] as of the last updateMenuScrollWindow() call -- lets it
    // tell a menu.enter() strand switch (which resets menu.index to 0/
    // newIndex on an unrelated strand) apart from menu.next()/back() moving
    // within the same strand, since MenuManager itself exposes no signal
    // for "the strand changed" beyond the sibling array's identity.
    MenuNode *lastMenuStrand = nullptr;

    // Same sliding behavior as updateScrollWindow() below, keyed on
    // menu.index/menu.siblingCount instead of cursorIndex/trackCount.
    // Called from App::render() every MENU-mode frame, so it applies
    // regardless of which action (button press, encoder, a tree node's own
    // mgr.enter()) moved menu.index or switched strands.
    void updateMenuScrollWindow()
    {
        if (menu.siblings[0] != lastMenuStrand)
        {
            lastMenuStrand = menu.siblings[0];
            menuWindowStart = 0;
        }

        int16_t maxWindowStart = (menu.siblingCount > MENU_VISIBLE_ROWS) ? (int16_t)(menu.siblingCount - MENU_VISIBLE_ROWS) : 0;

        if (menu.index < menuWindowStart)
            menuWindowStart = (int16_t)menu.index;
        else if (menu.index >= menuWindowStart + MENU_VISIBLE_ROWS)
            menuWindowStart = (int16_t)(menu.index - MENU_VISIBLE_ROWS + 1);

        menuWindowStart = std::clamp(menuWindowStart, (int16_t)0, maxWindowStart);
    }

    // Slides windowStart to keep cursorIndex inside [windowStart, windowStart
    // + VISIBLE_SLOTS), then re-clamps to the populated range (also needed
    // after trackCount shrinks, e.g. deleteTrack()).
    //  NORMAL: windowStart tracks cursorIndex 1:1 whenever the cursor is
    //          outside the window, and doesn't move at all while the cursor
    //          is already inside it (smooth scroll, not a page jump).
    //  PAGED:  windowStart snaps to the VISIBLE_SLOTS-aligned page containing
    //          cursorIndex, so the whole window jumps at once when the
    //          cursor crosses a page boundary.
    void updateScrollWindow()
    {
        int16_t maxWindowStart = (trackCount > VISIBLE_SLOTS) ? (trackCount - VISIBLE_SLOTS) : 0;

        if (viewMode == ViewMode::PAGED)
        {
            windowStart = (int16_t)((cursorIndex / VISIBLE_SLOTS) * VISIBLE_SLOTS);
        }
        else if (cursorIndex < windowStart)
        {
            windowStart = cursorIndex;
        }
        else if (cursorIndex >= windowStart + VISIBLE_SLOTS)
        {
            windowStart = (int16_t)(cursorIndex - VISIBLE_SLOTS + 1);
        }

        windowStart = std::clamp(windowStart, (int16_t)0, maxWindowStart);
    }

    // Same sliding behavior as updateScrollWindow(), but keyed on
    // insertIndex (AppMode::INSERT's boundary cursor, range 0..trackCount
    // inclusive -- one more position than a track index since it addresses
    // the gaps *between* tracks, not the tracks themselves) instead of
    // cursorIndex. Always smooth/1-at-a-time regardless of viewMode -- a
    // page jump while placing an insertion line would be disorienting and
    // the user's spec didn't ask for paged behavior here.
    void updateInsertWindow()
    {
        int16_t maxWindowStart = (trackCount > VISIBLE_SLOTS) ? (trackCount - VISIBLE_SLOTS) : 0;

        if (insertIndex < windowStart)
            windowStart = insertIndex;
        else if (insertIndex > windowStart + VISIBLE_SLOTS)
            windowStart = (int16_t)(insertIndex - VISIBLE_SLOTS);

        windowStart = std::clamp(windowStart, (int16_t)0, maxWindowStart);
    }

    // AppMode::QUICK_CONFIG's field grid + its DUAL list-select/assignment-
    // select cursor (see GUIWidgets.h's DualEditState doc comment) for
    // whichever track cursorIndex points at. Only one QC page is ever
    // on-screen at once, so a single shared instance is enough -- rebuilt
    // by rebuildQuickConfigFields() on entering QUICK_CONFIG (App::
    // changeMode()), not every render() frame, so qcDualEdit's list/
    // assignment cursor survives across frames while the page is open.
    QCFieldList qcFields;
    DualEditState qcDualEdit;

    // Rebuilds qcFields for tracks[cursorIndex] and resets qcDualEdit to a
    // fresh cursor (list 1, slot 0) -- called once per QUICK_CONFIG entry
    // rather than per-frame, so mid-session field edits (and qcDualEdit's
    // own cursor position) aren't clobbered on the next render(). Only
    // DUAL is wired up yet (buildDualFieldList(), GUIWidgets.cpp); other
    // ControlTypes leave qcFields empty until their own builders exist.
    void rebuildQuickConfigFields()
    {
        qcFields.clear();
        if (trackCount == 0)
            return;

        Track *t = tracks[cursorIndex];
        if (t->getControlType() != ControlType::DUAL)
            return;

        qcDualEdit = DualEditState{};
        auto *dual = static_cast<TrackDUO *>(t);
        buildDualFieldList(qcFields, dual->dualCfg, qcDualEdit);
    }
}

void syncScrollWindow()
{
    updateScrollWindow();
}

// ---------------------------------------------------------------------
// App class
// ---------------------------------------------------------------------

bool App::isDebugMode() const
{
    return dbMode;
}

void rebuildTickableTracks()
{
    tickableTrackCount = 0;
    for (uint8_t i = 0; i < trackCount; i++)
    {
        if (tracks[i]->isTickable())
            tickableTracks[tickableTrackCount++] = tracks[i];
    }
}

void App::createTrack(uint8_t position, ControlType type)
{
    if (trackCount >= TOTAL_TRACKS || position > trackCount)
        return;

    for (uint8_t i = trackCount; i > position; i--)
    {
        tracks[i] = tracks[i - 1];
        tracks[i]->index = i;
    }

    // Each new track claims the next palette entry (wrapping mod 16) so
    // consecutive tracks are visibly distinct.
    const CRGB &color = Colors[position % 16];
    TrackConfig config;
    config.controlType = type;
    tracks[position] = makeTrack(position, 0, 0, color, config);
    trackCount++;
    rebuildTickableTracks();
}

void App::deleteTrack(uint8_t position)
{
    if (position >= trackCount)
        return;

    delete tracks[position];
    for (uint8_t i = position; i + 1 < trackCount; i++)
    {
        tracks[i] = tracks[i + 1];
        tracks[i]->index = i;
    }
    trackCount--;
    tracks[trackCount] = nullptr;
    rebuildTickableTracks();
}

namespace
{
    // Which concrete Track subclass makeTrack() (track.h) constructs for a
    // given ControlType -- mirrors its switch exactly, so
    // App::rebuildTrackType() can tell whether a ControlType change
    // actually changes the class. Every ControlType currently maps to a
    // distinct class (DUAL -> TrackDUO/CC, LFO -> TrackLFO,
    // STEPSEQ -> TrackStepSeq, MOTIONSEQ -> TrackMotionSeq), so in practice
    // rebuildTrackType() only no-ops when oldType == newType.
    enum class TrackClass : uint8_t
    {
        CC,
        LFO,
        SEQ,
        MOTION
    };

    TrackClass trackClassFor(ControlType type)
    {
        switch (type)
        {
        case ControlType::LFO:
            return TrackClass::LFO;
        case ControlType::STEPSEQ:
            return TrackClass::SEQ;
        case ControlType::MOTIONSEQ:
            return TrackClass::MOTION;
        default:
            return TrackClass::CC;
        }
    }

}

void App::rebuildTrackType(uint8_t position, ControlType oldType, ControlType newType)
{
    if (position >= trackCount)
        return;

    if (trackClassFor(oldType) == trackClassFor(newType))
        return; // already the right concrete class -- nothing to rebuild

    Track *old = tracks[position];
    double v1, v2;
    old->getValues(v1, v2);

    // TODO: the old subclass's params are dropped here -- each subclass owns
    // only its own struct and Track no longer carries a full TrackConfig, so
    // there's nothing to copy across. Switching type resets that type's
    // config to defaults.
    TrackConfig config;
    config.controlType = newType;
    Track *replacement = makeTrack(position, v1, v2, old->trackColor, config);
    delete old;
    tracks[position] = replacement;
    rebuildTickableTracks();
}

void App::adjustValue(int16_t &value, int8_t delta, int16_t step)
{
    value = (int16_t)(value + delta * step);
}

void App::processEncoder(int8_t delta)
{
    if (encoder.isButtonLongPressed()) // what button long press means in each AppMode:
    {

        if (appMode == AppMode::MENU) // longpress in menu exits the menu
        {
            changeMode(AppMode::LIVE);
        }
        // longpress in live view opens the menu.
        else if (appMode == AppMode::LIVE)
        {
            changeMode(AppMode::MENU);
            menu.enter(&menuView); // always open fresh at the top strand
        }
        else if (appMode == AppMode::QUICK_CONFIG)
        {
            changeMode(AppMode::LIVE);
        }
        else if (appMode == AppMode::INSERT)
        {
            if (insertOrigin == InsertOrigin::MENU_NEW_TRACK)
                cancelInsert();
            else
                changeMode(AppMode::LIVE);
        }
        else if (appMode == AppMode::DELETE_SELECT)
        {
            cancelDelete();
        }
    }

    if (encoder.isButtonPressed()) // what button press means in each AppMode:
    {
        // short press in live view opens the Quick Config screen for
        // cursorIndex's track.
        if (appMode == AppMode::LIVE)
        {
            changeMode(AppMode::QUICK_CONFIG);
        }
        else if (appMode == AppMode::MENU)
        {
            menu.update(0); // trigger current node's action
        }
        else if (appMode == AppMode::INSERT && insertOrigin == InsertOrigin::MENU_NEW_TRACK)
        {
            confirmInsert(pendingInsertType);
        }
        else if (appMode == AppMode::DELETE_SELECT)
        {
            confirmDelete();
        }
    }

    if (appMode == AppMode::MENU)
    {
        if (delta != 0)
            menu.update((int8_t)delta);
        return;
    }
    if (appMode == AppMode::INSERT)
    {
        // Insertion-line cursor: same one-at-a-time scroll as LIVE's
        // cursorIndex, but ranging over trackCount+1 boundary positions
        // (updateInsertWindow()) instead of track indices. Encoder-button
        // dispatch for this mode is handled above; nothing else to do here.
        if (delta != 0)
            insertIndex = std::clamp((int16_t)(insertIndex + delta), (int16_t)0, (int16_t)trackCount);
        updateInsertWindow();
        return;
    }
    if (appMode == AppMode::DELETE_SELECT)
    {
        // Same one-at-a-time scroll as LIVE's cursorIndex -- the delete
        // selector reuses drawTrackSelector() (LIVE's own column view), so it
        // reuses LIVE's cursor logic too. Button dispatch handled above.
        if (trackCount == 0)
        {
            cancelDelete(); // nothing left to delete -- don't strand the user here
            return;
        }
        if (delta != 0)
            moveTrackCursor((int8_t)delta);
        updateScrollWindow();
        return;
    }
    if (appMode == AppMode::QUICK_CONFIG)
    {
        // Placeholder: no field-page scrolling or title-latch yet -- encoder
        // rotation is a no-op here for now. Button dispatch handled above.
        return;
    }

    if (trackCount == 0)
        return; // nothing created yet -- render() prompts the user to create one

    // LIVE-mode track list scrolling (appMode == LIVE here unconditionally
    // -- MENU already returned above). Pot-driven track value updates
    // happen in render(), not here. cursorIndex moves one track per detent,
    // free to reach any populated track; the visible window only repages
    // when the cursor exits it (updateScrollWindow()), so the 4 shown
    // tracks don't slide on every step.
    if (delta != 0)
        moveTrackCursor((int8_t)delta);
    updateScrollWindow();
}

void App::changeMode(AppMode mode)
{
    if (appMode == mode) // no change -> no-op
        return;

    if (mode == AppMode::LIVE)
    {
        oled.setFont(u8g2_font_8x13B_tr);
    }

    if (mode == AppMode::MENU)
    {
        oled.setFont(u8g2_font_8bitclassic_tr);
    }

    if (mode == AppMode::QUICK_CONFIG)
    {
        rebuildQuickConfigFields();
    }

    appMode = mode;
}

namespace
{
    // Shared by confirmInsert()/cancelInsert()/confirmDelete()/cancelDelete()
    // -- all four return to the Track menu strand, entered fresh at its
    // first child like every other menu.enter() call in this file.
    void returnToTrackMenu()
    {
        app.changeMode(AppMode::MENU);
        menu.enter(&menuTrack);
    }
}

void App::confirmInsert(ControlType type)
{
    if (insertOrigin != InsertOrigin::MENU_NEW_TRACK)
        return; // LIVE_DEBUG's INSERT has no confirm behavior

    createTrack((uint8_t)std::clamp<int16_t>(insertIndex, 0, trackCount), type);
    returnToTrackMenu();
}

void App::cancelInsert()
{
    if (insertOrigin != InsertOrigin::MENU_NEW_TRACK)
        return;

    returnToTrackMenu();
}

void App::confirmDelete()
{
    if (trackCount == 0)
        return;

    deleteTrack((uint8_t)std::clamp<int16_t>(cursorIndex, 0, (int16_t)(trackCount - 1)));
    cursorIndex = std::clamp<int16_t>(cursorIndex, 0, (int16_t)(trackCount > 0 ? trackCount - 1 : 0));
    returnToTrackMenu();
}

void App::cancelDelete()
{
    returnToTrackMenu();
}

void App::update()
{

    // Menu/mode logic only -- no drawing, no pots[] reads. See render() for
    // the pot-driven track update and OLED/LED output, run separately at a
    // faster rate (renderTask, ~60Hz) than this (appTask, ~20Hz).

    int16_t scrollDelta = encoder.getDelta();
    processEncoder(scrollDelta);
}

void moveTrackCursor(int8_t delta)
{
    if (trackCount == 0)
        return;
    cursorIndex = std::clamp((int16_t)(cursorIndex + delta), (int16_t)0, (int16_t)(trackCount - 1));
}

namespace
{
    // Short (<=4 char) label for the live track view's column header --
    // menus.cpp has its own full-name controlTypeName(), kept separate
    // since that one's meant for a menu row, not a narrow OLED column.
    const char *controlTypeAbbrev(ControlType t)
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

    // TODO: temporary hardware smoke test -- draws each pot's live value as
    // a bar on the OLED, so OLED/pot/task liveness can be checked directly
    // (e.g. if the encoder button ever appears completely dead, seeing
    // these bars move on pot rotation confirms renderTask/mux/pots are
    // still alive and the fault is isolated to the button path). Shown
    // instead of the "No tracks yet" prompt while trackCount == 0.
    void debugDrawPots()
    {
        uint8_t w = oled.getDisplayWidth();
        uint8_t h = oled.getDisplayHeight();
        uint8_t colWidth = w / POT_NBR;

        oled.clearBuffer();
        oled.setCursor(0, 0);
        oled.print("Add NEW Track !");
        for (int i = 0; i < POT_NBR; i++)
        {
            uint8_t v = (uint8_t)pots[i]->getValue(); // 0-127
            uint8_t barHeight = (uint8_t)(((uint32_t)v * (h - 10)) / 127);
            oled.drawBox(i * colWidth, h - barHeight, colWidth - 2, barHeight);
        }
        oled.sendBuffer();
    }

    // TODO: temporary hardware smoke test -- drives each LED from its pot's
    // live value while no tracks exist yet, so LED hardware/task liveness
    // can be checked directly. Falls back from black-idle to this while
    // trackCount == 0 -- see renderTrackLeds() below.
    void debugDriveLeds()
    {
        for (int i = 0; i < POT_NBR; i++)
        {
            uint8_t v = (uint8_t)pots[i]->getValue(); // 0-127
            setLed(CHSV(v * 2, 255, 255), mappingLED[i]);
        }
        showLedsIfDirty();
    }

    // Column+bar+highlight rendering shared by drawTrackView() (LIVE) and
    // drawTrackSelector() (a tree-defined "browse tracks" node's draw hook)
    // -- both show the same VISIBLE_SLOTS window with cursorIndex's column
    // framed, so picking a track to modify looks exactly like the live view
    // the user already scrolls in LIVE mode. Does not clearBuffer()/
    // sendBuffer() itself -- callers bracket that.
    void drawTracks()
    {
        uint8_t w = oled.getDisplayWidth();
        uint8_t h = oled.getDisplayHeight();
        uint8_t colWidth = w / VISIBLE_SLOTS;
        uint8_t barAreaHeight = h - TRACK_BAR_AREA_TOP;
        uint8_t barWidth = (colWidth - 4) / 2; // two side-by-side bars per column

        for (uint8_t i = 0; i < VISIBLE_SLOTS; i++)
        {
            uint8_t idx = (uint8_t)windowStart + i;
            if (idx >= trackCount)
                continue; // idle slot -- leave blank, matches idle-LED behavior

            Track *t = tracks[idx];
            double v1, v2;
            t->getValues(v1, v2);
            ControlType type = t->getControlType();

            uint8_t x = i * colWidth + 1;

            uint8_t colh1 = (uint8_t)((v1 * barAreaHeight) / 127);
            uint8_t colh2 = (uint8_t)((v2 * barAreaHeight) / 127);

            uint8_t iconh1 = TRACK_ICON_Y;
            uint8_t iconh2 = iconh1 + KNOBVIZ_HEIGHT - 3;

            // differenciate type

            switch (trackClassFor(type))
            {
            case TrackClass::CC:
                oled.drawXBM(x + 3, iconh1, KNOBVIZ_WIDTH, KNOB24X24_HEIGHT, knob24x24[(uint8_t)((v1 * KNOB24X24_FRAME_COUNT) / 128)]);
                oled.drawXBM(x + 3, iconh2, KNOBVIZ_WIDTH, KNOB24X24_HEIGHT, knob24x24[(uint8_t)((v2 * KNOB24X24_FRAME_COUNT) / 128)]);
                break;
            case TrackClass::LFO:
                oled.drawXBM(x + ICONSET_WIDTH / 2, iconh1, ICONSET_WIDTH, ICONSET_HEIGHT, iconSet[(uint8_t)(4 + (v1 * 4) / 128)]);
                oled.drawXBM(x + 3, iconh2, KNOBVIZ_WIDTH, KNOB24X24_HEIGHT, knob24x24[(uint8_t)((v2 * KNOB24X24_FRAME_COUNT) / 128)]);
                break;
            // todo : finish the icon selection (maybe create helper fn + index enum)
            case TrackClass::SEQ:
                // todo : create distinction from SEQ and SUB_SEQ tracks
                oled.drawXBM(x + 3, iconh1, ICONSET_WIDTH, ICONSET_HEIGHT, iconSet[(uint8_t)((v2 * ICONSET_FRAME_COUNT) / 128)]);
                break;
            default:
                oled.drawBox(x + 3, h - colh1, barWidth - 3, colh1);
                oled.drawBox(x + barWidth + 3, h - colh2, barWidth - 3, colh2);
                break;
            }

            oled.setCursor(x + 4, 2);
            oled.print(controlTypeAbbrev(type));

            if (idx == (uint8_t)cursorIndex)
                oled.drawFrame(x, 0, colWidth - 1, h); // highlight the selected track within the window
        }
    }

    // Real LIVE-mode OLED view: one column per VISIBLE_SLOTS track slot,
    // matching the 4 physical pot-pairs -- each shows the track's
    // ControlType abbreviation and two bars for value1/value2 (upper/lower
    // knob). Falls back to the pot-bar debug view (debugDrawPots()) while
    // trackCount == 0, per PROJECT.md's Concept ("the UI prompts the user
    // to create a track when none exist yet").
    void drawTrackView()
    {
        if (trackCount == 0)
        {
            debugDrawPots();
            return;
        }
        oled.clearBuffer();
        drawTracks();
        oled.sendBuffer();
    }

    // AppMode::INSERT's OLED view: the same VISIBLE_SLOTS columns as
    // drawTrackView() (so the user can see exactly where a new track would
    // land relative to its neighbors), but with a vertical line at the
    // insertIndex boundary instead of a frame around a selected column --
    // per the user's spec, insertion is shown as a gap marker between
    // tracks, not a track highlight. insertIndex ranges 0..trackCount
    // (inclusive); column x-position i's left edge is boundary
    // windowStart+i, so the line for insertIndex sits at x = (insertIndex -
    // windowStart) * colWidth, clamped on-screen since updateInsertWindow()
    // (App::update()) keeps insertIndex within [windowStart, windowStart +
    // VISIBLE_SLOTS] whenever the window itself is clamped to the populated
    // range.
    void drawInsertView()
    {
        oled.clearBuffer();
        uint8_t w = oled.getDisplayWidth();
        uint8_t h = oled.getDisplayHeight();
        uint8_t colWidth = w / VISIBLE_SLOTS;
        uint8_t barAreaHeight = h - TRACK_BAR_AREA_TOP;
        uint8_t barWidth = (colWidth - 4) / 2; // two side-by-side bars per column

        for (uint8_t i = 0; i < VISIBLE_SLOTS; i++)
        {
            uint8_t idx = (uint8_t)windowStart + i;
            if (idx >= trackCount)
                continue; // idle slot -- leave blank, matches idle-LED behavior

            Track *t = tracks[idx];
            double v1, v2;
            t->getValues(v1, v2);
            ControlType type = t->getControlType();

            uint8_t x = i * colWidth + 1;

            uint8_t colh1 = (uint8_t)((v1 * barAreaHeight) / 127);
            uint8_t colh2 = (uint8_t)((v2 * barAreaHeight) / 127);

            uint8_t iconh1 = TRACK_ICON_Y;
            uint8_t iconh2 = iconh1 + KNOBVIZ_HEIGHT - 3;

            // differenciate type

            switch (trackClassFor(type))
            {
            case TrackClass::CC:
                oled.drawXBM(x + 3, iconh1, KNOBVIZ_WIDTH, KNOB24X24_HEIGHT, knob24x24[(uint8_t)((v1 * KNOB24X24_FRAME_COUNT) / 128)]);
                oled.drawXBM(x + 3, iconh2, KNOBVIZ_WIDTH, KNOB24X24_HEIGHT, knob24x24[(uint8_t)((v2 * KNOB24X24_FRAME_COUNT) / 128)]);
                break;
            case TrackClass::LFO:
                oled.drawXBM(x + ICONSET_WIDTH / 2, iconh1, ICONSET_WIDTH, ICONSET_HEIGHT, iconSet[(uint8_t)(4 + (v1 * 4) / 128)]);
                oled.drawXBM(x + 3, iconh2, KNOBVIZ_WIDTH, KNOB24X24_HEIGHT, knob24x24[(uint8_t)((v2 * KNOB24X24_FRAME_COUNT) / 128)]);
                break;
            // todo : finish the icon selection (maybe create helper fn + index enum)
            case TrackClass::SEQ:
                // todo : create distinction from SEQ and SUB_SEQ tracks
                oled.drawXBM(x + 3, iconh1, ICONSET_WIDTH, ICONSET_HEIGHT, iconSet[(uint8_t)((v2 * ICONSET_FRAME_COUNT) / 128)]);
                break;
            default:
                oled.drawBox(x + 3, h - colh1, barWidth - 3, colh1);
                oled.drawBox(x + barWidth + 3, h - colh2, barWidth - 3, colh2);
                break;
            }

            oled.setCursor(x + 4, 2);
            oled.print(controlTypeAbbrev(type));

            if (idx == (uint8_t)cursorIndex)
                oled.drawFrame(x, 0, colWidth - 1, h); // highlight the selected track within the window
        }

        int16_t lineOffset = insertIndex - windowStart;
        if (lineOffset >= 0 && lineOffset <= VISIBLE_SLOTS)
        {
            uint8_t lineX = (uint8_t)std::min<int16_t>(lineOffset * colWidth, w - 1);
            oled.drawVLine(lineX, 0, h);
        }

        oled.sendBuffer();
    }

    // AppMode::QUICK_CONFIG's OLED view: title bar (track index +
    // ControlType abbrev) plus qcFields' widget grid, two rows of up to 4
    // fields (QC_SLOT_W/H slots). Only the title bar draws if the
    // ControlType has no field builder yet.
    void drawQuickConfigView()
    {
        oled.clearBuffer();

        if (trackCount == 0)
        {
            oled.setCursor(0, 10);
            oled.print("No tracks yet");
            oled.sendBuffer();
            return;
        }

        Track *t = tracks[cursorIndex];
        oled.setCursor(2, 2);
        oled.print(cursorIndex + 1); // 1-based for display; cursorIndex itself stays 0-based
        oled.print(" - ");
        oled.print(controlTypeAbbrev(t->getControlType()));

        // Must match GUIWidgets.cpp's QC_SLOT_W/H by convention (no shared
        // constant). 4 cols x 32px = SCREEN_W; 2 rows x 26px fit below the
        // title bar within SCREEN_H (12 + 2*26 = 64).
        constexpr int16_t kQcSlotW = 32;
        constexpr int16_t kQcSlotH = 26;
        constexpr size_t kQcCols = 4;
        constexpr int16_t kQcGridY = 8; // below the title bar

        qcFields.windowStart = 0;
        qcFields.render(&oled, 0, kQcGridY, kQcSlotW, kQcCols);
        qcFields.windowStart = kQcCols;
        qcFields.render(&oled, 0, kQcGridY + kQcSlotH, kQcSlotW, kQcCols);
        qcFields.windowStart = 0; // leave iteration state as found for the next frame

        oled.sendBuffer();
    }

    // Real per-frame LED render: each of the VISIBLE_SLOTS on-screen tracks
    // gets its upper/lower LED colors from Track::getLedColors() (base
    // class blends knob1/knob2 start->end by pot value; LFO/Seq/Motion
    // override to compute their own). While trackCount == 0 falls back to
    // debugDriveLeds() (pot-hue smoke test) instead of going dark, so LED
    // hardware/task liveness is directly visible with no tracks created.
    // setLed() only marks the strip dirty on an actual change, so
    // showLedsIfDirty() is a no-op most frames.
    void renderTrackLeds()
    {
        if (trackCount == 0)
        {
            debugDriveLeds();
            return;
        }

        for (uint8_t i = 0; i < VISIBLE_SLOTS; i++)
        {
            uint8_t idx = (uint8_t)windowStart + i;
            if (idx >= trackCount)
            {
                setLed(CRGB::Black, mappingLED[i]);
                setLed(CRGB::Black, mappingLED[i + 4]);
                continue;
            }

            CRGB upper, lower;
            tracks[idx]->getLedColors(upper, lower);
            setLed(upper, mappingLED[i]);
            setLed(lower, mappingLED[i + 4]);
        }
        showLedsIfDirty();
    }

    // Converts a MenuNode::DisplayFunc's `absoluteIndex` (node's position in
    // siblings[], per Menu.h's DisplayFunc doc) into a pixel row relative to
    // menuWindowStart, and reports whether that row is inside the on-screen
    // [0, MENU_VISIBLE_ROWS) window. Scroll-window math lives here (not in
    // MenuManager::render()) so Menu.h/.cpp stay pixel/display-agnostic.
    // Returns false (row unset) when off-window -- callers must skip drawing.
    bool menuRowY(int16_t absoluteIndex, int16_t &rowY)
    {
        int16_t rel = absoluteIndex - menuWindowStart;
        if (rel < 0 || rel >= MENU_VISIBLE_ROWS)
            return false;
        rowY = (int16_t)(rel * MENU_ROW_HEIGHT);
        return true;
    }

    // Default per-row renderer passed to MenuManager::render() for nodes
    // that don't set their own MenuNode::display -- prints the node's name
    // at (x, y), framing the row if it's the selected one. displayCtx is
    // always &oled here (see App::render()); Menu.h/Menu.cpp never see the
    // concrete U8G2 type, so the cast back happens here instead. `y` in is
    // the node's absolute sibling index, not pixels -- see menuRowY().
    void defaultMenuIconDisplay(MenuManager &mgr, MenuNode &node, void *displayCtx, int16_t x, int16_t absoluteIndex, bool selected)
    {
        auto *display = static_cast<U8G2_SSD1306_128X64_NONAME_F_HW_I2C *>(displayCtx);
        int16_t y;
        if (!menuRowY(absoluteIndex, y))
            return;
        if (x > display->getWidth())
            return;
        int16_t *p; // every actionCtx in menus.cpp points at an int16_t

        display->setCursor(x + 1, y);
        display->print(node.name);

        if (node.actionCtx)
        {
            switch (node.varType)
            {
            case MenuVarType::INT_4B:
            case MenuVarType::INT_7B:
                p = (int16_t *)node.actionCtx;
                display->setCursor(display->getWidth() - MENU_VALUE_MARGIN, y),
                    display->print(*p);
                break;
            case MenuVarType::ICON_IDX:
                p = (int16_t *)node.actionCtx;
                display->drawXBM(display->getWidth() - ICONSET_WIDTH, y, ICONSET_WIDTH, ICONSET_HEIGHT, iconSet[*p]);
                break;
            default:
                break;
            }
        }

        if (selected)
            display->drawXBM(display->getCursorX() + CURSORSET_WIDTH, y, CURSORSET_WIDTH, CURSORSET_HEIGHT, cursorSet[0]);
    }

    //! TODO: implement real behavior, this is currently just copy past of defaultMenuRowDisply().
    void defaultMenuRowDisplay(MenuManager &mgr, MenuNode &node, void *displayCtx, int16_t x, int16_t absoluteIndex, bool selected)
    {
        auto *display = static_cast<U8G2_SSD1306_128X64_NONAME_F_HW_I2C *>(displayCtx);
        int16_t y;
        if (!menuRowY(absoluteIndex, y))
            return;
        if (x > display->getWidth())
            return;
        int16_t *p; // every actionCtx in menus.cpp points at an int16_t

        display->setCursor(node.iconIndex != -1 ? x + MENU_ICON_TEXT_OFFSET : x + 2, y + 2);
        if (selected)
        {
            display->drawXBM(display->getCursorX(), y + 4, CURSORSET_WIDTH, CURSORSET_HEIGHT, cursorSet[1]);
            display->setCursor(display->getCursorX() + CURSORSET_WIDTH, display->getCursorY());
        }
        if (node.iconIndex != -1)
            display->drawXBM(0, y, ICONSET_WIDTH, ICONSET_HEIGHT, iconSet[node.iconIndex]);
        display->print(node.name);

        // TEXT reads node.textValue, not actionCtx, so it sits outside the
        // actionCtx guard below.
        if (node.varType == MenuVarType::TEXT)
        {
            if (node.textValue)
            {
                display->setCursor(display->getWidth() - MENU_TEXT_MARGIN, y + 2);
                display->print(node.textValue);
            }
            return;
        }

        if (node.actionCtx)
        {
            switch (node.varType)
            {
            case MenuVarType::INT_4B:
            case MenuVarType::INT_7B:
                p = (int16_t *)node.actionCtx;
                display->setCursor(display->getWidth() - MENU_VALUE_MARGIN, y),
                    display->print(*p);
                break;
            case MenuVarType::ICON_IDX:
                p = (int16_t *)node.actionCtx;
                display->drawXBM(display->getWidth() - ICONSET_WIDTH, y, ICONSET_WIDTH, ICONSET_HEIGHT, iconSet[*p]);
                break;
            default:
                break;
            }
        }
    }
}

void drawTrackSelector()
{
    // Same column/bar/highlight rendering as the LIVE view (drawTrackView()),
    // reused wherever cursorIndex needs to be shown/browsed outside LIVE
    // itself: AppMode::DELETE_SELECT's full-screen delete picker (App::
    // render()) and, in principle, a tree-defined "browse tracks" node's
    // draw hook. Doesn't clearBuffer()/sendBuffer() itself -- callers bracket
    // that (both of App::render()'s MENU and DELETE_SELECT branches already
    // do). Falls back to a plain message instead of the LIVE view's pot-bar
    // -debug screen while trackCount == 0 -- pots aren't a relevant debug
    // signal outside LIVE.
    if (trackCount == 0)
    {
        oled.setCursor(0, 10);
        oled.print("No tracks yet");
        return;
    }
    drawTracks();
}
 
void App::render()
{
    if (appMode == AppMode::MENU)
    {
        updateMenuScrollWindow();
        oled.clearBuffer();
        menu.render(&oled, defaultMenuRowDisplay);
        oled.sendBuffer();
    }
    else if (appMode == AppMode::INSERT)
    {
        drawInsertView();
    }
    else if (appMode == AppMode::QUICK_CONFIG)
    {
        drawQuickConfigView();
    }
    else if (appMode == AppMode::DELETE_SELECT)
    {
        // drawTrackSelector() doesn't bracket clearBuffer()/sendBuffer()
        // itself (designed for MENU-mode drawing to bracket it) -- do it
        // here instead, same as drawTrackView()/drawInsertView() do for
        // their own full-screen modes.
        oled.clearBuffer();
        drawTrackSelector();
        oled.sendBuffer();
    }
    else if (appMode == AppMode::LIVE)
    {

        drawTrackView();
    }

    // LEDs: real per-track colors via Track::getLedColors(); idle slots (no
    // track there, or trackCount == 0) go black. Runs regardless of appMode
    // -- LEDs reflect LIVE-visible tracks even while the menu is open.
    renderTrackLeds();

    if (trackCount == 0)
        return; // nothing to update

    // QUICK_CONFIG reserves all 8 pots for qcFields' widgets instead of
    // track value1/value2 -- pot i drives qcFields.fields[i] directly, one
    // physical pot per field (see GUIWidgets.h's file comment), independent
    // of the windowStart/selectedIndex on-screen grid position used only
    // for drawQuickConfigView()'s layout.
    if (appMode == AppMode::QUICK_CONFIG)
    {
        for (size_t i = 0; i < qcFields.fieldCount && i < POT_NBR; i++)
        {
            QCField &field = qcFields.fields[i];
            if (!field.applyDelta)
                continue;
            // Unlike the LIVE-mode loop below, no POT_DELTA_GAIN here --
            // that gain scales into Track::update()'s 0-127.9 double
            // accumulator, whereas QCField::applyDelta wants a small
            // per-detent int8_t step (same convention as MenuNode::
            // EncoderFunc), so the raw quadrature delta is rounded and
            // sign-clamped to +-1 per frame instead.
            double delta = pots[i]->getDelta(); // auto-resets
            if (delta == 0.0)
                continue;
            field.applyDelta(field.ctx, delta > 0.0 ? (int8_t)1 : (int8_t)-1);
        }
        return;
    }

    // Pots are physically independent of the rotary encoder, so pot-driven
    // track updates keep running even while the menu owns the encoder for
    // navigation -- only encoder input is gated by appMode. Without this,
    // turning a knob while browsing the menu was silently ignored.
    // Only the VISIBLE_SLOTS tracks currently in the visible window get
    // their pot deltas applied here (windowStart, not cursorIndex -- pot i
    // always drives whatever track is in on-screen column i, regardless of
    // which one is selected); every other track's clock-driven state (LFO
    // phase, sequencer step) advances via tick() from trackClockTask instead
    // (see track.h), independent of pot input or scroll position.
    for (int i = 0; i < VISIBLE_SLOTS; i++)
    {
        uint8_t idx = windowStart + i;
        if (idx >= trackCount)
            break; // no wrap here: windowStart is already clamped to the populated range above

        double delta1 = pots[i]->getDelta() * POT_DELTA_GAIN; // auto-resets
        double delta2 = pots[i + 4]->getDelta() * POT_DELTA_GAIN;
        if (delta1 == 0.0 && delta2 == 0.0)
        {
            continue; // no movement, nothing to send
        }
        tracks[idx]->update(delta1, delta2);
    }
}

App app;
