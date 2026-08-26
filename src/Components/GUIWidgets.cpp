/**
 * @file GUIWidgets.cpp
 * @brief Concrete per-ControlType QCFieldList builders (against DualParams/
 * LfoParams/StepSeqParams/MotionSeqParams -- see track.h) live here once
 * they're built.
 *
 * buildDualFieldList() is the first one: DUAL's 7-field layout (list-select,
 * assignment-select, cc, channel, start, end, tf), one field per physical
 * pot, against DualParams. There is no separate "unassign" field -- turning
 * CC down past 0 removes the selected CCAssignment instead (applyCc()/
 * unassignSelected()). LFO/StepSeq/MotionSeq builders are still TODO.
 */
#include "GUIWidgets.h"
#include "track.h"
#include "U8g2lib.h"
#include <cstdio>
#include <algorithm>

namespace
{
    // QC field slot size, in pixels -- must match App.cpp's kQcSlotW/H
    // (drawQuickConfigView()); kept in sync by convention, not a shared
    // constant.
    constexpr int16_t QC_SLOT_W = 32;
    constexpr int16_t QC_SLOT_H = 26;

    // Selects list1/list2 by DualEditState::listIndex.
    CCAssignmentList &selectedList(DualParams &params, DualEditState &edit)
    {
        return edit.listIndex == 0 ? params.assignmentList1 : params.assignmentList2;
    }

    // Clamps edit.assignmentIndex into the selected list's valid range
    // (0..length, length itself being the "unassigned" slot -- see
    // DualEditState's doc comment) after either list/assignment field
    // changes the list or the length.
    void clampAssignmentIndex(DualParams &params, DualEditState &edit)
    {
        CCAssignmentList &list = selectedList(params, edit);
        uint8_t maxIndex = std::min<uint8_t>(list.length, TRACK_MAX_CCS - 1);
        edit.assignmentIndex = std::min(edit.assignmentIndex, maxIndex);
    }

    // True while assignmentIndex points at an existing CCAssignment rather
    // than the trailing "unassigned" slot.
    bool hasSelectedAssignment(DualParams &params, DualEditState &edit)
    {
        return edit.assignmentIndex < selectedList(params, edit).length;
    }

    // Creates the assignment (bumping list.length) if assignmentIndex
    // currently points at the unassigned slot.
    CCAssignment &getOrCreateSelectedAssignment(DualParams &params, DualEditState &edit)
    {
        CCAssignmentList &list = selectedList(params, edit);
        if (edit.assignmentIndex >= list.length)
            list.length = edit.assignmentIndex + 1;
        return list.ccs[edit.assignmentIndex];
    }

    const char *transferFunctionName(TransferFunction tf)
    {
        switch (tf)
        {
        case TransferFunction::LINEAR:
            return "LIN";
        case TransferFunction::BELL:
            return "BELL";
        case TransferFunction::EXP:
            return "EXP";
        case TransferFunction::LOG:
            return "LOG";
        default:
            return "?";
        }
    }

    // Shared minimal-text render: label on the first line, value on the
    // second, selection shown via drawFrame -- same "frame the selected
    // slot" convention as App.cpp's drawTracks()/drawInsertView() column
    // highlight. displayCtx is always &oled (App.cpp's QUICK_CONFIG render
    // loop, once built); GUIWidgets.h/.cpp have no display-library
    // dependency of their own, so the cast back happens here, same split as
    // menus.cpp's defaultMenuRowDisplay().
    void drawLabelAndText(void *displayCtx, int16_t slotX, int16_t slotY, bool selected,
                           const char *label, const char *value)
    {
        auto *display = static_cast<U8G2_SSD1306_128X64_NONAME_F_HW_I2C *>(displayCtx);
        display->setCursor(slotX + 1, slotY + 10);
        display->print(label);
        display->setCursor(slotX + 1, slotY + 22);
        display->print(value);
        if (selected)
            display->drawFrame(slotX, slotY, QC_SLOT_W - 1, QC_SLOT_H - 1);
    }

