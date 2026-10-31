/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * The thinker list: DOOM's one ordered list of everything that ticks.
 *
 * Upstream keeps a circular list of `thinker_t` nodes rooted at a file-scope
 * `thinkercap`, and every simulated object -- a mobj, a moving ceiling, a lift,
 * a door, a light flash -- starts with a `thinker_t` and is reached only through
 * that list. P_RunThinkers() walks it once per tic in insertion order, and
 * P_RemoveThinker() does not remove anything: it writes a sentinel into the
 * node's function pointer and the next walk unlinks and frees the node when it
 * reaches it ("deallocation is lazy", p_tick.c).
 *
 * This layer is that list and nothing else. It has no opinion about what a
 * thinker is: `function` is a caller-supplied entry point and `kind` is a tag
 * the caller may use. p_mobj.c supplies both for mobjs; D4.1b supplies the
 * sector thinkers (doors, lifts, floors, ceilings, light flashes); the list
 * does not change for either.
 *
 * THREE DELIBERATE DIFFERENCES FROM UPSTREAM
 *
 * 1. Head and tail, not a ring with a sentinel. A sentinel is a thinker-shaped
 *    object that must be allocated somewhere and owned by somebody; with a
 *    caller-owned state struct it would have to live in the list itself, and a
 *    mis-set pointer to it would be indirection through the list's own address.
 *    `head`/`tail` with NULL terminators has the same insertion order and one
 *    fewer level of indirection.
 * 2. Removal is a flag, not `(actionf_v)(-1)`. The sentinel is what upstream
 *    writes into a function pointer; a `removed` byte says the same thing
 *    without a cast through an incompatible function pointer type, and it is
 *    the field the walk tests.
 * 3. The walk never reads a freed node. Upstream does `current =
 *    current->next` *after* `Z_Free(current)` (p_tick.c), which happens to work
 *    because the zone does not clear the block; here the next pointer is
 *    captured before the call and the node is released after it, so the walk is
 *    correct even if the arena is reused immediately.
 *
 * Deferred removal is what makes a thinker that removes itself safe: it marks
 * its own node, returns, and the walk finishes the unlink. A thinker removed by
 * an earlier one is still reached but is not run -- it is unlinked and released
 * when its turn comes, exactly as upstream's sentinel branch does.
 *
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals, no
 * stdio, no floating point. The caller owns the list and every node.
 */
#ifndef DOOM_P_TICK_H
#define DOOM_P_TICK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct p_thinker p_thinker_t;
typedef struct p_thinker_list p_thinker_list_t;

/**
 * @brief One thinker's entry point.
 *
 * @param thinker The node being run. A thinker that wants its own record casts
 *                it back; every implementation puts p_thinker_t first.
 * @param context The same pointer the caller passed to p_run_thinkers(), so a
 *                thinker can reach the world it belongs to without a global.
 */
typedef void (*p_thinker_fn)(p_thinker_t* thinker, void* context);

/**
 * @brief Called once for each node the walk unlinks, to return its memory.
 *
 * The list does not free anything itself: on the board the nodes come from the
 * level's zone, and a list that called z_free() would tie this layer to that
 * allocator. The callback receives the same @p context the walk was given.
 */
typedef void (*p_thinker_release_fn)(void* context, p_thinker_t* thinker);

/*
 * What a node is. Only P_THINKER_MOBJ exists in D4.1a; the rest are the sector
 * thinkers D4.1b adds, declared now so the tag never has to be renumbered and
 * so a debugger can name the value.
 */
typedef enum p_thinker_kind {
    P_THINKER_MOBJ = 1,
    P_THINKER_CEILING = 2,    /* reserved for D4.1b */
    P_THINKER_FLOOR = 3,      /* reserved for D4.1b */
    P_THINKER_PLAT = 4,       /* reserved for D4.1b */
    P_THINKER_DOOR = 5,       /* reserved for D4.1b */
    P_THINKER_LIGHTFLASH = 6  /* reserved for D4.1b */
} p_thinker_kind_t;

/*
 * One node. It is meant to be the first member of a larger record (r_mobj_t
 * is), which is why the function receives the node rather than a separate
 * payload pointer: the node's address is the record's address.
 *
 * `owner` is the list the node is in, or NULL. It is what makes misuse
 * detectable: adding a linked node, removing a node from a list it is not in,
 * and running two lists over the same node are all refused before a pointer is
 * written, so a caller bug cannot corrupt a list.
 */
struct p_thinker {
    p_thinker_t* next;
    p_thinker_t* prev;
    p_thinker_fn function; /* what to run; NULL is an inert node */
    p_thinker_list_t* owner;
    int16_t kind;          /* p_thinker_kind_t */
    uint8_t removed;       /* marked for removal; unlinked by the next walk */
};

/*
 * The list. `count` is live nodes (marked ones included, until the walk
 * unlinks them); `peak` is the highest count ever reached, which is what a
 * capacity decision on the board is made from.
 */
struct p_thinker_list {
    p_thinker_t* head;
    p_thinker_t* tail;
    uint32_t count;
    uint32_t peak;
};

/*
 * One code per failure class, never a shared "list error" catch-all:
 *
 *   P_TICK_OK              the call did what it says
 *   P_TICK_ERR_NULL_LIST   no list was passed (or the level is not open)
 *   P_TICK_ERR_NULL_THINKER no node was passed
 *   P_TICK_ERR_ALREADY_LINKED the node is already in a list
 *   P_TICK_ERR_NOT_LINKED  the node is not in this list
 *   P_TICK_ERR_BAD_KIND    the kind is not one of p_thinker_kind_t
 *   P_TICK_ERR_NULL_RELEASE the walk was given no release callback
 *   P_TICK_ERR_NOT_OPEN    p_level_tick() was called on a closed level
 *
 * A refused call changes no pointer, no count and no flag.
 */
