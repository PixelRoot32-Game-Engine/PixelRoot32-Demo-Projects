/*
 * The thinker list: add, mark for removal, and run one tic.
 *
 * Upstream's P_AddThinker/P_RemoveThinker/P_RunThinkers (p_tick.c) are the
 * reference; the differences are the three the header documents -- head/tail
 * instead of a sentinel ring, a `removed` byte instead of a function-pointer
 * sentinel, and a next pointer captured before the call instead of read after a
 * free. The order and the deferred-removal semantics are upstream's.
 *
 * Every entry point validates before it writes, so a misuse (a null list, a
 * node from another list, a node already linked) reports a code and leaves the
 * list's four fields exactly as they were.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#include "p_tick.h"

/* Unlinks @p thinker from @p list without releasing it. The caller has already
 * proved that the node belongs to this list. */
static void unlink_node(p_thinker_list_t* list, p_thinker_t* thinker) {
    if (thinker->prev != NULL) {
        thinker->prev->next = thinker->next;
    } else {
        list->head = thinker->next;
    }
    if (thinker->next != NULL) {
        thinker->next->prev = thinker->prev;
    } else {
        list->tail = thinker->prev;
    }
    thinker->next = NULL;
    thinker->prev = NULL;
    thinker->owner = NULL;
    if (list->count > 0u) {
        list->count -= 1u;
    }
}

p_tick_error_t p_thinker_list_init(p_thinker_list_t* list) {
    if (list == NULL) {
        return P_TICK_ERR_NULL_LIST;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0u;
    list->peak = 0u;
    return P_TICK_OK;
}

p_tick_error_t p_thinker_init(p_thinker_t* thinker, p_thinker_fn function,
                              p_thinker_kind_t kind) {
    if (thinker == NULL) {
        return P_TICK_ERR_NULL_THINKER;
    }
    if (kind != P_THINKER_MOBJ && kind != P_THINKER_CEILING &&
        kind != P_THINKER_FLOOR && kind != P_THINKER_PLAT &&
        kind != P_THINKER_DOOR && kind != P_THINKER_LIGHTFLASH) {
        return P_TICK_ERR_BAD_KIND;
    }
    thinker->next = NULL;
    thinker->prev = NULL;
    thinker->function = function;
    thinker->owner = NULL;
    thinker->kind = (int16_t)kind;
    thinker->removed = 0u;
    return P_TICK_OK;
}

p_tick_error_t p_thinker_add(p_thinker_list_t* list, p_thinker_t* thinker) {
    if (list == NULL) {
        return P_TICK_ERR_NULL_LIST;
    }
    if (thinker == NULL) {
        return P_TICK_ERR_NULL_THINKER;
    }
    if (thinker->owner != NULL) {
        return P_TICK_ERR_ALREADY_LINKED;
    }

    thinker->next = NULL;
    thinker->prev = list->tail;
    thinker->owner = list;
    if (list->tail != NULL) {
        list->tail->next = thinker;
    } else {
        list->head = thinker;
    }
    list->tail = thinker;
    list->count += 1u;
    if (list->count > list->peak) {
        list->peak = list->count;
    }
    return P_TICK_OK;
}

p_tick_error_t p_thinker_remove(p_thinker_list_t* list, p_thinker_t* thinker) {
    if (list == NULL) {
        return P_TICK_ERR_NULL_LIST;
    }
    if (thinker == NULL) {
        return P_TICK_ERR_NULL_THINKER;
    }
    if (thinker->owner != list) {
        return P_TICK_ERR_NOT_LINKED;
    }
    /* The mark is the removal: the walk unlinks and releases it. */
    thinker->removed = 1u;
    return P_TICK_OK;
}

p_tick_error_t p_run_thinkers(p_thinker_list_t* list, p_thinker_release_fn release,
                              void* context, p_thinker_run_t* stats) {
    p_thinker_t* current;

    if (stats != NULL) {
        stats->executed = 0u;
        stats->removed = 0u;
        stats->inert = 0u;
    }
    if (list == NULL) {
        return P_TICK_ERR_NULL_LIST;
    }
    if (release == NULL) {
        return P_TICK_ERR_NULL_RELEASE;
    }

    current = list->head;
    while (current != NULL) {
        /* Captured before any call: a thinker may remove the node it runs on,
         * and a released node must never be read again. */
        p_thinker_t* next = current->next;

        if (current->removed) {
            unlink_node(list, current);
            release(context, current);
            if (stats != NULL) {
                stats->removed += 1u;
            }
        } else if (current->function != NULL) {
            current->function(current, context);
            if (stats != NULL) {
                stats->executed += 1u;
            }
            /* It may have marked itself; the unlink happens here, after the
             * call, so the thinker never runs on a released node. */
            if (current->removed) {
                unlink_node(list, current);
                release(context, current);
                if (stats != NULL) {
                    stats->removed += 1u;
                }
            }
        } else if (stats != NULL) {
            stats->inert += 1u;
        }
        current = next;
    }
    return P_TICK_OK;
}

const p_thinker_t* p_thinker_first(const p_thinker_list_t* list) {
    if (list == NULL) {
        return NULL;
    }
    return list->head;
}

const p_thinker_t* p_thinker_next(const p_thinker_t* thinker) {
    if (thinker == NULL) {
        return NULL;
    }
    return thinker->next;
}
