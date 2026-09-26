/*
 * Tables.cpp - see Tables.h.
 *
 * The ten shipped stages share a full 6-ball rack and differ in cushion
 * geometry, pockets and obstacles, in increasing difficulty (like the NES
 * original, where every level plays 6 balls and only the table changes):
 *
 * - Stage 1 ("Classic", NES STAGE01-like): plain rectangle.
 * - Stage 2 ("Bites", NES STAGE08/10-like): rectangular bites cut into both
 *   side cushions.
 * - Stage 3 ("Zigzag", NES STAGE04-like): angled teeth on both side
 *   cushions.
 * - Stage 4 ("Donut", NES STAGE09-like): rectangle with a central octagon
 *   island; the rack splits around it.
 * - Stage 5 ("Gate", NES STAGE15-like): rectangle with a center bar between
 *   cue and rack.
 * - Stage 6 ("Chevron", NES STAGE13-like): right cushion folded into an
 *   inward V.
 * - Stage 7 ("Fortress", NES STAGE03-like): bites on all four top/bottom
 *   runs.
 * - Stage 8 ("Twins", NES STAGE25-like): rectangle with two square islands.
 * - Stage 9 ("Octagon", NES STAGE06-like): chamfered corners, only 4
 *   pockets (top/bottom sides plus mid-left/mid-right).
 * - Stage 10 ("Corridor", NES STAGE20-like): narrow rectangle, a single
 *   ball column with almost no banking angles.
 *
 * Stages 2, 3, 6 and 7 reuse the stage-1 pocket notches verbatim and only
 * reshape the straight runs between them; stages 4, 5 and 8 reuse the whole
 * stage-1 border. Every border below is wound clockwise on screen (positive
 * shoelace) and every obstacle counter-clockwise (negative), the windings
 * loadTable() requires.
 */
#include "pool/Tables.h"

