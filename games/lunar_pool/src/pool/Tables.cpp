/*
 * Tables.cpp - see Tables.h.
 *
 * The four shipped stages share the six pocket centers but differ in cushion
 * geometry, obstacles and rack size, in increasing difficulty:
 *
 * - Stage 1 ("Classic", NES STAGE01-like): plain rectangle, 3 targets.
 * - Stage 2 ("Bites", NES STAGE08/10-like): rectangular bites cut into both
 *   side cushions, 4 targets.
 * - Stage 3 ("Zigzag", NES STAGE04-like): angled teeth on both side
 *   cushions, 5 targets.
 * - Stage 4 ("Donut", NES STAGE09-like): rectangle with a central octagon
 *   island, 6 targets.
 *
 * Stages 2 and 3 reuse the stage-1 pocket notches verbatim and only reshape
 * the straight side runs between them, so their mouth indices match; stage 4
 * reuses the whole stage-1 border. Every border below is wound clockwise on
 * screen (positive shoelace) and every obstacle counter-clockwise
 * (negative), the windings loadTable() requires.
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

constexpr TargetDef kTable1Targets[3] = {
    {1, {150, 140}}, {2, {160, 132}}, {3, {160, 148}},
};

constexpr TableDef kTable1{
    kRectBorderLine, nullptr, 0, kSharedPockets, 6, {70, 140}, kTable1Targets, 3,
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

constexpr TargetDef kTable2Targets[4] = {
    {1, {150, 120}}, {2, {158, 130}}, {3, {166, 112}}, {4, {166, 138}},
};

constexpr TableDef kTable2{
    kTable2BorderLine, nullptr, 0, kTable2Pockets, 6, {60, 140}, kTable2Targets, 4,
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

constexpr TargetDef kTable3Targets[5] = {
    {1, {140, 140}}, {2, {148, 132}}, {3, {148, 148}}, {4, {156, 124}}, {5, {156, 156}},
};

constexpr TableDef kTable3{
    kTable3BorderLine, nullptr, 0, kTable2Pockets, 6, {70, 110}, kTable3Targets, 5,
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

constexpr const TableDef* kStages[kStageCount] = {&kTable1, &kTable2, &kTable3, &kTable4};

}  // namespace

const TableDef& tableForStage(uint8_t stage) {
    if (stage < 1) {
        return kTable1;
    }
    if (stage > kStageCount) {
        return kTable4;
    }
    return *kStages[stage - 1];
}

}  // namespace pool
