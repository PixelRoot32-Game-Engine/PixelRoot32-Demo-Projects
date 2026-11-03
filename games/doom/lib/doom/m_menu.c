/*
 * DOOM on PixelRoot32 - the menu and the setup-lite settings.
 *
 * Derived from linuxdoom-1.10 m_menu.c and v_video.c (V_DrawPatch)
 * (Copyright (C) 1993-1996 id Software, Inc.), licensed under the GNU General
 * Public License v2. See NOTICE.
 *
 * What changed from upstream, and why:
 *
 *   - The menus are the same data: a `menu_t` table per screen (rows, origin,
 *     previous screen) and a responder that moves `itemOn` with wrap-around,
 *     skips status -1 rows, steps a slider with left/right, enters on ENTER and
 *     goes back (or closes, from the main screen) on BACK. The keys are the
 *     board's buttons, not a keyboard; see m_menu.h.
 *   - No READ THIS or QUIT GAME: the port has no help screens and nothing to
 *     quit to. No episode screen: shareware has one episode. No "are you sure?"
 *     for nightmare: the board has no Y/N keys, so ENTER on the nightmare row
 *     starts the game.
 *   - LOAD GAME and SAVE GAME sit in upstream's order after
 *     OPTIONS, each a six-row slot screen. Their titles and rows are TEXT, not
 *     `M_LOADG`/`M_SAVEG` patches: those two lumps are 2,332 B and 2,364 B and
 *     the container has 1,164 B of spare in the `wad` row, so drawing them
 *     would mean a container and partition change for two pictures the HU font
 *     already spells. The rest of the main screen keeps its patch rows.
 *   - The options screen is the port's "setup-lite": skill, sound volume,
 *     gamma and mute, drawn with the HU font (M_WriteText) and the thermometer
 *     patches. Upstream has no gamma row (F11 cycled it); the table is
 *     v_video.c's. The mute row is a slider with two positions (OFF/ON) rather
 *     than a new row type: the state machine already has "a value with a low
 *     and a high", and a second kind of row would be a second code path to gate.
 *   - Settings are plain bytes with a checksummed stored form (m_settings_*),
 *     because the menu's owner persists them and this file must stay free of any
 *     storage API. Slot labels are strings the owner fills for the same reason.
 *   - The state is a caller-owned struct, not file-scope globals.
 */
#include "m_menu.h"

#include <string.h>

/* v_video.c / hu_stuff.h: the HU font is STCFN033..STCFN095. */
#define M_FONT_FIRST 33
#define M_FONT_LAST 95

/* m_menu.c's constants. */
#define LINEHEIGHT 16
#define SKULLXOFF -32
#define SKULL_ANIM_FIRST 10  /* skullAnimCounter = 10 in M_Init */
#define SKULL_ANIM_NEXT 8    /* ... and 8 after each flip in M_Ticker */

/* Row status, upstream's menuitem_t.status: 1 selectable, 2 selectable with a
 * left/right adjustment, -1 a gap the cursor skips. */
enum { ROW_GAP = -1, ROW_ENTER = 1, ROW_SLIDER = 2 };

typedef struct m_row {
    int8_t status;
    const char* patch;  /* the row's picture, or NULL for a text row */
    const char* text;   /* the row's text when `patch` is NULL */
} m_row_t;

typedef struct m_def {
    uint8_t rows;
    int8_t prev;  /* the screen BACK returns to, or -1 to close */
    const m_row_t* items;
    int16_t x;
    int16_t y;
} m_def_t;

/* MainMenu[] / MainDef: upstream's order, less READ THIS and QUIT. LOAD GAME and
 * SAVE GAME are text rows (see the file comment); the first two keep their
 * patches. */
static const m_row_t kMainRows[] = {
    {ROW_ENTER, "M_NGAME", NULL},
    {ROW_ENTER, "M_OPTION", NULL},
    {ROW_ENTER, NULL, "LOAD GAME"},
    {ROW_ENTER, NULL, "SAVE GAME"},
};

/* NewGameMenu[] / NewDef: the skill levels, baby to nightmare. */
static const m_row_t kNewGameRows[M_SKILL_COUNT] = {
    {ROW_ENTER, "M_JKILL", NULL},
    {ROW_ENTER, "M_ROUGH", NULL},
    {ROW_ENTER, "M_HURT", NULL},
    {ROW_ENTER, "M_ULTRA", NULL},
    {ROW_ENTER, "M_NMARE", NULL},
};

