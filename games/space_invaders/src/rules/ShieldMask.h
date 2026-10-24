#pragma once

#include <cstdint>

#include "rules/RulesConstants.h"

namespace spaceinvaders::rules {

class ShieldField;

/**
 * @brief Erodes the first solid pixel a shot crosses on its way through a shield.
 *
 * A shot advances several pixels per tick, so testing only where it ended can
 * skip a row and let it pass through an intact wall. Walking the straight
 * segment from the previous position one pixel at a time finds the cell the
 * shot actually entered, and that cell is eroded through ShieldField::carve()
 * (S10). Coordinates are shield-local: the caller owns the world conversion.
 *
 * @param field The shield set to erode.
 * @param shield Shield index, 0..ShieldCount-1.
 * @param fromX Column the shot occupied on the previous tick.
 * @param fromY Row the shot occupied on the previous tick.
 * @param toX Current shot column.
 * @param toY Current shot row.
 * @return true when the segment crossed a solid pixel; the blast is applied.
 */
bool carveSwept(ShieldField& field, int shield, int fromX, int fromY, int toX, int toY);

/**
 * @class ShieldField
 * @brief The four destructible 22x16 bitmasks and their erosion (S10).
 *
 * A shield is a wall, not a health bar (F13). Each row is 22 pixels wide and
 * stored as a uint32_t whose bit 0 is the leftmost pixel, which is the order
 * the drawing code emits runs in and the opposite of the two backends'
 * drawBitmap orders (D6). The initial shape has cut top corners and a
 * bottom-centre arch that the cannon can shoot through.
 */
class ShieldField {
public:
    /**
     * @brief Restores every shield to the intact reference shape.
     *
     * Row 0 is solid from x=2 to x=19, rows 1-11 span the full width, and rows
     * 12-15 carry the arch: x=9..12, 8..13, 7..14 and 6..15 are cleared.
     */
    void reset();

    /**
     * @brief Tests one pixel of one shield.
     * @param shield Shield index, 0..ShieldCount-1.
     * @param x Column, 0..ShieldWidth-1.
     * @param y Row, 0..ShieldHeight-1.
     * @return true when the pixel is still part of the wall.
     */
    bool solid(int shield, int x, int y) const;

    /**
     * @brief Clears a 3x3 blast centred on a pixel.
     *
     * The block is clipped to the mask, so a hit on an edge clears only the
     * cells that exist. A shield with no bits left is reported destroyed.
     *
     * @param shield Shield index, 0..ShieldCount-1.
     * @param x Impact column, 0..ShieldWidth-1.
     * @param y Impact row, 0..ShieldHeight-1.
     */
    void carve(int shield, int x, int y);

    /**
     * @brief Reports whether a shield has lost every pixel.
     * @param shield Shield index, 0..ShieldCount-1.
     * @return true when all sixteen rows are empty.
     */
    bool destroyed(int shield) const;

    /**
     * @brief One shield row as a bit mask.
     *
     * Bit 0 is the leftmost pixel, so the value can be turned into runs
     * directly. Bits at or above ShieldWidth are always zero.
     *
     * @param shield Shield index, 0..ShieldCount-1.
     * @param y Row, 0..ShieldHeight-1.
     * @return Row bits.
     */
    uint32_t rowBits(int shield, int y) const;

private:
    uint32_t rowMask[ShieldCount][ShieldHeight] = {};
};

}  // namespace spaceinvaders::rules
