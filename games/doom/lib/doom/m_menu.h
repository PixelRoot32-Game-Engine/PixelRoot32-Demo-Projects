/*
 * DOOM on PixelRoot32 - the menu and the setup-lite settings (D5.2).
 *
 * Derived from linuxdoom-1.10 m_menu.c (Copyright (C) 1993-1996 by id Software,
 * Inc.), licensed under the GNU General Public License v2. See NOTICE.
 */
#ifndef DOOM_M_MENU_H
#define DOOM_M_MENU_H

#include <stddef.h>
#include <stdint.h>

#include "st_stuff.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * THE MENU, AS m_menu.c STRUCTURES IT
 *
 * linuxdoom keeps one `menu_t` per screen (item table, previous menu, origin,
 * the item the cursor was last on) and `M_Responder()` is a switch over the key.
 * This port keeps the same shape as data and one pure state machine, with the
 * keys the board can produce. Nothing here reads a clock, a device or a file:
 *
 *   input     m_menu_buttons() takes the six button bits (InputMap.h's order),
 *             finds the rising edges and turns them into keys
 *   state     m_menu_t, about 24 bytes, owned by the caller
 *   output    m_menu_take_new_game() / m_menu_take_settings_changed()
 *   draw      m_menu_draw() paints the active screen into an 8bpp surface
 *
 * SCREENS
 *
 *   MAIN      NEW GAME, OPTIONS          (MainDef; LOAD/SAVE/READ THIS/QUIT are
 *                                         not here: the port has no save files,
 *                                         no help screens and nothing to quit to)
 *   NEWGAME   the five skill levels      (NewDef; the episode screen is skipped,
 *                                         shareware has one episode and its
 *                                         other rows only show the "registered
 *                                         version" notice)
 *   OPTIONS   skill, volume, gamma       (setup-lite)
 *
 * KEYS, and the buttons they come from
 *
 *   UP / DOWN       move the cursor, wrapping; a status -1 row is skipped
 *   LEFT / RIGHT    on a slider row, one step down / up (clamped, no wrap)
 *   ENTER (A)       on a screen row, go there; on NEWGAME, start the game at that
 *                   skill; on a slider row, one step up (upstream's routine(1))
 *   BACK (B)        to the previous screen, or close the menu from MAIN
 *
 * OPENING THE MENU. The board's six buttons are all taken by the game, and a
 * Start button is not known to be wired, so the menu opens on LEFT and RIGHT
 * held together: the one pair the game never needs (they cancel to no turn).
 * While the menu is open, or latched until every button is released, the world
 * is handed no buttons and its tic does not run.
 */

#define M_SKILL_COUNT 5     /* baby .. nightmare, p_skill_t */
#define M_SKILL_DEFAULT 2   /* "Hurt me plenty", NewDef's initial row */
#define M_VOLUME_MAX 15     /* snd_SfxVolume: 16 thermometer dots, 0..15 */
#define M_VOLUME_DEFAULT 8
#define M_GAMMA_MAX 4       /* usegamma: 5 levels, 0 is the identity */
#define M_GAMMA_DEFAULT 0

/* The three persisted values. */
typedef struct m_settings {
    uint8_t skill;   /* 0..M_SKILL_COUNT-1 */
    uint8_t volume;  /* 0..M_VOLUME_MAX */
    uint8_t gamma;   /* 0..M_GAMMA_MAX */
} m_settings_t;

/* --- Settings and their stored form ------------------------------------- */

/* The defaults a missing or corrupt store falls back to. */
void m_settings_default(m_settings_t* settings);

/* Clamps every field into its range (a value from a key can only be in range;
 * this is for callers that build a struct by hand). */
void m_settings_clamp(m_settings_t* settings);

/*
 * The stored form: 8 bytes.
 *
 *   0  'D'            magic
 *   1  'M'
 *   2  1              version
 *   3  skill
 *   4  volume
 *   5  gamma
 *   6  0              reserved
 *   7  checksum       (0xA5 + the sum of bytes 0..6) modulo 256
 */
#define M_SETTINGS_BLOB_SIZE 8u

void m_settings_encode(const m_settings_t* settings, uint8_t blob[M_SETTINGS_BLOB_SIZE]);

/*
 * Decodes a stored blob. Returns 1 and the stored values when the size, magic,
 * version, checksum and every range are right. Returns 0 and the DEFAULTS in
 * @p out for anything else, including a NULL or empty blob (a store that has
 * never been written): corruption is never half-trusted and never clamped.
 */
int m_settings_decode(const uint8_t* blob, size_t size, m_settings_t* out);

/* --- Gamma --------------------------------------------------------------- */

/* gammatable[level][value] (m_gamma.c). Out-of-range levels clamp to M_GAMMA_MAX. */
uint8_t m_gamma_correct(uint8_t level, uint8_t value);

/* --- The state machine --------------------------------------------------- */

typedef enum m_screen {
    M_SCREEN_MAIN = 0,
    M_SCREEN_NEWGAME = 1,
    M_SCREEN_OPTIONS = 2,
    M_SCREEN_COUNT = 3
} m_screen_t;

typedef enum m_key {
    M_KEY_UP = 0,
    M_KEY_DOWN = 1,
    M_KEY_LEFT = 2,
    M_KEY_RIGHT = 3,
    M_KEY_ENTER = 4,
    M_KEY_BACK = 5
} m_key_t;

/* The button bits m_menu_buttons() reads: src/doomc/InputMap.h's order, restated
 * here because this is C; the C++ side static_asserts the two agree. */
#define M_BTN_UP 0x01u
#define M_BTN_DOWN 0x02u
#define M_BTN_LEFT 0x04u
#define M_BTN_RIGHT 0x08u
#define M_BTN_A 0x10u
#define M_BTN_B 0x20u
#define M_BTN_START 0x40u /* the board's GPIO 34; menu-only, never reaches the world */

typedef struct m_menu {
    m_settings_t settings;
    uint8_t active;                  /* menuactive */
    uint8_t screen;                  /* currentMenu */
    uint8_t itemOn[M_SCREEN_COUNT];  /* itemOn / each menu's lastOn */
    uint8_t whichSkull;              /* 0 or 1: M_SKULL1 / M_SKULL2 */
    uint8_t latched;                 /* world input stays masked until all released */
    uint8_t prevButtons;
    uint8_t settingsChanged;         /* a persisted value moved since last take */
    int8_t pendingSkill;             /* -1, or a New Game the owner must start */
    int16_t skullCounter;            /* skullAnimCounter */
} m_menu_t;

/* Closed menu, MAIN selected, @p settings as the initial values (NULL = defaults). */
void m_menu_init(m_menu_t* menu, const m_settings_t* settings);

/* M_StartControlPanel(): opens on the main screen. No-op if already open. */
void m_menu_open(m_menu_t* menu);

/* M_ClearMenus(): closes the menu. */
void m_menu_close(m_menu_t* menu);

/* M_Responder() for one key. Returns 1 when the menu was open (the key is spent). */
int m_menu_key(m_menu_t* menu, m_key_t key);

/*
 * One frame's held buttons. The Start rising edge opens the menu, and closes it
 * while it is open; each other rising edge is fed to m_menu_key() while it is
 * open. Returns the buttons the WORLD may see (never the Start bit): the held bits when the menu is closed and unlatched, otherwise
 * 0. Use m_menu_active() to decide whether the world tic runs.
 */
uint8_t m_menu_buttons(m_menu_t* menu, uint8_t held);

/* M_Ticker(): @p tics of skull animation. */
void m_menu_tick(m_menu_t* menu, uint32_t tics);

static inline int m_menu_active(const m_menu_t* menu) { return menu != NULL && menu->active; }

/* A pending New Game: returns 1 once and stores its skill in @p skill. */
int m_menu_take_new_game(m_menu_t* menu, int* skill);

/* Returns 1 once after a persisted value changed. */
int m_menu_take_settings_changed(m_menu_t* menu);

/* --- Drawing ------------------------------------------------------------- */

/*
 * Paints the active screen over @p surface (palette indices, R_SCREEN_WIDTH
 * wide, R_SCREEN_HEIGHT rows), patches looked up by name through @p lump. A
 * closed menu paints nothing. Returns the number of patches that could not be
 * drawn (missing or malformed); a bad patch is skipped, never half drawn.
 */
int m_menu_draw(const m_menu_t* menu, st_lump_fn lump, void* context, uint8_t* surface,
                int stride);

/* The skill row's text ("I'M TOO YOUNG TO DIE." ...). Out-of-range clamps. */
const char* m_skill_name(int skill);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_M_MENU_H */
