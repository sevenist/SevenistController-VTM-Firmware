/**
 * @file App.h
 * @brief Two things live in this single header, deliberately kept together
 * (see CLAUDE.md's "two sibling headers" note -- this project folds both
 * into one file rather than splitting Globals.h/AppState.h):
 *  1. Hardware singletons (mux, pots[], encoder, oled, leds[]) -- the
 *     driver-object instances every task reads/writes.
 *  2. App-layer state (tracks[], trackCount, cursorIndex, menu, appMode,
 *     Colors[]) and the App class that owns track-list mutation.
 *
 * Everything below is declared `extern`; definitions live in App.cpp. Any
 * .cpp using one of these globals must #include this header directly
 * rather than relying on another header to pull it in transitively -- and
 * conversely, none of the lower-level headers included below (mux.h,
 * quadrature.h, Encoder.h, track.h, Menu.h, ...) may ever include this
 * header back, or you get the #pragma once include-cycle described in
 * PROJECT.md/CLAUDE.md.
 */
#pragma once


#include "./driver/mux.h"
#include "./driver/quadrature.h"
#include "./driver/Encoder.h"
#include "./driver/usbMidi.h"
#include "./driver/uartMidi.h"

#include "./Components/Tasks.h"
#include "./Components/Menu.h"
#include "./Components/menus.h"
#include "./Components/track.h"

#include "FastLED.h"
#include "U8g2lib.h"
#include "boardConfig.h"

// ---------------------------------------------------------------------
// Hardware singletons -- one instance of each, defined in App.cpp.
// ---------------------------------------------------------------------

extern Multiplexer mux;                            ///< Dual 3-bit analog mux (boardConfig.h pins), polled by muxPollTask.
extern Quadrature * pots[];                         ///< One decoder per physical pot; constructed in main.cpp::setup().
extern Encoder encoder;                             ///< Menu-navigation rotary encoder + button.

/// Blocking frame-by-frame boot animation (GraphicsAssets.h's loading7[]) on
/// the OLED. Must be called after oled.begin()/setFont() (main.cpp::setup())
/// -- calling it any earlier draws to a not-yet-initialized display.
void playStartupAnim();

extern MenuManager menu;                                   ///< Menu tree navigator, rooted at menuView (see menus.h/menus.cpp).
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled;    ///< SSD1306 OLED driver (U8g2, full-buffer mode).

/// WS2812 strip, one entry per pot (see boardConfig.h's mappingLED for the
/// pot-index -> strip-index mapping). Configured by initLeds(); per-frame
/// color output happens in App::render() (renderTask), not here.
extern CRGB leds[LED_NBR];
/// Configures FastLED for the WS2812 strip on LEDRGB_PIN. Called once from setup().
void initLeds();

/// Sets index's color and marks the strip dirty; no-op if unchanged or
/// out-of-range (so re-asserting the same color every frame is free).
void setLed(CRGB color, uint8_t index);

/// Sets colorsLength consecutive LED slots from startIndex, in
/// *paired-per-track* slot space (slot 2n/2n+1 = track slot n's
/// upper/lower LED, mapped via mappingLED[]) rather than physical strip
/// order. Wraps modulo 2*min(trackCount, VISIBLE_SLOTS), not LED_NBR.
/// No-op while trackCount == 0.
void setLeds(const CRGB *colors, uint8_t colorsLength, uint8_t startIndex);

/// Pushes leds[] to the physical strip via FastLED.show() only if setLed()/
/// setLeds() actually changed something since the last call -- avoids
/// re-sending an unchanged frame over the WS2812 data line every tick.
void showLedsIfDirty();

// ---------------------------------------------------------------------
// App-layer state -- the flat track list, cursor, menu mode, and the
// palette new tracks draw their LED colors from.
// ---------------------------------------------------------------------

/// 16-entry color palette App::createTrack() draws a new track's knob1/knob2
/// start/end colors from by position; also used by menus.cpp's per-knob
/// Start/End color leaves to let the user re-pick a track's colors from the
/// same palette.
extern CRGB Colors[16];

// Flat, contiguous, positional track list: tracks[0..trackCount-1] are the
// populated tracks (no gaps, no fixed IDs -- a track's identity is its
// current index); tracks[trackCount..TOTAL_TRACKS-1] are unused. Starts
// empty (trackCount == 0); populated only via App::createTrack().
extern Track *tracks[TOTAL_TRACKS];
extern uint8_t trackCount;
extern int16_t cursorIndex; // current menu/live selection, shared by App::update()/render()

/**
 * @brief Subset of tracks[0..trackCount-1] whose Track::isTickable() is true
 * (LFO/StepSeq/MotionSeq -- anything with real clock-driven tick()
 * behavior; DUO's tick() is the base no-op). trackClockTask iterates this
 * instead of the full track list so it isn't calling a no-op on every DUO
 * track each cycle. Not positionally aligned with tracks[] -- just a
 * compact list of the pointers that matter, in tracks[] order.
 */