    void drawLabelAndInt(void *displayCtx, int16_t slotX, int16_t slotY, bool selected,
                          const char *label, int value)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", value);
        drawLabelAndText(displayCtx, slotX, slotY, selected, label, buf);
    }

    // Bundles the two pieces every field past list-select needs: params
    // (for the selected list's length) and edit (the list-select/
    // assignment-select cursor). Every DUAL QC field below uses this as its
    // ctx instead of a bare DualEditState*, since even list-select must
    // reclamp assignmentIndex against the newly-selected list's length.
    struct AssignSelectCtx
    {
        DualParams *params;
        DualEditState *edit;
    };

    // --- list-select: DualEditState::listIndex (0/1 -> knob1/knob2) -------
    void applyListSelect(void *ctx, int8_t delta)
    {
        auto *c = static_cast<AssignSelectCtx *>(ctx);
        c->edit->listIndex = (delta > 0) ? 1 : (delta < 0) ? 0 : c->edit->listIndex;
        clampAssignmentIndex(*c->params, *c->edit);
    }

    void renderListSelect(QCField &field, void *displayCtx, int16_t slotX, int16_t slotY, bool selected)
    {
        auto *c = static_cast<AssignSelectCtx *>(field.ctx);
        drawLabelAndText(displayCtx, slotX, slotY, selected, field.name, c->edit->listIndex == 0 ? "K1" : "K2");
    }

    // --- assignment-select: DualEditState::assignmentIndex ----------------
    void applyAssignSelect(void *ctx, int8_t delta)
    {
        auto *c = static_cast<AssignSelectCtx *>(ctx);
        CCAssignmentList &list = selectedList(*c->params, *c->edit);
        uint8_t maxIndex = std::min<uint8_t>(list.length, TRACK_MAX_CCS - 1);
        int next = (int)c->edit->assignmentIndex + (delta > 0 ? 1 : delta < 0 ? -1 : 0);
        c->edit->assignmentIndex = (uint8_t)std::clamp(next, 0, (int)maxIndex);
    }

    void renderAssignSelect(QCField &field, void *displayCtx, int16_t slotX, int16_t slotY, bool selected)
    {
        auto *c = static_cast<AssignSelectCtx *>(field.ctx);
        if (hasSelectedAssignment(*c->params, *c->edit))
            drawLabelAndInt(displayCtx, slotX, slotY, selected, field.name, c->edit->assignmentIndex);
        else
            drawLabelAndText(displayCtx, slotX, slotY, selected, field.name, "NEW");
    }

    // Clears the selected CCAssignment slot, shifting every later slot in
    // the list down by one and shrinking length -- same removal formerly
    // done by a dedicated UNASSIGN field, now folded into applyCc() below
    // (turning CC past 0 removes the assignment instead of clamping at 0).
    void unassignSelected(AssignSelectCtx *c)
    {
        if (!hasSelectedAssignment(*c->params, *c->edit))
            return;
        CCAssignmentList &list = selectedList(*c->params, *c->edit);
        for (uint8_t i = c->edit->assignmentIndex; i + 1 < list.length; i++)
            list.ccs[i] = list.ccs[i + 1];
        list.length--;
        clampAssignmentIndex(*c->params, *c->edit);
    }

    // --- cc/channel/start/end/tf: operate on the selected CCAssignment,
    // creating it on first edit if it was still the unassigned slot -------
    // cc has no lower clamp at 0 like channel/start/end -- decrementing
    // past 0 removes the assignment entirely (unassignSelected()) instead
    // of sitting at 0, per the user's spec: "past 0 it remove the
    // assignment, and removes it from the list".
    void applyCc(void *ctx, int8_t delta)
    {
        auto *c = static_cast<AssignSelectCtx *>(ctx);
        CCAssignment &a = getOrCreateSelectedAssignment(*c->params, *c->edit);
        int next = (int)a.cc + delta;
        if (next < 0)
        {
            unassignSelected(c);
            return;
        }
        a.cc = (uint8_t)std::min(next, 127);
    }

    void renderCc(QCField &field, void *displayCtx, int16_t slotX, int16_t slotY, bool selected)
    {
        auto *c = static_cast<AssignSelectCtx *>(field.ctx);
        if (!hasSelectedAssignment(*c->params, *c->edit))
        {
            drawLabelAndText(displayCtx, slotX, slotY, selected, field.name, "--");
            return;
        }
        CCAssignment &a = getOrCreateSelectedAssignment(*c->params, *c->edit);
        drawLabelAndInt(displayCtx, slotX, slotY, selected, field.name, a.cc);
    }

    void applyChannel(void *ctx, int8_t delta)
    {
        auto *c = static_cast<AssignSelectCtx *>(ctx);
        CCAssignment &a = getOrCreateSelectedAssignment(*c->params, *c->edit);
        a.channel = (uint8_t)std::clamp((int)a.channel + delta, 0, 15);
    }

    void applyStart(void *ctx, int8_t delta)
    {
        auto *c = static_cast<AssignSelectCtx *>(ctx);
        CCAssignment &a = getOrCreateSelectedAssignment(*c->params, *c->edit);
        a.start = (uint8_t)std::clamp((int)a.start + delta, 0, 127);
    }

    void applyEnd(void *ctx, int8_t delta)
    {
        auto *c = static_cast<AssignSelectCtx *>(ctx);
        CCAssignment &a = getOrCreateSelectedAssignment(*c->params, *c->edit);
        a.end = (uint8_t)std::clamp((int)a.end + delta, 0, 127);
    }

    void applyTf(void *ctx, int8_t delta)
    {
        auto *c = static_cast<AssignSelectCtx *>(ctx);
        CCAssignment &a = getOrCreateSelectedAssignment(*c->params, *c->edit);
        int next = (int)a.tf + (delta > 0 ? 1 : delta < 0 ? -1 : 0);
        constexpr int kTfCount = (int)TransferFunction::LOG + 1;
        a.tf = (TransferFunction)((next % kTfCount + kTfCount) % kTfCount);
    }

    void renderChannel(QCField &field, void *displayCtx, int16_t slotX, int16_t slotY, bool selected)
    {
        auto *c = static_cast<AssignSelectCtx *>(field.ctx);
        if (!hasSelectedAssignment(*c->params, *c->edit))
        {
            drawLabelAndText(displayCtx, slotX, slotY, selected, field.name, "--");
            return;
        }
        CCAssignment &a = getOrCreateSelectedAssignment(*c->params, *c->edit);
        drawLabelAndInt(displayCtx, slotX, slotY, selected, field.name, a.channel + 1);
    }

    void renderStart(QCField &field, void *displayCtx, int16_t slotX, int16_t slotY, bool selected)
    {
        auto *c = static_cast<AssignSelectCtx *>(field.ctx);
        if (!hasSelectedAssignment(*c->params, *c->edit))
        {
            drawLabelAndText(displayCtx, slotX, slotY, selected, field.name, "--");
            return;
        }
        CCAssignment &a = getOrCreateSelectedAssignment(*c->params, *c->edit);
        drawLabelAndInt(displayCtx, slotX, slotY, selected, field.name, a.start);
    }

    void renderEnd(QCField &field, void *displayCtx, int16_t slotX, int16_t slotY, bool selected)
    {
        auto *c = static_cast<AssignSelectCtx *>(field.ctx);
        if (!hasSelectedAssignment(*c->params, *c->edit))
        {
            drawLabelAndText(displayCtx, slotX, slotY, selected, field.name, "--");
            return;
        }
        CCAssignment &a = getOrCreateSelectedAssignment(*c->params, *c->edit);
        drawLabelAndInt(displayCtx, slotX, slotY, selected, field.name, a.end);
    }

    void renderTf(QCField &field, void *displayCtx, int16_t slotX, int16_t slotY, bool selected)
    {
        auto *c = static_cast<AssignSelectCtx *>(field.ctx);
        if (!hasSelectedAssignment(*c->params, *c->edit))
        {
            drawLabelAndText(displayCtx, slotX, slotY, selected, field.name, "--");
            return;
        }
        CCAssignment &a = getOrCreateSelectedAssignment(*c->params, *c->edit);
        drawLabelAndText(displayCtx, slotX, slotY, selected, field.name, transferFunctionName(a.tf));
    }
}

