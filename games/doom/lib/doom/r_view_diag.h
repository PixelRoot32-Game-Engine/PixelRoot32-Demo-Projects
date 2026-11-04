/*
 * The frame's per-record state, as console lines (r_view_diag).
 *
 * Part of the PixelRoot32 port of DOOM. See NOTICE and LICENSE in this
 * directory for the provenance and the terms.
 */

/*
 * WHY THIS EXISTS
 *
 * A board-only tear that the host cannot reproduce is a disagreement about the
 * *inputs* of one drawing pass -- the drawsegs the wall pass stored, the clip
 * codes the masked pass reads back, the visplanes the walk allocated, the
 * sprites the projector accepted. A photograph only says "wrong"; these lines
 * say *which record and which field*. They print nothing the renderer does not
 * already hold, they format no float, and they allocate nothing, so a build
 * that calls them renders the same pixels it would have.
 *
 * The format is deliberately machine-shaped: every line starts `VD,`, carries a
 * record tag, then positional fields separated by commas. `tools/
 * viewdiag_compare.py` parses it, and `rsegs_check --viewdiag` prints the same
 * lines for the host's render of the same viewpoint, so a board capture is one
 * command away from a field-by-field diff.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point. The
 * writer below is bounded by construction -- it stops appending rather than
 * writing past @c capacity -- because a console line the board cannot hold
 * would be the diagnostic's own defect.
 */
#ifndef DOOM_R_VIEW_DIAG_H
#define DOOM_R_VIEW_DIAG_H

#include <stddef.h>

#include "r_bsp.h"
#include "r_defs.h"
#include "r_plane.h"
#include "r_things.h"

#ifdef __cplusplus
extern "C" {
#endif

/// One line's ceiling, including the NUL. FrameDriver's console writer holds
/// 128 bytes, so a line that fits here fits there.
#define R_VIEW_DIAG_LINE 128

/*
 * Every function writes at most @c capacity bytes including the NUL and returns
 * the length written excluding the NUL, or 0 when an argument is unusable
 * (no state, no buffer, capacity 0, index out of range) -- 0 is not a valid
 * line length, so the caller can tell "nothing to print" from "printed".
 *
 * The lines:
 *
 *   VD,cfg,<caller viewh>,<caller centery>,<lib viewh>,<lib centery>,
 *          <screen width>,<drawseg cap>,<plane cap>,<clip pool cap>,
 *          <vis sprite cap>
 *       The first pair is what the CALLER compiled R_VIEW_HEIGHT/R_CENTER_Y to,
 *       the second what lib/doom's own translation unit did. The two disagree
 *       exactly when the macro leaked into one build of the library and not
 *       the other, which draws a view into a window it did not size.
 *
 *   VD,pool,<need>,<used>,<drawsegs>,<planes>
 *       state->clipPoolEntries / clipPoolUsed and the two live counts.
 *
 *   VD,d,<i>,<seg>,<mid>,<x1>,<x2>,<s1>,<s2>,<step>,<mc>,<ct>,<cb>,<sil>,<ln>
 *       One drawseg. `mid` is the masked mid texture's name, or `-` when the
 *       drawseg stored no masked run (mc == R_DRAWSEG_NO_BLOCK). `s1`, `s2`,
 *       `step` are the three fixed-point scales in 8 hex digits. `mc`, `ct`,
 *       `cb` are maskedTextureCol, sprTopClip and sprBottomClip; `ct`/`cb` are
 *       R_DRAWSEG_CLIP_TOP/BOTTOM constants or a clipPool offset.
 *
 *   VD,p,<i>,<height>,<flat>,<light>,<minx>,<maxx>,<spans>,<tmin>,<tmax>,
 *          <bmin>,<bmax>
 *       One visplane. `height` is 8 hex digits, `flat` the picture's name,
 *       `spans` the columns whose top is not R_PLANE_EMPTY, and the four
 *       extrema the smallest/largest top and bottom over those columns.
 *
 *   VD,s,<i>,<x>,<y>,<z>,<angle>,<light>,<lump>
 *       One SUBMITTED sprite (the caller's own list, before projection), with
 *       the four fixed-point fields in 8 hex digits. A submission whose x and y
 *       equal the view's is the camera itself; that row is what a host fixture
 *       generated from THINGS can never contain, so it is the first line to
 *       look at when the two captures disagree by a full-screen smear.
 */
int r_view_diag_config(const r_view_storage_t* storage, int callerViewHeight,
                       int callerCenterY, char* out, size_t capacity);

int r_view_diag_pool(const r_view_state_t* state, char* out, size_t capacity);

int r_view_diag_drawseg(const r_view_state_t* state, unsigned index, char* out,
                        size_t capacity);

int r_view_diag_plane(const r_view_state_t* state, unsigned index, char* out,
                      size_t capacity);

int r_view_diag_sprite(const r_sprite_t* sprites, unsigned count, unsigned index,
                       char* out, size_t capacity);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_R_VIEW_DIAG_H */