/* The setup-lite rows are text, so they carry no patch name. */
static const m_row_t kOptionsRows[] = {
    {ROW_SLIDER, NULL, NULL},  /* skill */
    {ROW_SLIDER, NULL, NULL},  /* sound volume */
    {ROW_SLIDER, NULL, NULL},  /* gamma */
    {ROW_SLIDER, NULL, NULL},  /* mute, a slider with a range of one (OFF/ON) */
};

/* LoadDef / SaveDef: six rows, each the owner's label for that slot. Vanilla
 * draws them with M_WriteText at x 80, y 54 and a 16 px line height, which is
 * what these rows and kSlotTitleY repeat. */
#define SLOT_SCREEN_X 80
#define SLOT_SCREEN_Y 54
#define SLOT_TITLE_X 120
#define SLOT_TITLE_Y 24
static const m_row_t kSlotRows[M_SLOT_COUNT] = {
    {ROW_ENTER, NULL, NULL}, {ROW_ENTER, NULL, NULL}, {ROW_ENTER, NULL, NULL},
    {ROW_ENTER, NULL, NULL}, {ROW_ENTER, NULL, NULL}, {ROW_ENTER, NULL, NULL},
};

#define OPTIONS_Y0 40
#define OPTIONS_STEP 36
#define OPTIONS_VALUE_DY 14

static const m_def_t kMenus[M_SCREEN_COUNT] = {
    /* MainDef: x 97, y 64 */
    {sizeof kMainRows / sizeof kMainRows[0], -1, kMainRows, 97, 64},
    /* NewDef: x 48, y 63 */
    {M_SKILL_COUNT, M_SCREEN_MAIN, kNewGameRows, 48, 63},
    /* The setup-lite screen; its rows sit OPTIONS_STEP apart. */
    {sizeof kOptionsRows / sizeof kOptionsRows[0], M_SCREEN_MAIN, kOptionsRows, 60,
     OPTIONS_Y0},
    /* LoadDef: x 80, y 54 */
    {M_SLOT_COUNT, M_SCREEN_MAIN, kSlotRows, SLOT_SCREEN_X, SLOT_SCREEN_Y},
    /* SaveDef: the same geometry, the same rows */
    {M_SLOT_COUNT, M_SCREEN_MAIN, kSlotRows, SLOT_SCREEN_X, SLOT_SCREEN_Y},
};

static const char* const kSkillNames[M_SKILL_COUNT] = {
    "I'M TOO YOUNG TO DIE.", "HEY, NOT TOO ROUGH.", "HURT ME PLENTY.", "ULTRA-VIOLENCE.",
    "NIGHTMARE!",
};

/* --- Settings -------------------------------------------------------------- */

void m_settings_default(m_settings_t* settings) {
    if (settings == NULL) {
        return;
    }
    settings->skill = M_SKILL_DEFAULT;
    settings->volume = M_VOLUME_DEFAULT;
    settings->gamma = M_GAMMA_DEFAULT;
    settings->muted = 0;
}

void m_settings_clamp(m_settings_t* settings) {
    if (settings == NULL) {
        return;
    }
    if (settings->skill > M_SKILL_COUNT - 1) {
        settings->skill = M_SKILL_COUNT - 1;
    }
    if (settings->volume > M_VOLUME_MAX) {
        settings->volume = M_VOLUME_MAX;
    }
    if (settings->gamma > M_GAMMA_MAX) {
        settings->gamma = M_GAMMA_MAX;
    }
    if (settings->muted > M_MUTE_MAX) {
        settings->muted = M_MUTE_MAX;
    }
}

static uint8_t blobChecksum(const uint8_t* blob) {
    unsigned sum = 0xA5u;
    unsigned index;

    for (index = 0; index < M_SETTINGS_BLOB_SIZE - 1u; ++index) {
        sum += blob[index];
    }
    return (uint8_t)(sum & 0xFFu);
}

