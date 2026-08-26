/**
 * @file menus.h
 * @brief Declares the concrete menu tree nodes (built on top of Menu.h's
 * primitives) and buildMenuTree(), which wires them together and is called
 * once from main.cpp::setup() before any task starts navigating the menu
 * (App.h's MenuManager menu, rooted at menuView).
 *
 * Like Menu.h, this header must not include App.h/AppState.h -- see
 * CLAUDE.md's back-include rule. menus.cpp includes App.h itself since
 * buildMenuTree() needs AppMode.
 */
#pragma once
#include "./Components/Menu.h"

extern MenuNode menuView;
extern MenuNode menuTrack;
extern MenuNode menuConfig;
extern MenuNode menuPresets;
extern MenuNode menuExit;

/// Chains menuView -> menuTrack -> menuConfig -> menuPresets -> menuExit as
/// the top-level siblings, links each one's child strand, sets every child's
/// .parent (so menuBack can walk up) and binds all actions/encoders. Call
/// once before appTask starts (main.cpp::setup()).
void buildMenuTree();