extern Track *tickableTracks[TOTAL_TRACKS];
extern uint8_t tickableTrackCount;

/**
 * @brief Rebuilds tickableTracks[]/tickableTrackCount from the current
 * tracks[0..trackCount-1] by filtering on Track::isTickable(). Called by
 * App::createTrack()/deleteTrack()/rebuildTrackType() after they touch
 * tracks[] -- and by persistence.cpp after a preset load replaces the whole
 * list -- so trackClockTask never reads a stale entry. O(trackCount); fine
 * since it only runs on track-list mutation, not every clock cycle.
 */
void rebuildTickableTracks();

/**
 * @brief Slides the VISIBLE_SLOTS on-screen/on-LED window (App.cpp's file-
 * local windowStart) to keep cursorIndex inside it, and re-clamps it to the
 * populated range. In ViewMode::NORMAL this is a smooth 1-at-a-time scroll
 * (the window slides only by the cursor's overshoot); in ViewMode::PAGED
 * the window jumps a full VISIBLE_SLOTS page at a time (see viewMode
 * below). App::update() calls this itself after every scroll; call it
 * directly after any out-of-band cursorIndex/trackCount change that doesn't
 * go through App::update() (e.g. a tree-defined insert-confirm/delete-track
 * mutation), or the window can be left pointing past the (now shrunk) track
 * list until the next encoder rotation.
 */
void syncScrollWindow();

/**
 * @brief Moves cursorIndex by delta, clamped to [0, trackCount-1] (no-op if
 * trackCount == 0) -- the same clamp App::update() applies to LIVE-mode
 * scrolling, factored out so a tree-defined "browse tracks" node's action
 * can drive the same cursor from inside the menu. Does not call
 * syncScrollWindow() itself -- callers that need the window to re-follow
 * immediately (rather than waiting for the next render()) call that
 * separately.
 */
void moveTrackCursor(int8_t delta);

/**
 * @brief Draws the same VISIBLE_SLOTS column/bar/highlight view as the LIVE
 * screen (drawTrackView()) but reusable from a tree-defined "browse tracks"
 * node's MenuNode::display -- lets the user preview tracks with the
 * encoder without leaving the menu first. Does not clearBuffer()/
 * sendBuffer() -- App::render() already brackets menu drawing with those for
 * all of MENU mode.
 */
void drawTrackSelector();

/// Whether the encoder drives track scrolling or menu navigation.
/// LIVE: scroll tracks; short press -> MENU; long press -> QUICK_CONFIG.
/// MENU: navigate the menu strand; press runs the current node's action.
/// INSERT: move an insertion line (insertIndex) between tracks; confirm/
///   cancel behavior depends on insertOrigin (see below).
/// DELETE_SELECT: pick a track to delete via confirmDelete()/cancelDelete().
/// QUICK_CONFIG: per-track field editor (placeholder) for cursorIndex's track.
/// See App::processEncoder() for exact button/mode transition rules.
enum class AppMode : uint8_t
{
    LIVE,
    MENU,
    INSERT,
    DELETE_SELECT,
    QUICK_CONFIG,
};

/// Where an AppMode::INSERT session started, so confirm/cancel knows
/// where to return. Read by App::confirmInsert()/cancelInsert().
enum class InsertOrigin : uint8_t
{
    LIVE_DEBUG,
    MENU_NEW_TRACK,
};
extern InsertOrigin insertOrigin;

// ControlType App::confirmInsert() creates the new track with, while
// insertOrigin == MENU_NEW_TRACK. Set by menus.cpp's "New Track" node
// before switching appMode to INSERT (see its Type: sibling), read only by
// confirmInsert() -- processEncoder() itself doesn't need to know it.
extern ControlType pendingInsertType;

/**
 * @brief How the VISIBLE_SLOTS on-screen/on-LED window follows cursorIndex
 * (see syncScrollWindow()). NORMAL is today's smooth 1-at-a-time scroll;
 * PAGED jumps a full page at a time. Selected via the root "View" menu
 * (menus.cpp's menuViewMode); defaults to NORMAL.
 */
enum class ViewMode : uint8_t
{
    NORMAL,
    PAGED,
};
extern ViewMode viewMode;

// Insertion-line cursor position while appMode == INSERT: an index into
// the trackCount+1 boundary slots around the visible window (0 = before
// tracks[windowStart], trackCount = after the last track) -- see
// AppMode::INSERT's doc comment and drawInsertView() (App.cpp).
extern int16_t insertIndex;
extern volatile AppMode appMode;

// ---------------------------------------------------------------------
// App class -- ties the above together each tick.
// ---------------------------------------------------------------------

