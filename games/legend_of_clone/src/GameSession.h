#pragma once

#include "game/DialogController.h"
#include "game/GameState.h"

/**
 * @file GameSession.h
 * @brief The state that outlives a scene change, and the controller that
 *        reads and writes it.
 *
 * The engine reruns init() on every scene swap, so the flags, the rupees and
 * the dialog in progress cannot belong to either scene. Both are defined once
 * in GameSession.cpp, beside the scenes rather than inside them. Nothing here
 * is saved yet.
 */
namespace legend_of_clone {

extern GameState gameState;
extern DialogController dialogController;

} // namespace legend_of_clone
