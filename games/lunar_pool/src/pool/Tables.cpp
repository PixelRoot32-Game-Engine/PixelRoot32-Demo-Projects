/*
 * Tables.cpp - see Tables.h.
 */
#include "pool/Tables.h"

namespace pool {
namespace {

/**
 * Shared cushion border: a rectangle (play area x 16..224, y 56..224) with a
 * bag notch cut at each of the 6 pocket locations, so every pocket mouth is
 * formed by two of the border's own vertices (loadTable()'s BadMouthIndex
 * check needs those as indices into this array). Wound clockwise on screen,
 * the winding loadTable() requires for a border. All three shipped tables
 * share it; they differ in obstacles and ball layouts instead.
 */
constexpr PointPx kSharedBorder[24] = {
    // Top-left corner: enters from the left edge, exits to the top edge.
    {16, 66}, {10, 60}, {20, 50}, {26, 56},
    // Top-side pocket.
    {113, 56}, {113, 48}, {127, 48}, {127, 56},
    // Top-right corner: enters from the top edge, exits to the right edge.
    {214, 56}, {220, 50}, {230, 60}, {224, 66},
    // Bottom-right corner: enters from the right edge, exits to the bottom edge.
    {224, 214}, {230, 220}, {220, 230}, {214, 224},
    // Bottom-side pocket.
    {127, 224}, {127, 232}, {113, 232}, {113, 224},
    // Bottom-left corner: enters from the bottom edge, exits to the left edge.
    {26, 224}, {20, 230}, {10, 220}, {16, 214},
};
constexpr Polyline kSharedBorderLine{kSharedBorder, 24};

/** {mouthA, mouthB} index into kSharedBorder; center is the capture point. */
constexpr PocketDef kSharedPockets[6] = {
    {{17, 57}, 0, 3},      // top-left
    {{120, 52}, 4, 7},     // top-side
    {{223, 57}, 8, 11},    // top-right
    {{223, 223}, 12, 15},  // bottom-right
    {{120, 228}, 16, 19},  // bottom-side
    {{17, 223}, 20, 23},   // bottom-left
};

// --- Table 1: open table, no obstacles ---------------------------------------

constexpr TargetDef kTable1Targets[6] = {
    {1, {150, 140}}, {2, {158, 135}}, {3, {158, 145}},
    {4, {166, 130}}, {5, {166, 140}}, {6, {166, 150}},
};

constexpr TableDef kTable1{
    kSharedBorderLine, nullptr, 0, kSharedPockets, 6, {70, 140}, kTable1Targets, 6,
};

// --- Table 2: one central block ----------------------------------------------
//
// A 30x16 block left of the rack forces bank shots around it. Wound
// top-left -> bottom-left -> bottom-right -> top-right (counter-clockwise on
// screen), the winding loadTable() requires for obstacles. Balls sit clear:
// the cue 35 px left of the block, the rack at y <= 130 (top edge at 132)
// and x >= 150 (right edge at 125).

constexpr PointPx kTable2ObstaclePoints[4] = {{95, 132}, {95, 148}, {125, 148}, {125, 132}};
constexpr Polyline kTable2Obstacles[1] = {Polyline{kTable2ObstaclePoints, 4}};

constexpr TargetDef kTable2Targets[6] = {
    {1, {150, 120}}, {2, {158, 115}}, {3, {158, 125}},
    {4, {166, 110}}, {5, {166, 120}}, {6, {166, 130}},
};

constexpr TableDef kTable2{
    kSharedBorderLine, kTable2Obstacles, 1, kSharedPockets, 6, {60, 140}, kTable2Targets, 6,
};

// --- Table 3: gate pair ------------------------------------------------------
//
// Two 20x8 blocks above and below the middle form a gate the cue must thread.
// Same counter-clockwise winding as table 2's block. The rack clusters
// between the blocks (y 125..145) and the cue sits left at (60, 120), all
// well clear of both blocks (x 110..130, y 100..108 and 172..180).

constexpr PointPx kTable3ObstacleAPoints[4] = {{110, 100}, {110, 108}, {130, 108}, {130, 100}};
constexpr PointPx kTable3ObstacleBPoints[4] = {{110, 172}, {110, 180}, {130, 180}, {130, 172}};
constexpr Polyline kTable3Obstacles[2] = {
    Polyline{kTable3ObstacleAPoints, 4},
    Polyline{kTable3ObstacleBPoints, 4},
};

constexpr TargetDef kTable3Targets[6] = {
    {1, {150, 135}}, {2, {158, 130}}, {3, {158, 140}},
    {4, {166, 125}}, {5, {166, 135}}, {6, {166, 145}},
};

constexpr TableDef kTable3{
    kSharedBorderLine, kTable3Obstacles, 2, kSharedPockets, 6, {60, 120}, kTable3Targets, 6,
};

constexpr const TableDef* kStages[kStageCount] = {&kTable1, &kTable2, &kTable3};

}  // namespace

const TableDef& tableForStage(uint8_t stage) {
    if (stage < 1) {
        return kTable1;
    }
    if (stage > kStageCount) {
        return kTable3;
    }
    return *kStages[stage - 1];
}

}  // namespace pool
