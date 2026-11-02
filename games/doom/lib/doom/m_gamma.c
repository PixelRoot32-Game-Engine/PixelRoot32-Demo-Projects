/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * v_video.c's gammatable[5][256] and the one place it is read. `usegamma` is
 * the menu's gamma value; I_SetPalette applies the table to every PLAYPAL
 * channel, which the board's palette module does when the value changes.
 *
 * The 1,280 bytes are const: they live in flash (.rodata) and cost no DRAM.
 */
#include "m_menu.h"

static const uint8_t gammatable[M_GAMMA_MAX + 1][256] = {
#include "m_gamma_table.inc"
};

uint8_t m_gamma_correct(uint8_t level, uint8_t value) {
    if (level > M_GAMMA_MAX) {
        level = M_GAMMA_MAX;
    }
    return gammatable[level][value];
}