namespace pool {
namespace {

// --- Shared pocket data ------------------------------------------------------

constexpr PocketDef kSharedPockets[6] = {
    {{17, 57}, 0, 3},      // top-left
    {{120, 52}, 4, 7},     // top-side
    {{223, 57}, 8, 11},    // top-right
    {{223, 223}, 12, 15},  // bottom-right
    {{120, 228}, 16, 19},  // bottom-side
    {{17, 223}, 20, 23},   // bottom-left
};

// --- Stage 1 & 4 border: plain rectangle with bag notches --------------------

constexpr PointPx kRectBorder[24] = {
    // Top-left corner: enters from the left edge, exits to the top edge.
    {16, 66}, {10, 60}, {20, 50}, {26, 56},
    // Top-side pocket.
    {113, 56}, {113, 48}, {127, 48}, {127, 56},
    // Top-right corner: enters from the top edge, exits to the right edge.
    {214, 56}, {220, 50}, {230, 60}, {224, 66},
    // Right edge.
    // Bottom-right corner: enters from the right edge, exits to the bottom edge.
    {224, 214}, {230, 220}, {220, 230}, {214, 224},
    // Bottom-side pocket.
    {127, 224}, {127, 232}, {113, 232}, {113, 224},
    // Bottom-left corner: enters from the bottom edge, exits to the left edge.
    {26, 224}, {20, 230}, {10, 220}, {16, 214},
};
constexpr Polyline kRectBorderLine{kRectBorder, 24};

constexpr TargetDef kTable1Targets[6] = {
    {1, {150, 140}}, {2, {158, 135}}, {3, {158, 145}},
    {4, {166, 130}}, {5, {166, 140}}, {6, {166, 150}},
};

constexpr TableDef kTable1{
    kRectBorderLine, nullptr, 0, kSharedPockets, 6, {70, 140}, kTable1Targets, 6,
};

// --- Stage 2 border: side bites ----------------------------------------------
//
// The left run (16,214)->(16,66) detours in to x=44 over y 120..160 and the
// right run mirrors it, leaving the pocket notches untouched.

constexpr PointPx kTable2Border[32] = {
    {16, 66}, {10, 60}, {20, 50}, {26, 56},
    {113, 56}, {113, 48}, {127, 48}, {127, 56},
    {214, 56}, {220, 50}, {230, 60}, {224, 66},
    // Right bite: down, in, down, out, down.
    {224, 120}, {196, 120}, {196, 160}, {224, 160}, {224, 214},
    {230, 220}, {220, 230}, {214, 224},
    {127, 224}, {127, 232}, {113, 232}, {113, 224},
    {26, 224}, {20, 230}, {10, 220}, {16, 214},
    // Left bite: up, in, up, out, up.
    {16, 160}, {44, 160}, {44, 120}, {16, 120},
};
constexpr Polyline kTable2BorderLine{kTable2Border, 32};

// Same notch indices as the rectangle: TL(0,3) TS(4,7) TR(8,11) BR(16,19)
// BS(20,23) BL(24,27).
constexpr PocketDef kTable2Pockets[6] = {
    {{17, 57}, 0, 3},      // top-left
    {{120, 52}, 4, 7},     // top-side
    {{223, 57}, 8, 11},    // top-right
    {{223, 223}, 16, 19},  // bottom-right
    {{120, 228}, 20, 23},  // bottom-side
    {{17, 223}, 24, 27},   // bottom-left
};

constexpr TargetDef kTable2Targets[6] = {
    {1, {150, 120}}, {2, {150, 140}}, {3, {158, 130}},
    {4, {158, 150}}, {5, {166, 112}}, {6, {166, 138}},
};

constexpr TableDef kTable2{
    kTable2BorderLine, nullptr, 0, kTable2Pockets, 6, {60, 140}, kTable2Targets, 6,
};

// --- Stage 3 border: angled teeth --------------------------------------------
//
// Both side runs zigzag with tips at x=44/x=196, so straight shots up either
// side come back at an angle. Notches untouched, same indices as stage 2.

constexpr PointPx kTable3Border[32] = {
    {16, 66}, {10, 60}, {20, 50}, {26, 56},
    {113, 56}, {113, 48}, {127, 48}, {127, 56},
    {214, 56}, {220, 50}, {230, 60}, {224, 66},
    // Right teeth, traced top to bottom.
    {224, 126}, {196, 148}, {224, 170}, {196, 192}, {224, 214},
    {230, 220}, {220, 230}, {214, 224},
    {127, 224}, {127, 232}, {113, 232}, {113, 224},
    {26, 224}, {20, 230}, {10, 220}, {16, 214},
    // Left teeth, traced bottom to top.
    {44, 192}, {16, 170}, {44, 148}, {16, 126},
};
constexpr Polyline kTable3BorderLine{kTable3Border, 32};

constexpr TargetDef kTable3Targets[6] = {
    {1, {140, 140}}, {2, {148, 132}}, {3, {148, 148}},
    {4, {156, 124}}, {5, {156, 156}}, {6, {164, 140}},
};

constexpr TableDef kTable3{
    kTable3BorderLine, nullptr, 0, kTable2Pockets, 6, {70, 110}, kTable3Targets, 6,
};

// --- Stage 4: central island -------------------------------------------------
//
// Plain rectangle plus one octagon block mid-table. The rack splits around
// it: two targets left, four right.

constexpr PointPx kTable4IslandPoints[8] = {
    {95, 130}, {95, 150}, {105, 160}, {135, 160}, {145, 150}, {145, 130}, {135, 120}, {105, 120},
};
constexpr Polyline kTable4Obstacles[1] = {Polyline{kTable4IslandPoints, 8}};

constexpr TargetDef kTable4Targets[6] = {
    {1, {60, 170}}, {2, {80, 120}}, {3, {160, 110}},
    {4, {170, 140}}, {5, {160, 170}}, {6, {185, 150}},
};

constexpr TableDef kTable4{
    kRectBorderLine, kTable4Obstacles, 1, kSharedPockets, 6, {60, 100}, kTable4Targets, 6,
};

// --- Stage 5: center bar -----------------------------------------------------
//
// Plain rectangle with a 40x8 bar mid-table between the cue (left) and the
// rack (right). Same counter-clockwise winding as every other obstacle.

constexpr PointPx kTable5BarPoints[4] = {{100, 136}, {100, 144}, {140, 144}, {140, 136}};
constexpr Polyline kTable5Obstacles[1] = {Polyline{kTable5BarPoints, 4}};

constexpr TargetDef kTable5Targets[6] = {
    {1, {150, 120}}, {2, {150, 155}}, {3, {158, 130}},
    {4, {166, 115}}, {5, {166, 135}}, {6, {166, 155}},
};

constexpr TableDef kTable5{
    kRectBorderLine, kTable5Obstacles, 1, kSharedPockets, 6, {60, 140}, kTable5Targets, 6,
};

// --- Stage 6: chevron --------------------------------------------------------
//
// The right run folds into an inward V tipped at (188, 140); every other
// notch is untouched, so only the bottom-right/bottom-side/bottom-left mouth
// indices shift.

constexpr PointPx kTable6Border[27] = {
    {16, 66}, {10, 60}, {20, 50}, {26, 56},
    {113, 56}, {113, 48}, {127, 48}, {127, 56},
    {214, 56}, {220, 50}, {230, 60}, {224, 66},
    // Right chevron: down, in to the tip, out, down.
    {224, 110}, {188, 140}, {224, 170}, {224, 214},
    {230, 220}, {220, 230}, {214, 224},
    {127, 224}, {127, 232}, {113, 232}, {113, 224},
    {26, 224}, {20, 230}, {10, 220}, {16, 214},
};
constexpr Polyline kTable6BorderLine{kTable6Border, 27};

constexpr PocketDef kTable6Pockets[6] = {
    {{17, 57}, 0, 3},      // top-left
    {{120, 52}, 4, 7},     // top-side
    {{223, 57}, 8, 11},    // top-right
    {{223, 223}, 15, 18},  // bottom-right
    {{120, 228}, 19, 22},  // bottom-side
    {{17, 223}, 23, 26},   // bottom-left
};

constexpr TargetDef kTable6Targets[6] = {
    {1, {120, 110}}, {2, {130, 125}}, {3, {130, 155}},
    {4, {140, 140}}, {5, {150, 115}}, {6, {150, 165}},
};

constexpr TableDef kTable6{
    kTable6BorderLine, nullptr, 0, kTable6Pockets, 6, {60, 140}, kTable6Targets, 6,
};

// --- Stage 7: fortress -------------------------------------------------------
//
// Downward bites on both top runs and upward bites on both bottom runs; the
// side runs stay straight. 40 segments, the most cushions of any stage.

constexpr PointPx kTable7Border[40] = {
    {16, 66}, {10, 60}, {20, 50}, {26, 56},
    // Top-left bite: right, down, right, up.
    {50, 56}, {50, 72}, {89, 72}, {89, 56},
    {113, 56}, {113, 48}, {127, 48}, {127, 56},
    // Top-right bite.
    {151, 56}, {151, 72}, {190, 72}, {190, 56},
    {214, 56}, {220, 50}, {230, 60}, {224, 66},
    {224, 214}, {230, 220}, {220, 230}, {214, 224},
    // Bottom-right bite: left, up, left, down.
    {190, 224}, {190, 208}, {151, 208}, {151, 224},
    {127, 224}, {127, 232}, {113, 232}, {113, 224},
    // Bottom-left bite.
    {89, 224}, {89, 208}, {50, 208}, {50, 224},
    {26, 224}, {20, 230}, {10, 220}, {16, 214},
};
constexpr Polyline kTable7BorderLine{kTable7Border, 40};

constexpr PocketDef kTable7Pockets[6] = {
    {{17, 57}, 0, 3},      // top-left
    {{120, 52}, 8, 11},    // top-side
    {{223, 57}, 16, 19},   // top-right
    {{223, 223}, 20, 23},  // bottom-right
    {{120, 228}, 28, 31},  // bottom-side
    {{17, 223}, 36, 39},   // bottom-left
};

constexpr TargetDef kTable7Targets[6] = {
    {1, {110, 130}}, {2, {120, 145}}, {3, {120, 165}},
    {4, {135, 135}}, {5, {135, 160}}, {6, {150, 145}},
};

constexpr TableDef kTable7{
    kTable7BorderLine, nullptr, 0, kTable7Pockets, 6, {60, 140}, kTable7Targets, 6,
};

// --- Stage 8: twin islands ---------------------------------------------------

constexpr PointPx kTable8IslandAPoints[4] = {{95, 105}, {95, 125}, {115, 125}, {115, 105}};
constexpr PointPx kTable8IslandBPoints[4] = {{125, 155}, {125, 175}, {145, 175}, {145, 155}};
constexpr Polyline kTable8Obstacles[2] = {
    Polyline{kTable8IslandAPoints, 4},
    Polyline{kTable8IslandBPoints, 4},
};

constexpr TargetDef kTable8Targets[6] = {
    {1, {60, 160}}, {2, {80, 130}}, {3, {160, 110}},
    {4, {170, 140}}, {5, {160, 170}}, {6, {100, 190}},
};

constexpr TableDef kTable8{
    kRectBorderLine, kTable8Obstacles, 2, kSharedPockets, 6, {60, 100}, kTable8Targets, 6,
};

// --- Stage 9: octagon, 4 pockets ---------------------------------------------
//
// Chamfered corners with no pockets on them; the only holes are mid-left,
// mid-right (rectangular bumps out of the side runs) and the top/bottom
// sides. Fewer pockets plus angled cushions make this the second-hardest.

constexpr PointPx kTable9Border[24] = {
    {40, 56},
    {113, 56}, {113, 48}, {127, 48}, {127, 56},
    {200, 56}, {224, 80}, {224, 120},
    // Right pocket bump: out, down, back in.
    {232, 120}, {232, 160}, {224, 160},
    {224, 200}, {200, 224},
    {127, 224}, {127, 232}, {113, 232}, {113, 224},
    {40, 224}, {16, 200}, {16, 160},
    // Left pocket bump: out, up, back in.
    {8, 160}, {8, 120}, {16, 120},
    {16, 80},
};
constexpr Polyline kTable9BorderLine{kTable9Border, 24};

constexpr PocketDef kTable9Pockets[4] = {
    {{120, 52}, 1, 4},    // top-side
    {{228, 140}, 7, 10},  // mid-right
    {{120, 228}, 13, 16},  // bottom-side
    {{12, 140}, 19, 22},  // mid-left
};

constexpr TargetDef kTable9Targets[6] = {
    {1, {110, 140}}, {2, {120, 132}}, {3, {120, 148}},
    {4, {130, 132}}, {5, {130, 148}}, {6, {140, 140}},
};

constexpr TableDef kTable9{
    kTable9BorderLine, nullptr, 0, kTable9Pockets, 4, {70, 100}, kTable9Targets, 6,
};

// --- Stage 10: corridor ------------------------------------------------------
//
// Narrow rectangle (x 88..152) with the same notch pattern as the classic,
// so the mouth indices match; the rack is a single column with almost no
// banking angles available.

constexpr PointPx kTable10Border[24] = {
    {88, 66}, {82, 60}, {92, 50}, {98, 56},
    {113, 56}, {113, 48}, {127, 48}, {127, 56},
    {142, 56}, {148, 50}, {158, 60}, {152, 66},
    {152, 214}, {158, 220}, {148, 230}, {142, 224},
    {127, 224}, {127, 232}, {113, 232}, {113, 224},
    {98, 224}, {92, 230}, {82, 220}, {88, 214},
};
constexpr Polyline kTable10BorderLine{kTable10Border, 24};

constexpr PocketDef kTable10Pockets[6] = {
    {{89, 57}, 0, 3},     // top-left
    {{120, 52}, 4, 7},    // top-side
    {{151, 57}, 8, 11},   // top-right
    {{151, 223}, 12, 15},  // bottom-right
    {{120, 228}, 16, 19},  // bottom-side
    {{89, 223}, 20, 23},  // bottom-left
};

constexpr TargetDef kTable10Targets[6] = {
    {1, {120, 100}}, {2, {120, 114}}, {3, {120, 128}},
    {4, {120, 142}}, {5, {120, 156}}, {6, {120, 170}},
};

constexpr TableDef kTable10{
    kTable10BorderLine, nullptr, 0, kTable10Pockets, 6, {120, 200}, kTable10Targets, 6,
};

constexpr const TableDef* kStages[kStageCount] = {
    &kTable1, &kTable2, &kTable3, &kTable4, &kTable5,
    &kTable6, &kTable7, &kTable8, &kTable9, &kTable10,
};

}  // namespace

const TableDef& tableForStage(uint8_t stage) {
    if (stage < 1) {
        return kTable1;
    }
    if (stage > kStageCount) {
        return kTable10;
    }
    return *kStages[stage - 1];
}

}  // namespace pool
