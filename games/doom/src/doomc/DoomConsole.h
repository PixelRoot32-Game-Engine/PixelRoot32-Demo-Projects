#pragma once

/**
 * @brief The port's console: the one symbol every serial line goes through.
 *
 * `writeLine()` is the C++ side of the port's output path. Inside it calls
 * `pixelroot32::core::logging::platformPrint()`, the engine's cross-platform
 * primitive -- `Serial.print()` on the board, `printf()` on the simulator --
 * which is always compiled in, unlike `logging::log()`, whose output
 * `PIXELROOT32_DEBUG_MODE` gates.
 *
 * It exists as a symbol and not as an inline helper so the C <-> C++ bridge can
 * call the same code the scene does. That is what makes the boot-time `LINK,...`
 * line come out of the same door as every other line in `DoomScene.cpp`, even
 * though the C++ bridge and not the scene is the caller.
 *
 * The engine include is probed, not required: `env:link_test` installs no
 * library dependency, so the engine header is not on its include path and
 * `DoomConsole.cpp` falls back to stdio. That fallback is deliberate -- it is
 * the proof that the seam carries no dependency on the engine.
 */
namespace doom {
namespace doomc {

/**
 * @brief Writes one already-terminated line through the engine's console.
 *
 * No allocation, no formatting, no state: the caller owns the buffer and this
 * adds nothing to the line. Same contract as the engine primitive it wraps, so
 * a caller may pass a stack buffer that dies with the call.
 */
void writeLine(const char* line);

}  // namespace doomc
}  // namespace doom