void m_settings_encode(const m_settings_t* settings, uint8_t blob[M_SETTINGS_BLOB_SIZE]) {
    m_settings_t clean;

    if (blob == NULL) {
        return;
    }
    m_settings_default(&clean);
    if (settings != NULL) {
        clean = *settings;
    }
    m_settings_clamp(&clean);
    blob[0] = 'D';
    blob[1] = 'M';
    blob[2] = 1;
    blob[3] = clean.skill;
    blob[4] = clean.volume;
    blob[5] = clean.gamma;
    /* The reserved byte is the mute flag. An older record holds 0 here,
     * which is "not muted", so nothing a board already stored is invalidated. */
    blob[6] = clean.muted ? 1u : 0u;
    blob[7] = blobChecksum(blob);
}

int m_settings_decode(const uint8_t* blob, size_t size, m_settings_t* out) {
    if (out == NULL) {
        return 0;
    }
    m_settings_default(out);
    if (blob == NULL || size != M_SETTINGS_BLOB_SIZE) {
        return 0;
    }
    if (blob[0] != 'D' || blob[1] != 'M' || blob[2] != 1 || blob[6] > M_MUTE_MAX
        || blob[7] != blobChecksum(blob)) {
        return 0;
    }
    if (blob[3] > M_SKILL_COUNT - 1 || blob[4] > M_VOLUME_MAX || blob[5] > M_GAMMA_MAX) {
        return 0;
    }
    out->skill = blob[3];
    out->volume = blob[4];
    out->gamma = blob[5];
    out->muted = blob[6];
    return 1;
}

/* --- The state machine ----------------------------------------------------- */

void m_menu_init(m_menu_t* menu, const m_settings_t* settings) {
    if (menu == NULL) {
        return;
    }
    memset(menu, 0, sizeof *menu);
    m_settings_default(&menu->settings);
    if (settings != NULL) {
        menu->settings = *settings;
        m_settings_clamp(&menu->settings);
    }
    /* NewDef.lastOn: the saved skill is where the cursor starts. */
    menu->itemOn[M_SCREEN_NEWGAME] = menu->settings.skill;
    menu->skullCounter = SKULL_ANIM_FIRST;
    menu->pendingSkill = -1;
    /* Nothing is owed and no slot is named until the owner fills the labels
     * and an activation happens. An unfilled row reads EMPTY, so a menu that is
     * opened before the owner filled anything cannot load a slot. */
    menu->pendingAction = (int8_t)M_ACTION_NONE;
    menu->slot = 0u;
    for (uint8_t index = 0; index < M_SLOT_COUNT; ++index) {
        strcpy((char*)menu->slotLabels[index], M_SLOT_EMPTY);
    }
}

void m_menu_open(m_menu_t* menu) {
    if (menu == NULL || menu->active) {
        return;
    }
    /* M_StartControlPanel(): currentMenu = &MainDef; itemOn = currentMenu->lastOn. */
    menu->active = 1;
    menu->screen = M_SCREEN_MAIN;
}

void m_menu_close(m_menu_t* menu) {
    if (menu == NULL) {
        return;
    }
    /* M_ClearMenus(): menuactive = 0. The next open starts on MainDef again. */
    menu->active = 0;
    menu->screen = M_SCREEN_MAIN;
}

/* A slider row stepped by @p direction (-1 or +1), clamped at both ends. A
 * persisted value that did not move sets no flag. */
static void slide(m_menu_t* menu, int row, int direction) {
    uint8_t* value;
    int high;
    int next;

    switch (row) {
        case 0:
            value = &menu->settings.skill;
            high = M_SKILL_COUNT - 1;
            break;
        case 1:
            value = &menu->settings.volume;
            high = M_VOLUME_MAX;
            break;
        case 2:
            value = &menu->settings.gamma;
            high = M_GAMMA_MAX;
            break;
        default:
            /* The mute row: a slider with two positions, so the same clamp and
             * the same "only a moved value reports a change" rule apply. */
            value = &menu->settings.muted;
            high = M_MUTE_MAX;
            break;
    }
    next = (int)*value + direction;
    if (next < 0) {
        next = 0;
    }
    if (next > high) {
        next = high;
    }
    if (next != (int)*value) {
        *value = (uint8_t)next;
        menu->settingsChanged = 1;
    }
}

