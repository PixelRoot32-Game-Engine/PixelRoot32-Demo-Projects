// The engine is installed for host_test but not built (see platformio.ini),
// so the engine translation units src/game/ links against are compiled here,
// straight from the installed checkout. Renderer.cpp is deliberately absent:
// nothing under test draws.
#include "gameplay/DialogRunner.cpp"
#include "graphics/DialogBox.cpp"
#include "graphics/Font5x7.cpp"
#include "graphics/FontManager.cpp"
#include "graphics/TextLayout.cpp"
