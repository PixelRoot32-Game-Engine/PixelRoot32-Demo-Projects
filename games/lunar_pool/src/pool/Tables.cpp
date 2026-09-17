/*
 * Tables.cpp - see Tables.h.
 */
#include "pool/Tables.h"

namespace pool {
namespace {

/**
 * Table 1's cushion border: a rectangle (play area x 16..224, y 56..224)
 * with a bag notch cut at each of the 6 pocket locations, so every pocket
 * mouth is formed by two of the border's own vertices (loadTable()'s
 * BadMouthIndex check needs those as indices into this array). Wound
 * clockwise on screen, the winding loadTable() requires for a border.
 */
constexpr PointPx kTable1Border[24] = {
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
constexpr Polyline kTable1BorderLine{kTable1Border, 24};

/** {mouthA, mouthB} index into kTable1Border; center is the capture point. */
constexpr PocketDef kTable1Pockets[6] = {
    {{17, 57}, 0, 3},      // top-left
    {{120, 52}, 4, 7},     // top-side
    {{223, 57}, 8, 11},    // top-right
    {{223, 223}, 12, 15},  // bottom-right
    {{120, 228}, 16, 19},  // bottom-side
    {{17, 223}, 20, 23},   // bottom-left
};

constexpr TargetDef kTable1Targets[6] = {
    {1, {150, 140}}, {2, {158, 135}}, {3, {158, 145}},
    {4, {166, 130}}, {5, {166, 140}}, {6, {166, 150}},
};

constexpr TableDef kTable1{
    kTable1BorderLine, nullptr, 0, kTable1Pockets, 6, {70, 140}, kTable1Targets, 6,
};

}  // namespace

const TableDef& tableForStage(uint8_t stage) {
    (void)stage;  // Only table 1 ships; every stage returns it for now.
    return kTable1;
}

}  // namespace pool