int m_menu_key(m_menu_t* menu, m_key_t key) {
    const m_def_t* def;
    uint8_t* itemOn;

    if (menu == NULL || !menu->active) {
        return (int)M_ACTION_NONE;
    }
    def = &kMenus[menu->screen];
    itemOn = &menu->itemOn[menu->screen];

    switch (key) {
        case M_KEY_DOWN:
            do {
                *itemOn = (uint8_t)(*itemOn + 1u >= def->rows ? 0u : *itemOn + 1u);
            } while (def->items[*itemOn].status == ROW_GAP);
            break;
        case M_KEY_UP:
            do {
                *itemOn = (uint8_t)(*itemOn == 0u ? def->rows - 1u : *itemOn - 1u);
            } while (def->items[*itemOn].status == ROW_GAP);
            break;
        case M_KEY_LEFT:
        case M_KEY_RIGHT:
            if (def->items[*itemOn].status == ROW_SLIDER && menu->screen == M_SCREEN_OPTIONS) {
                slide(menu, *itemOn, key == M_KEY_LEFT ? -1 : 1);
            }
            break;
        case M_KEY_ENTER:
            if (menu->screen == M_SCREEN_MAIN) {
                if (*itemOn == 0u) {
                    /* M_NewGame -> NewDef, cursor on the saved skill. */
                    menu->itemOn[M_SCREEN_NEWGAME] = menu->settings.skill;
                    menu->screen = M_SCREEN_NEWGAME;
                } else if (*itemOn == 1u) {
                    menu->screen = M_SCREEN_OPTIONS;
                } else {
                    /* LoadDef / SaveDef: the owner has already filled the
                     * six labels, so the screen opens with its rows readable. */
                    menu->itemOn[*itemOn == 2u ? M_SCREEN_LOAD : M_SCREEN_SAVE] = 0u;
                    menu->screen = *itemOn == 2u ? M_SCREEN_LOAD : M_SCREEN_SAVE;
                }
            } else if (menu->screen == M_SCREEN_NEWGAME) {
                /* M_ChooseSkill: G_DeferedInitNew(choice) then M_ClearMenus(). */
                menu->pendingSkill = (int8_t)*itemOn;
                if (menu->settings.skill != *itemOn) {
                    menu->settings.skill = *itemOn;
                    menu->settingsChanged = 1;
                }
                m_menu_close(menu);
            } else if (menu->screen == M_SCREEN_LOAD || menu->screen == M_SCREEN_SAVE) {
                /* A LOAD row with no record does nothing and leaves the screen
                 * open, so a mistyped press cannot close the menu over a world
                 * that did not change. A SAVE over any row is the owner's to
                 * perform: there is no confirm dialog here (m_menu.h). */
                const int load = menu->screen == M_SCREEN_LOAD;
                const int empty =
                    strcmp((const char*)menu->slotLabels[*itemOn], M_SLOT_EMPTY) == 0;
                if (load && empty) {
                    break;
                }
                menu->slot = *itemOn;
                menu->pendingAction =
                    (int8_t)(load ? M_ACTION_LOAD_SLOT : M_ACTION_SAVE_SLOT);
                m_menu_close(menu);
                return (int)menu->pendingAction;
            } else if (def->items[*itemOn].status == ROW_SLIDER) {
                slide(menu, *itemOn, 1);  /* upstream: routine(1) */
            }
            break;
        case M_KEY_BACK:
            if (def->prev < 0) {
                m_menu_close(menu);
            } else {
                menu->screen = (uint8_t)def->prev;
            }
            break;
        default:
            break;
    }
    return (int)M_ACTION_KEY;
}

