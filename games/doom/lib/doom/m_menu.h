/*
 * DOOM on PixelRoot32 - the menu and the setup-lite settings.
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
 *   MAIN      NEW GAME, OPTIONS, LOAD GAME, SAVE GAME
 *                                     (MainDef, in upstream's order; READ THIS
 *                                     and QUIT are not here: the port has no help
 *                                     screens and nothing to quit to)
 *   NEWGAME   the five skill levels      (NewDef; the episode screen is skipped,
 *                                         shareware has one episode and its
 *                                         other rows only show the "registered
 *                                         version" notice)
 *   OPTIONS   skill, volume, gamma, mute  (setup-lite)
 *   LOAD      six slot rows               (a slot label or EMPTY)
 *   SAVE      six slot rows               (same rows, same labels)
 *
 * The two slot screens are drawn from `menu->slotLabels`, which the owner fills
 * before opening them (`m_menu_set_slot_labels`). This file never reads a store:
 * it does not know what a save slot is made of, which is why it stays C and
 * engine-free. An activation is reported to the owner as a distinct action
 * (`m_menu_take_slot_action`, and `m_menu_key`'s return value), and the owner
 * does the load or the save.
 *
 * A LOAD row whose label is "EMPTY" does nothing and keeps the screen open.
 * A SAVE over a used row saves immediately: vanilla asks "are you sure?", and
 * this port has no confirm dialog -- adding one is a screen and two keys on a
 * six-button pad, so a save is one key press, like New Game.
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
#define M_MUTE_MAX 1        /* the mute row is a slider with two positions */

/*
 * LOAD GAME / SAVE GAME.
 *
 * Six is vanilla's own slot count and the most this board's 32 KB `saves`
 * partition holds honestly. A label is at most 15 characters plus the
 * terminator -- the record's own header field, "E1M1 HURT 03:12" -- so the menu
 * can draw it without knowing where it came from.
 */
#define M_SLOT_COUNT 6
#define M_SLOT_LABEL_SIZE 16
#define M_SLOT_EMPTY "EMPTY"

/* The four persisted values. */
typedef struct m_settings {
    uint8_t skill;   /* 0..M_SKILL_COUNT-1 */
    uint8_t volume;  /* 0..M_VOLUME_MAX */
    uint8_t gamma;   /* 0..M_GAMMA_MAX */
    /*
     * 0 or 1. A separate field, not a zero volume: the player's slider
     * position has to survive being muted, or unmuting could not restore it.
     */
    uint8_t muted;
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
 *   6  mute flag      0 = off, 1 = on
 *   7  checksum       (0xA5 + the sum of bytes 0..6) modulo 256
 *
 * Byte 6 was "reserved, must be 0" in earlier records, which is exactly why the flag
 * lives there: a record an older board wrote decodes as "not muted" with no
 * version bump and without touching a saved skill, volume or gamma. The
 * checksum already covers it, so a damaged flag byte is caught twice -- by the
 * checksum and by the 0..1 range test.
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
    M_SCREEN_LOAD = 3,
    M_SCREEN_SAVE = 4,
    M_SCREEN_COUNT = 5
} m_screen_t;

typedef enum m_key {
    M_KEY_UP = 0,
    M_KEY_DOWN = 1,
    M_KEY_LEFT = 2,
    M_KEY_RIGHT = 3,
    M_KEY_ENTER = 4,
    M_KEY_BACK = 5
} m_key_t;

/*
 * What a key meant to the menu's OWNER.
 *
 * M_ACTION_KEY is 1 so `m_menu_key` keeps its old contract for every path that
 * predates the save slots ("nonzero = the menu was open and spent the key"). The two
 * slot actions are distinct values above it; the owner's `m_menu_take_slot_action`
 * reports them once, together with the row in `menu->slot`.
 */
typedef enum m_action {
    M_ACTION_NONE = 0,       /* the menu was closed: the key was never seen */
    M_ACTION_KEY = 1,        /* the menu spent the key and nothing is owed */
    M_ACTION_LOAD_SLOT = 2,  /* LOAD GAME confirmed on row `menu->slot` */
    M_ACTION_SAVE_SLOT = 3   /* SAVE GAME confirmed on row `menu->slot` */
} m_action_t;

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
    /*
     * The six slot labels the owner filled, and the row an activation
     * named. The labels live here rather than behind a callback so the menu
     * never reads a store and a closed menu costs no call. `pendingAction` is
     * M_ACTION_NONE until an activation; `slot` is that row (0..5) and stays
     * readable after the action is taken.
     */
    uint8_t slotLabels[M_SLOT_COUNT][M_SLOT_LABEL_SIZE];
    uint8_t slot;
    int8_t pendingAction;            /* M_ACTION_NONE or a slot action */
} m_menu_t;

/* Closed menu, MAIN selected, @p settings as the initial values (NULL = defaults). */
void m_menu_init(m_menu_t* menu, const m_settings_t* settings);

/* M_StartControlPanel(): opens on the main screen. No-op if already open. */
void m_menu_open(m_menu_t* menu);

/* M_ClearMenus(): closes the menu. */
void m_menu_close(m_menu_t* menu);

/* M_Responder() for one key. Returns M_ACTION_NONE when the menu was closed (the
 * key was never seen), M_ACTION_KEY when it was open and spent the key, and a
 * slot action when an activation is owed; a caller that only tests "nonzero"
 * still sees every spent key. */
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

/* --- The save slots ---------------------------------------------------------- */

/*
 * Fills the six slot labels the LOAD and SAVE screens draw. The owner calls it
 * before opening either screen (one label per row; "EMPTY" for a row with no
 * usable record), so the menu itself never reads a store. Each label is copied
 * and clipped to M_SLOT_LABEL_SIZE - 1 characters; a NULL label reads as
 * "EMPTY".
 */
void m_menu_set_slot_labels(m_menu_t* menu, const char* const labels[M_SLOT_COUNT]);

/*
 * A pending LOAD/SAVE: returns M_ACTION_LOAD_SLOT or M_ACTION_SAVE_SLOT once and
 * stores the row in @p slot, or M_ACTION_NONE when there is nothing to do. The
 * menu has already closed itself for a confirmed activation; the world is the
 * caller's to reload or write.
 */
int m_menu_take_slot_action(m_menu_t* menu, int* slot);

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
