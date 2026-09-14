#include "GameSession.h"

namespace legend_of_clone {

// Defined in this order on purpose: within one translation unit, globals are
// constructed top to bottom, and the controller binds a reference to the state.
GameState gameState{};
DialogController dialogController{gameState};

} // namespace legend_of_clone