uint8_t m_menu_buttons(m_menu_t* menu, uint8_t held) {
    uint8_t rising;
    static const struct {
        uint8_t mask;
        m_key_t key;
    } kKeys[] = {
        {M_BTN_UP, M_KEY_UP},       {M_BTN_DOWN, M_KEY_DOWN}, {M_BTN_LEFT, M_KEY_LEFT},
        {M_BTN_RIGHT, M_KEY_RIGHT}, {M_BTN_A, M_KEY_ENTER},   {M_BTN_B, M_KEY_BACK},
    };
    unsigned index;

    if (menu == NULL) {
        return held;
    }
    rising = (uint8_t)(held & ~menu->prevButtons);
    menu->prevButtons = held;

    /* After opening or closing, the world sees nothing until every button is up:
     * the Start press that opened or closed must not reach the game, and the B that
     * backed out must not become a USE. */
    if (menu->latched) {
        if (held == 0u) {
            menu->latched = 0;
        }
        return 0;
    }

    if (!menu->active) {
        /* The Start edge opens it; Start is never a game button. */
        if ((rising & M_BTN_START) != 0u) {
            m_menu_open(menu);
            menu->latched = 1;
            return 0;
        }
        return (uint8_t)(held & ~M_BTN_START);
    }

    /* Start closes it from any screen (Esc toggles the menu in DOOM). */
    if ((rising & M_BTN_START) != 0u) {
        m_menu_close(menu);
        menu->latched = 1;
        return 0;
    }

    /* Both horizontal buttons pressed in the same frame cancel, as they do for
     * the turn axis. */
    if ((rising & (M_BTN_LEFT | M_BTN_RIGHT)) == (M_BTN_LEFT | M_BTN_RIGHT)) {
        rising = (uint8_t)(rising & ~(M_BTN_LEFT | M_BTN_RIGHT));
    }
    for (index = 0; index < sizeof kKeys / sizeof kKeys[0]; ++index) {
        if ((rising & kKeys[index].mask) != 0u && menu->active) {
            (void)m_menu_key(menu, kKeys[index].key);
        }
    }
    if (!menu->active) {
        menu->latched = 1;
    }
    return 0;
}

void m_menu_tick(m_menu_t* menu, uint32_t tics) {
    if (menu == NULL) {
        return;
    }
    while (tics-- > 0u) {
        /* M_Ticker(): if (--skullAnimCounter <= 0) { whichSkull ^= 1; counter = 8; } */
        if (--menu->skullCounter <= 0) {
            menu->whichSkull ^= 1u;
            menu->skullCounter = SKULL_ANIM_NEXT;
        }
    }
}

int m_menu_take_new_game(m_menu_t* menu, int* skill) {
    if (menu == NULL || menu->pendingSkill < 0) {
        return 0;
    }
    if (skill != NULL) {
        *skill = menu->pendingSkill;
    }
    menu->pendingSkill = -1;
    return 1;
}

int m_menu_take_settings_changed(m_menu_t* menu) {
    int changed;

    if (menu == NULL) {
        return 0;
    }
    changed = menu->settingsChanged;
    menu->settingsChanged = 0;
    return changed;
}

void m_menu_set_slot_labels(m_menu_t* menu, const char* const labels[M_SLOT_COUNT]) {
    uint8_t index;

    if (menu == NULL) {
        return;
    }
    for (index = 0; index < M_SLOT_COUNT; ++index) {
        const char* label = labels != NULL ? labels[index] : NULL;
        size_t length;

        /* A NULL row (or a label the owner could not build) reads as EMPTY: the
         * menu never shows a blank row, which a player reads as a bug. */
        if (label == NULL) {
            label = M_SLOT_EMPTY;
        }
        length = strlen(label);
        if (length >= M_SLOT_LABEL_SIZE) {
            length = M_SLOT_LABEL_SIZE - 1u;
        }
        memcpy(menu->slotLabels[index], label, length);
        menu->slotLabels[index][length] = '\0';
    }
}

int m_menu_take_slot_action(m_menu_t* menu, int* slot) {
    int action;

    if (menu == NULL || menu->pendingAction == (int8_t)M_ACTION_NONE) {
        return (int)M_ACTION_NONE;
    }
    action = menu->pendingAction;
    menu->pendingAction = (int8_t)M_ACTION_NONE;
    if (slot != NULL) {
        *slot = (int)menu->slot;
    }
    return action;
}

const char* m_skill_name(int skill) {
    if (skill < 0) {
        skill = 0;
    }
    if (skill > M_SKILL_COUNT - 1) {
        skill = M_SKILL_COUNT - 1;
    }
    return kSkillNames[skill];
}

/* --- Drawing --------------------------------------------------------------- */

static int16_t le16(const uint8_t* p) { return (int16_t)(p[0] | (p[1] << 8)); }

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

/* Fetches @p name and checks its header and column table. 0 on failure. */
static int fetch(st_lump_fn lump, void* context, const char* name, const uint8_t** data,
                 size_t* size, int* width) {
    *data = lump(context, name, size);
    if (*data == NULL || *size < 8u) {
        return 0;
    }
    *width = le16(*data);
    if (*width < 1 || 8u + 4u * (size_t)*width > *size) {
        return 0;
    }
    return 1;
}

