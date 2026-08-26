#include "Menu.h"

size_t MenuManager::collectStrand(MenuNode *head, MenuNode *out[MAX_SIBLINGS])
{
    size_t count = 0;
    MenuNode *node = head;
    while (node && count < MAX_SIBLINGS)
    {
        out[count++] = node;
        node = node->sibling;
    }
    return count;
}

MenuManager::MenuManager(MenuNode *strandHead, int index)
    : current(nullptr), siblingCount(0), index(0)
{
    siblingCount = collectStrand(strandHead, siblings);
    if (siblingCount > 0)
    {
        this->index = (index >= 0 && (size_t)index < siblingCount) ? index : 0;
        current = siblings[this->index];
    }
}

void MenuManager::next()
{
    if (index + 1 < (int)siblingCount)
    {
        ++index;
        current = siblings[index];
    }
}

void MenuManager::back()
{
    if (index > 0)
    {
        --index;
        current = siblings[index];
    }
}

void MenuManager::enter(MenuNode *strandHead, int newIndex)
{
    siblingCount = collectStrand(strandHead, siblings);
    if (siblingCount == 0)
    {
        current = nullptr;
        index = 0;
        return;
    }
    index = (newIndex >= 0 && (size_t)newIndex < siblingCount) ? newIndex : 0;
    current = siblings[index];
}

void MenuManager::latch(MenuNode *node)
{
    if (node && node->onEncoder)
        editingNode = node;
}

void MenuManager::unlatch()
{
    editingNode = nullptr;
}

void MenuManager::update(int direction)
{
    if (editingNode)
    {
        if (direction != 0)
            editingNode->onEncoder(*this, editingNode->actionCtx, (int8_t)direction);
        else
            unlatch();
        return;
    }

    if (direction > 0)
        next();
    else if (direction < 0)
        back();
    else if (current)
        current->trigger(*this);
}

void MenuManager::render(void *displayCtx, MenuNode::DisplayFunc fallback)
{
    for (size_t i = 0; i < siblingCount; ++i)
    {
        MenuNode *node = siblings[i];
        bool selected = (node == current);
        int16_t absoluteIndex = (int16_t)i;

        if (node->display)
            node->display(*this, *node, displayCtx, 0, absoluteIndex, selected);
        else if (fallback)
            fallback(*this, *node, displayCtx, 0, absoluteIndex, selected);
    }
}