/**
 * @brief Top-level application state, split across two update rates (see
 * Tasks.h for the full task list):
 *  - update():  menu navigation, LIVE<->MENU mode switching, and the
 *    encoder-button dispatch that decides between them. Not latency-
 *    sensitive -- driven from appTask at APP_TASK_PERIOD_MS (~50ms/20Hz),
 *    fast enough for responsive menu navigation without competing with the
 *    render/clock tasks for CPU.
 *  - render():  pot-driven Track::update() for the VISIBLE_SLOTS on-screen
 *    tracks, plus the OLED/LED-strip draw. Driven from renderTask at 60Hz so
 *    a knob move is reflected on screen/LEDs with minimal latency; combined
 *    into one method (rather than split further) so the pot update and the
 *    frame that shows its result happen in the same tick -- see
 *    renderTask()'s doc comment in Tasks.h.
 * Also owns track list mutation (createTrack()/deleteTrack()) since it's
 * the class most responsible for tracks[]/trackCount. Single instance
 * (`app`).
 */
class App
{
public:
    App() = default;

    /**
     * @brief Menu/mode logic: encoder-button dispatch (LIVE<->MENU, menu
     * enter/back), menu navigation while in MENU. Does not touch pots[] or
     * draw anything -- see render() for that. Called from appTask.
     */
    void update();

    /**
     * @brief Pot-driven track update + OLED/LED render for whichever
     * VISIBLE_SLOTS tracks are currently on screen (LIVE) or the open menu
     * (MENU). Called from renderTask, ~60Hz.
     */
    void render();

    /**
     * @brief process encoder press and longpress to switch appMode 
     * 
     */
    void processEncoder(int8_t delta);

    void changeMode(AppMode mode);

    /// @todo dbMode is never set anywhere yet -- this always returns false. Intended to gate a debug overlay, read by render().
    bool isDebugMode() const;

    /**
     * @brief Confirms an AppMode::INSERT session started with insertOrigin
     * == MENU_NEW_TRACK: creates a track of type at insertIndex (see
     * createTrack()) and returns to the Track menu strand. No-op (mode
     * unchanged) if insertOrigin is LIVE_DEBUG -- that path has no confirm
     * behavior, see AppMode's doc comment.
     */
    void confirmInsert(ControlType type);

    /**
     * @brief Cancels an AppMode::INSERT session started with insertOrigin
     * == MENU_NEW_TRACK: returns to the Track menu strand without creating
     * anything. No-op (mode unchanged) if insertOrigin is LIVE_DEBUG.
     */
    void cancelInsert();

    /**
     * @brief Confirms an AppMode::DELETE_SELECT session: deletes
     * tracks[cursorIndex] (see deleteTrack()) and returns to the Track menu
     * strand. No-op if trackCount == 0.
     */
    void confirmDelete();

    /**
     * @brief Cancels an AppMode::DELETE_SELECT session: returns to the
     * Track menu strand without deleting anything.
     */
    void cancelDelete();

    /**
     * @brief Creates a new track of the given type (see makeTrack(),
     * track.h) and inserts it at position, shifting
     * tracks[position..trackCount-1] one slot to the right. No-op if the
     * list is already at TOTAL_TRACKS capacity or position > trackCount.
     * @param position Insertion index; pass trackCount to append at the end.
     * @param type     ControlType of the new track; defaults to DUAL.
     */
    void createTrack(uint8_t position, ControlType type = ControlType::DUAL);

    /**
     * @brief Deletes the track at position, shifting
     * tracks[position+1..trackCount-1] one slot to the left. No-op if
     * position is out of range.
     */
    void deleteTrack(uint8_t position);

    /**
     * @brief Swaps tracks[position]'s concrete subclass to match newType (a
     * track's behavior is fixed by its C++ class, not a stored field --
     * see makeTrack()). Preserves value1/value2/trackColor; drops the old
     * subclass's own param struct (TODO in App.cpp -- nothing to copy
     * across since each subclass's struct is disjoint). No-op if position
     * is out of range or oldType/newType map to the same class.
     */
    void rebuildTrackType(uint8_t position, ControlType oldType, ControlType newType);

    /**
     * @brief Adds delta * step to *value -- meant to be called from a
     * MenuNode::EncoderFunc (see Menu.h) while a menu node is latched, so
     * turning the encoder adjusts an arbitrary app-layer int16_t (e.g. a
     * selected preset slot) instead of navigating menu siblings. No
     * clamping -- callers whose value has a valid range are expected to
     * clamp it themselves (see std::clamp usage elsewhere in App.cpp, e.g.
     * moveTrackCursor()).
     */
    static void adjustValue(int16_t &value, int8_t delta, int16_t step = 1);

private:
    bool dbMode = false; // toggled by encoder button; render() reads it
};

extern App app;
