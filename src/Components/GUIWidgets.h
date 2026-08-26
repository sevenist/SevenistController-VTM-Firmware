/**
 * @file GUIWidgets.h
 * @brief Data model for AppMode::QUICK_CONFIG's graphical, per-ControlType
 * parameter grid (see PROJECT.md). A QCField describes one on-screen widget
 * bound to one physical pot: applyDelta() mutates the underlying track param
 * struct directly (no undo/staging), render() draws the widget's current
 * state. A QCFieldList is the fixed-capacity, ordered set of fields for one
 * track's page -- more fields than fit on screen at once scroll via the
 * window start index, one field per pot (see App.cpp's QUICK_CONFIG render
 * loop once that lands).
 *
 * Like Menu.h, this header must not include App.h/track.h -- see CLAUDE.md's
 * back-include rule. ctx is an opaque pointer (typically a Track subclass's
 * param struct member) so this file has no dependency on Track/TrackConfig;
 * the concrete per-ControlType field lists (built against DualParams/
 * LfoParams/etc.) belong in GUIWidgets.cpp, which can include track.h
 * itself.
 */
#pragma once

#include <cstdint>
#include <cstddef>

class QCField
{
public:
    using ApplyDeltaFunc = void (*)(void *ctx, int8_t delta);
    // Renders this field's current widget state at the given screen slot
    // (slotX/slotY are pixel coordinates of the slot's top-left corner, one
    // slot per visible field -- GUIWidgets.h has no concept of slot sizing or
    // layout, that's the display function's job, same split as
    // MenuNode::DisplayFunc in Menu.h). displayCtx is an opaque pointer to
    // whatever the caller passed to QCFieldList::render() (e.g. the real oled
    // instance).
    using RenderFunc = void (*)(QCField &field, void *displayCtx, int16_t slotX, int16_t slotY, bool selected);

    const char *name;
    void *ctx; // passed to applyDelta/render; typically &someParams.someField
    ApplyDeltaFunc applyDelta;
    RenderFunc render;

    explicit QCField(
        const char *name = nullptr,
        void *ctx = nullptr,
        ApplyDeltaFunc applyDelta = nullptr,
        RenderFunc render = nullptr)
        : name(name), ctx(ctx), applyDelta(applyDelta), render(render)
    {
    }
};

/// Max fields collected per track's QC page -- fixed so QCFieldList never
/// allocates, same rationale as Menu.h's MAX_SIBLINGS.
static constexpr size_t MAX_QC_FIELDS = 16;

/// Fixed-capacity, ordered list of QCFields for one track's QC page. Encoder
/// rotation (once wired in App.cpp) shifts windowStart to scroll which
/// fields are bound to the 8 physical pots; this class has no opinion on how
/// many pots exist or how a window is drawn (see App.cpp's own
/// VISIBLE_SLOTS-style windowing for that, once QUICK_CONFIG's render loop
/// is built).
class QCFieldList
{
public:
    QCField fields[MAX_QC_FIELDS];
    size_t fieldCount = 0;
    size_t windowStart = 0;

    /// Absolute index into fields[] of the field render() should draw as
    /// selected -- e.g. an encoder-driven focus cursor layered on top of
    /// the pot-per-field binding, distinct from windowStart (which field is
    /// scrolled on-screen, not which one is focused). Left at 0 by
    /// clear()/add(); the caller (App.cpp, once QUICK_CONFIG's render loop
    /// is built) is responsible for moving it and keeping it within
    /// [0, fieldCount).
    size_t selectedIndex = 0;

    void clear()
    {
        fieldCount = 0;
        windowStart = 0;
        selectedIndex = 0;
    }

    /// Appends field to the list. No-op if already at MAX_QC_FIELDS.
    void add(const QCField &field)
    {
        if (fieldCount >= MAX_QC_FIELDS)
            return;
        fields[fieldCount++] = field;
    }

    /// Renders every field from windowStart up to the first visibleCount
    /// slots (or fewer, once fieldCount is exhausted), one slot per field
    /// left-to-right starting at (originX, originY) and advancing by slotW
    /// -- callers own their own slot sizing (GUIWidgets.h has no opinion on
    /// pixel layout, see QCField::RenderFunc's doc comment), so slotW is
    /// passed in rather than hardcoded here. Skips a field entirely if it
    /// has no render() set (e.g. reserved/unused slot) rather than drawing
    /// nothing at its position -- same "just don't draw it" behavior as an
    /// idle track column in App.cpp's drawTracks(). Each field's selected
    /// flag is true only for the one field at selectedIndex, so conditional
    /// draw logic (framing, inverted text, etc.) lives entirely in the
    /// field's own RenderFunc rather than here.
    void render(void *displayCtx, int16_t originX, int16_t originY, int16_t slotW, size_t visibleCount) const
    {
        for (size_t i = 0; i < visibleCount; i++)
        {
            size_t idx = windowStart + i;
            if (idx >= fieldCount)
                break;

            const QCField &field = fields[idx];
            if (!field.render)
                continue;

            int16_t slotX = (int16_t)(originX + (int16_t)i * slotW);
            field.render(const_cast<QCField &>(field), displayCtx, slotX, originY, idx == selectedIndex);
        }
    }
};

/// Cursor state for DUAL's QC page (GUIWidgets.cpp's buildDualFieldList()):
/// which of DualParams's two CCAssignmentLists, and which CCAssignment slot
/// within it, the cc/channel/start/end/tf fields currently read and write.
/// Lives outside DualParams itself (it's page-navigation state, not track
/// config) -- the caller (App.cpp, once QUICK_CONFIG's render loop is
/// built) owns one instance per track and passes it into
/// buildDualFieldList() alongside that track's DualParams, keeping it alive
/// for as long as the built QCFieldList's field ctx pointers are in use.
struct DualEditState
{
    /// 0 = assignmentList1 (knob1), 1 = assignmentList2 (knob2).
    uint8_t listIndex = 0;

    /// Index into the selected list's ccs[]. Valid range is
    /// 0..list.length inclusive: 0..length-1 selects an existing
    /// CCAssignment to edit, and == length (capped at TRACK_MAX_CCS-1)
    /// selects the "unassigned" slot -- see track.h's CCAssignmentList.
    uint8_t assignmentIndex = 0;
};