/// Builds DUAL's 7-field QC page against params, using edit as the shared
/// list-select/assignment-select cursor. No separate "unassigned" field --
/// turning CC down past 0 removes the selected CCAssignment instead. Both
/// params and edit must outlive out's use.
void buildDualFieldList(QCFieldList &out, DualParams &params, DualEditState &edit)
{
    out.clear();

    // AssignSelectCtx instances backing the cc/channel/start/end/tf fields
    // below. Static: QCField::ctx must stay valid for as long as the built
    // list is in use, same lifetime requirement as params/edit themselves,
    // and every call for a given (params, edit) pair resolves to the same
    // underlying data regardless of which static instance is live -- only
    // one DUAL track's QC page is ever on-screen at once (see App.cpp's
    // AppMode::QUICK_CONFIG), so a single shared instance rebound per build
    // is sufficient.
    static AssignSelectCtx assignCtx;
    assignCtx.params = &params;
    assignCtx.edit = &edit;

    out.add(QCField("L", &assignCtx, applyListSelect, renderListSelect));
    out.add(QCField("S", &assignCtx, applyAssignSelect, renderAssignSelect));
    out.add(QCField("CC", &assignCtx, applyCc, renderCc));
    out.add(QCField("CH", &assignCtx, applyChannel, renderChannel));
    out.add(QCField("STRT", &assignCtx, applyStart, renderStart));
    out.add(QCField("END", &assignCtx, applyEnd, renderEnd));
    out.add(QCField("TF", &assignCtx, applyTf, renderTf));
}