/* V_DrawPatch(): the patch's own offsets move it; every pixel is bounds-checked.
 * Returns the patch width through @p advance when it is not NULL, 0 on failure. */
static int drawPatch(st_lump_fn lump, void* context, const char* name, int x, int y,
                     uint8_t* surface, int stride, int* advance) {
    const uint8_t* data;
    size_t size;
    int width;
    int column;

    if (!fetch(lump, context, name, &data, &size, &width)) {
        return 0;
    }
    if (advance != NULL) {
        *advance = width;
    }
    x -= le16(data + 4);
    y -= le16(data + 6);

    for (column = 0; column < width; ++column) {
        size_t offset = le32(data + 8 + 4 * column);
        const int screenX = x + column;

        while (offset + 1u < size && data[offset] != 0xFF) {
            const int topdelta = data[offset];
            const int length = data[offset + 1];
            int row;

            if (offset + 3u + (size_t)length > size) {
                return 0;
            }
            for (row = 0; row < length; ++row) {
                const int screenY = y + topdelta + row;
                if (screenX >= 0 && screenX < R_SCREEN_WIDTH && screenY >= 0
                    && screenY < R_SCREEN_HEIGHT) {
                    surface[screenY * stride + screenX] = data[offset + 3 + row];
                }
            }
            offset += (size_t)length + 4u;
        }
    }
    return 1;
}

/* "STCFN" + the three-digit character code. */
static void fontName(int code, char out[9]) {
    out[0] = 'S';
    out[1] = 'T';
    out[2] = 'C';
    out[3] = 'F';
    out[4] = 'N';
    out[5] = (char)('0' + code / 100);
    out[6] = (char)('0' + (code / 10) % 10);
    out[7] = (char)('0' + code % 10);
    out[8] = '\0';
}

/* M_WriteText(): the HU font, a space or an unknown character advancing 4. The
 * font patches are fetched per character, so a missing one is counted. */
static int writeText(st_lump_fn lump, void* context, int x, int y, const char* text,
                     uint8_t* surface, int stride) {
    int missing = 0;
    int cx = x;
    int cy = y;

    for (; *text != '\0'; ++text) {
        int code = (unsigned char)*text;
        char name[9];
        int width = 0;

        if (code == '\n') {
            cx = x;
            cy += 12;
            continue;
        }
        if (code >= 'a' && code <= 'z') {
            code = code - 'a' + 'A';
        }
        if (code < M_FONT_FIRST || code > M_FONT_LAST) {
            cx += 4;
            continue;
        }
        fontName(code, name);
        if (!drawPatch(lump, context, name, cx, cy, surface, stride, &width)) {
            ++missing;
            cx += 4;
            continue;
        }
        if (cx + width > R_SCREEN_WIDTH) {
            break;
        }
        cx += width;
    }
    return missing;
}

/* M_DrawThermo(): left cap, @p width middle pieces, right cap, then the dot. */
static int drawThermo(st_lump_fn lump, void* context, int x, int y, int width, int dot,
                      uint8_t* surface, int stride) {
    int missing = 0;
    int xx = x;
    int index;

    missing += !drawPatch(lump, context, "M_THERML", xx, y, surface, stride, NULL);
    xx += 8;
    for (index = 0; index < width; ++index) {
        missing += !drawPatch(lump, context, "M_THERMM", xx, y, surface, stride, NULL);
        xx += 8;
    }
    missing += !drawPatch(lump, context, "M_THERMR", xx, y, surface, stride, NULL);
    missing += !drawPatch(lump, context, "M_THERMO", (x + 8) + dot * 8, y, surface, stride,
                          NULL);
    return missing;
}

