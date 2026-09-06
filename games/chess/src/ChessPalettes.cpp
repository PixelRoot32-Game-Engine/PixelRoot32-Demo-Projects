/*
 * ChessPalettes.cpp - Loading the engine palettes, in the order that matters.
 */
#include "ChessPalettes.h"

#include "assets/ChessCustomPalette.h"

namespace chessdemo {

namespace gfx = pixelroot32::graphics;

namespace {

constexpr gfx::PaletteType kBackgroundPalette = gfx::PaletteType::CHESS_BACKGROUND_PALETTE;
constexpr gfx::PaletteType kSpritePalette     = gfx::PaletteType::CHESS_SPRITE_PALETTE;

/*
 * The renderer stores a pointer to the context, so these have to outlive every
 * draw that reads them. File scope is the simplest way to guarantee that; a
 * local inside PaletteScope's constructor would dangle the moment it returned.
 */
gfx::PaletteContext backgroundContext = gfx::PaletteContext::Background;
gfx::PaletteContext spriteContext     = gfx::PaletteContext::Sprite;

}  // namespace

void applyPalettes() {
    /*
     * Order matters, and not for a cosmetic reason.
     *
     * The engine keeps three pointers: backgroundPalette, spritePalette, and a
     * third, currentPalette, left over from single-palette mode. resolveColor
     * has two overloads, and the SINGLE-argument one reads currentPalette and
     * ignores dual mode completely. setDualPalette and setDualCustomPalette
     * never touch currentPalette.
     *
     * That matters here because ParticleEmitter resolves its colours through
     * exactly that single-argument overload. Call only the dual setter and the
     * capture debris keeps resolving through whichever palette happened to be
     * loaded before, while every other colour on screen moves - a divergence
     * with no error and no obvious cause.
     *
     * So the base palette is set first: setPalette points all three at the
     * sprite choice, which is what the debris should match, and only then does
     * the dual setter split the background off.
     */
    gfx::setPalette(kSpritePalette);

#if CHESS_CUSTOM_BACKGROUND
    // The sprite table is read back from slot 0 rather than mapped from the
    // PaletteType a second time - setPalette just pointed it at the right one.
    //
    // Neither pointer is copied by the engine. kChessCustomPalette is a
    // file-scope array in the generated header and the slot table is the
    // engine's own, so both outlive the call; a stack-local table here would
    // leave the renderer reading freed memory.
    gfx::setDualCustomPalette(kChessCustomPalette, gfx::getSpritePaletteSlot(0));
#else
    gfx::setDualPalette(kBackgroundPalette, kSpritePalette);
#endif
}

PaletteScope::PaletteScope(gfx::Renderer& renderer, gfx::PaletteContext context)
    : renderer_(renderer), previous_(renderer.getRenderContext()) {
    renderer_.setRenderContext(context == gfx::PaletteContext::Background
                                   ? &backgroundContext
                                   : &spriteContext);
}

PaletteScope::~PaletteScope() {
    // Restoring the previous pointer rather than clearing to nullptr is what
    // makes these nest: an inner scope hands the outer one back untouched.
    renderer_.setRenderContext(previous_);
}

}  // namespace chessdemo
