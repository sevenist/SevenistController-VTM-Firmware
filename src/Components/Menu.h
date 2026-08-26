#pragma once

#include <cstdint>
#include <cstddef>

enum class MenuVarType
{
    INT_4B,
    INT_7B,
    ICON_IDX,
    TEXT, // row's value is MenuNode::textValue, not actionCtx
    //! TODO: will need mode of theeses as i create menu user interface
};

class MenuManager;

/// Max sibling entries collected per strand -- fixed so MenuManager never
/// allocates. Bump if a menu strand ever needs more entries.
static constexpr size_t MAX_SIBLINGS = 16;

class MenuNode
{
public:
    using ActionFunc = void (*)(MenuManager &, void *ctx);
    // Called with +1/-1 per encoder detent while this node is latched (see
    // MenuManager::latch()/unlatch()) -- rotation is routed here instead of
    // next()/back() for as long as the latch holds. ctx is the same
    // actionCtx the node was constructed with.
    using EncoderFunc = void (*)(MenuManager &, void *ctx, int8_t delta);
    // Renders one row for `node`. displayCtx is opaque (cast back by the
    // concrete display function, e.g. App.cpp) -- Menu.h has no display-lib
    // dependency. y is node's absolute sibling index, NOT a pixel row --
    // windowing/culling is entirely the display function's job (see
    // App.cpp's menuRowY()).
    using DisplayFunc = void (*)(MenuManager &, MenuNode &node, void *displayCtx, int16_t x, int16_t y, bool selected);

    const char *name;
    int8_t iconIndex;
    ActionFunc action;
    MenuVarType varType;
    void *actionCtx;

    // Value shown for MenuVarType::TEXT rows, in the same right-hand slot the
    // other varTypes use for a number/icon. Points at a string literal with
    // static lifetime (see menus.cpp's controlTypeName()), so the row renderer
    // never owns or copies it.
    const char *textValue = nullptr;
    EncoderFunc onEncoder; // optional; see EncoderFunc -- only meaningful while this node is latched (MenuManager::latch())
    DisplayFunc display;   // optional; overrides the default per-row rendering passed to MenuManager::render()

    // Node whose action enter()s the strand this one belongs to, or nullptr
    // for top-level nodes. Assigned in buildMenuTree() rather than the ctor,
    // since strands are wired after every node is constructed. Read as
    // siblings[0]->parent (not current->parent) so one shared Back node
    // works in any strand -- see menus.cpp's backAction().
    MenuNode *parent = nullptr;

    MenuNode *sibling;

    explicit MenuNode(
        const char *name,
        int8_t iconIndex = -1, /// -1 = no icon used
        ActionFunc action = nullptr,
        MenuVarType varType = MenuVarType::INT_4B,
        void *actionCtx = nullptr,
        MenuNode *sibling = nullptr,
        DisplayFunc display = nullptr,
        EncoderFunc onEncoder = nullptr)
        : name(name),
          iconIndex(iconIndex),
          action(action),
          varType(varType),
          actionCtx(actionCtx),
          onEncoder(onEncoder),
          display(display),
          sibling(sibling)
    {
    }

    void trigger(MenuManager &manager)
    {
        if (action)
        {
            action(manager, actionCtx);
        }
    }
};

/// Chains nodes[0] -> nodes[1] -> ... -> nodes[N-1] via .sibling, in the
/// order given. Overwrites each node's existing .sibling -- call once per
/// strand while building the tree, not incrementally. Returns the head
/// (nodes[0]) so a strand can be linked and captured in one expression,
/// e.g. `MenuNode *strand = linkSiblings(&a, &b, &c);`.
template <typename... Nodes>
MenuNode *linkSiblings(MenuNode *first, Nodes... rest)
{
    MenuNode *chain[] = {first, rest...};
    for (size_t i = 0; i + 1 < sizeof...(rest) + 1; ++i)
        chain[i]->sibling = chain[i + 1];
    return first;
}

class MenuManager
{
public:
    explicit MenuManager(MenuNode *strandHead, int index = 0);

    void next();
    void back();
    void enter(MenuNode *strandHead, int index = 0);

    /// direction > 0 / < 0: rotates -- routed to the latched node's
    /// onEncoder if latched, else next()/back(). direction == 0: press --
    /// unlatches if latched, else runs current's action.
    void update(int direction);

    /// Latches node: subsequent update(+-1) calls route to node->onEncoder
    /// instead of next()/back(), until unlatch() (or another latch()).
    /// No-op if node has no onEncoder set.
    void latch(MenuNode *node);
    void unlatch();
    bool isEditing() const { return editingNode != nullptr; }

    /// Calls each sibling's own display() if set, else fallback, once per
    /// sibling (x=0, y=sibling's absolute index in siblings[]) -- no
    /// windowing/culling here, see MenuNode::DisplayFunc's doc comment. Does
    /// not clear/flush the display itself; the caller brackets that.
    void render(void *displayCtx, MenuNode::DisplayFunc fallback);

    MenuNode *current;
    MenuNode *siblings[MAX_SIBLINGS];
    size_t siblingCount;
    int index;

private:
    static size_t collectStrand(MenuNode *head, MenuNode *out[MAX_SIBLINGS]);

    MenuNode *editingNode = nullptr; // non-null while latched; see latch()/unlatch()
};