typedef enum p_tick_error {
    P_TICK_OK = 0,
    P_TICK_ERR_NULL_LIST = 1,
    P_TICK_ERR_NULL_THINKER = 2,
    P_TICK_ERR_ALREADY_LINKED = 3,
    P_TICK_ERR_NOT_LINKED = 4,
    P_TICK_ERR_BAD_KIND = 5,
    P_TICK_ERR_NULL_RELEASE = 6,
    P_TICK_ERR_NOT_OPEN = 7
} p_tick_error_t;

/*
 * What one walk did. `executed` counts nodes whose function ran, `removed`
 * counts nodes unlinked and released (whether they removed themselves or were
 * marked before their turn), and `inert` counts reached nodes with no function.
 * The three are disjoint and add up to the nodes the walk visited.
 */
typedef struct p_thinker_run {
    uint32_t executed;
    uint32_t removed;
    uint32_t inert;
} p_thinker_run_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Empties a list.
 *
 * Only the list's own four fields are written; the nodes are the caller's and
 * are not touched. `peak` starts at 0 with the count.
 *
 * @return P_TICK_OK, or P_TICK_ERR_NULL_LIST.
 */
p_tick_error_t p_thinker_list_init(p_thinker_list_t* list);

/**
 * @brief Prepares a node that is not in any list.
 *
 * Writes next/prev to NULL, owner to NULL, removed to 0, and copies the
 * function and the kind. A kind outside p_thinker_kind_t is refused and nothing
 * is written. A NULL function is accepted: upstream skips a node whose function
 * pointer is null, and so does this walk.
 *
 * @return P_TICK_OK, P_TICK_ERR_NULL_THINKER or P_TICK_ERR_BAD_KIND.
 */
p_tick_error_t p_thinker_init(p_thinker_t* thinker, p_thinker_fn function,
                              p_thinker_kind_t kind);

/**
 * @brief Appends @p thinker at the tail of @p list, as P_AddThinker() does.
 *
 * Validation order, and the code each step reports:
 *
 *   1. list == NULL                             P_TICK_ERR_NULL_LIST
 *   2. thinker == NULL                          P_TICK_ERR_NULL_THINKER
 *   3. thinker->owner != NULL                   P_TICK_ERR_ALREADY_LINKED
 *
 * On success the node is linked, `owner` is the list, `count` is one higher and
 * `peak` is raised if this is a new high. A marked-but-not-yet-unlinked node
 * cannot be re-added: it still has an owner.
 *
 * @return P_TICK_OK or one of the codes above.
 */
p_tick_error_t p_thinker_add(p_thinker_list_t* list, p_thinker_t* thinker);

/**
 * @brief Marks @p thinker for removal; unlinks nothing yet.
 *
 * This is P_RemoveThinker(): the node stays in the list, in order, until a walk
 * reaches it, which is what makes a thinker that removes itself safe -- it
 * returns into a walk that still holds its address, and the walk does the
 * unlink.
 *
 * Validation order:
 *
 *   1. list == NULL                             P_TICK_ERR_NULL_LIST
 *   2. thinker == NULL                          P_TICK_ERR_NULL_THINKER
 *   3. thinker->owner != list                   P_TICK_ERR_NOT_LINKED
 *
 * A node already marked is *not* an error here: removal is idempotent at this
 * level, because a thinker may be marked by its own function and by a
 * neighbouring one in the same tic. A caller that must report a double removal
 * (p_remove_mobj() does) tests the flag before calling.
 *
 * @return P_TICK_OK or one of the codes above.
 */
p_tick_error_t p_thinker_remove(p_thinker_list_t* list, p_thinker_t* thinker);

/**
 * @brief Runs one tic: visits every node once, in insertion order.
 *
 * The walk, exactly:
 *
 *   node = head
 *   while node != NULL:
 *       next = node->next                      (captured before any call)
 *       if node->removed: unlink, release, removed++
 *       else if node->function != NULL: call it, executed++
 *            then, if it marked itself: unlink, release, removed++
 *       else: inert++
 *       node = next
 *
 * A node that another node removed earlier in the same tic is unlinked here
 * without running, which is upstream's sentinel branch. A node that removes a
 * node *later* in the list only marks it; the walk still reaches it and unlinks
 * it. A function may therefore remove the node it is running on, or any node
 * after it, and the walk stays valid.
 *
 * @param list    The list to walk.
 * @param release Called for every unlinked node; may not be NULL, because a
 *                NULL here would leak every removed node.
 * @param context Passed unchanged to every function and to @p release.
 * @param stats   Optional out: reset to zero, then filled with this walk's
 *                counts. Untouched when NULL.
 * @return P_TICK_OK, P_TICK_ERR_NULL_LIST or P_TICK_ERR_NULL_RELEASE.
 */
p_tick_error_t p_run_thinkers(p_thinker_list_t* list, p_thinker_release_fn release,
                              void* context, p_thinker_run_t* stats);

/**
 * @brief The first node, or NULL. @return NULL for a NULL or empty list.
 */
const p_thinker_t* p_thinker_first(const p_thinker_list_t* list);

/**
 * @brief The next node, or NULL at the tail.
 *
 * A NULL argument answers NULL. The pointer is not checked against any list:
 * it is a cursor, and the caller is expected to be walking a list it has not
 * modified.
 */
const p_thinker_t* p_thinker_next(const p_thinker_t* thinker);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_TICK_H */