int m_menu_draw(const m_menu_t* menu, st_lump_fn lump, void* context, uint8_t* surface,
                int stride) {
    const m_def_t* def;
    int missing = 0;
    int index;
    int skullX;
    int skullY;

    if (menu == NULL || !menu->active || lump == NULL || surface == NULL) {
        return 0;
    }
    def = &kMenus[menu->screen];

    if (menu->screen == M_SCREEN_MAIN) {
        /* M_Drawer: the title, then each row's picture 16 px apart. */
        missing += !drawPatch(lump, context, "M_DOOM", 94, 2, surface, stride, NULL);
    } else if (menu->screen == M_SCREEN_NEWGAME) {
        /* M_DrawNewGame */
        missing += !drawPatch(lump, context, "M_NEWG", 96, 14, surface, stride, NULL);
        missing += !drawPatch(lump, context, "M_SKILL", 54, 38, surface, stride, NULL);
    } else if (menu->screen == M_SCREEN_OPTIONS) {
        missing += !drawPatch(lump, context, "M_OPTTTL", 108, 15, surface, stride, NULL);
    } else {
        /* M_DrawLoad / M_DrawSave draw M_LOADG / M_SAVEG here in vanilla; this
         * port spells them with the HU font (see the file comment). */
        missing += writeText(lump, context, SLOT_TITLE_X, SLOT_TITLE_Y,
                             menu->screen == M_SCREEN_LOAD ? "LOAD GAME" : "SAVE GAME",
                             surface, stride);
    }

    if (menu->screen == M_SCREEN_OPTIONS) {
        const int y0 = OPTIONS_Y0;
        const int y1 = OPTIONS_Y0 + OPTIONS_STEP;
        const int y2 = OPTIONS_Y0 + 2 * OPTIONS_STEP;
        const int y3 = OPTIONS_Y0 + 3 * OPTIONS_STEP;

        missing += writeText(lump, context, def->x, y0, "SKILL LEVEL", surface, stride);
        missing += writeText(lump, context, def->x, y0 + OPTIONS_VALUE_DY,
                             m_skill_name(menu->settings.skill), surface, stride);
        /* The slider drives the APU volume; the label uses the same words as
         * upstream's M_SFXVOL row. */
        missing += writeText(lump, context, def->x, y1, "SOUND VOLUME", surface, stride);
        missing += drawThermo(lump, context, def->x, y1 + OPTIONS_VALUE_DY, M_VOLUME_MAX + 1,
                              menu->settings.volume, surface, stride);
        missing += writeText(lump, context, def->x, y2, "GAMMA CORRECTION", surface, stride);
        missing += drawThermo(lump, context, def->x, y2 + OPTIONS_VALUE_DY, M_GAMMA_MAX + 1,
                              menu->settings.gamma, surface, stride);
        /* Two positions, so text rather than a two-dot thermometer: the value is
         * the word, the same way the skill row draws its own value. */
        missing += writeText(lump, context, def->x, y3, "SOUND MUTE", surface, stride);
        missing += writeText(lump, context, def->x, y3 + OPTIONS_VALUE_DY,
                             menu->settings.muted ? "ON" : "OFF", surface, stride);
        skullX = def->x + SKULLXOFF;
        skullY = OPTIONS_Y0 - 5 + menu->itemOn[M_SCREEN_OPTIONS] * OPTIONS_STEP;
    } else if (menu->screen == M_SCREEN_LOAD || menu->screen == M_SCREEN_SAVE) {
        /* The six rows are the owner's labels, EMPTY included. A text row is at
         * the same baseline a patch row would have, which is what makes the
         * skull's y agree with upstream's SaveDef/LoadDef. */
        for (index = 0; index < def->rows; ++index) {
            missing += writeText(lump, context, def->x, def->y + index * LINEHEIGHT,
                                 (const char*)menu->slotLabels[index], surface, stride);
        }
        skullX = def->x + SKULLXOFF;
        skullY = def->y - 5 + menu->itemOn[menu->screen] * LINEHEIGHT;
    } else {
        for (index = 0; index < def->rows; ++index) {
            const m_row_t* row = &def->items[index];
            if (row->patch != NULL) {
                missing += !drawPatch(lump, context, row->patch, def->x,
                                      def->y + index * LINEHEIGHT, surface, stride, NULL);
            } else if (row->text != NULL) {
                missing += writeText(lump, context, def->x, def->y + index * LINEHEIGHT,
                                     row->text, surface, stride);
            }
        }
        skullX = def->x + SKULLXOFF;
        skullY = def->y - 5 + menu->itemOn[menu->screen] * LINEHEIGHT;
    }

    /* The skull cursor, last, so it sits over everything. */
    missing += !drawPatch(lump, context, menu->whichSkull ? "M_SKULL2" : "M_SKULL1", skullX,
                          skullY, surface, stride, NULL);
    return missing;
}
