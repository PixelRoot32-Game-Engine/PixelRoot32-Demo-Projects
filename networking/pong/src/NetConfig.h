/*
 * Networking Pong - values every node and every target agrees on.
 *
 * This header is the single place the three boards can disagree about by
 * accident, so it holds no target-specific flag: the channel and the three node
 * ids are the wire contract, and the cadence numbers are the timing contract
 * the host and the two controllers must share to stay in step.
 */
#pragma once

#include <cstdint>

namespace netpong {

/// ESP-NOW channel shared by all three nodes.
///
/// Channel 6 rather than the default 1 on purpose: the other demos in this
/// repository all sit on channel 1, and a room where several boards are being
/// flashed at once is congested there. Nothing else listens on 6.
inline constexpr std::uint8_t kChannel = 6;

/// Application label of the CYD that runs the match.
inline constexpr std::uint8_t kNodeIdHost = 1;

/// Application label of the first C3 controller (ESP_NOW_NODE_ID=2).
inline constexpr std::uint8_t kNodeIdP1 = 2;

/// Application label of the second C3 controller (ESP_NOW_NODE_ID=3).
inline constexpr std::uint8_t kNodeIdP2 = 3;

/// Fixed rules step. The host advances the match on this clock, never on the
/// frame clock, so a slow frame cannot change how far the ball travelled.
inline constexpr std::uint8_t kRulesHz = 60;

/// Catch-up steps the host is allowed to run in one frame. A longer stall is
/// dropped rather than replayed: after three steps the ball is stale anyway,
/// and replaying more would stall the next frame too.
inline constexpr std::uint8_t kMaxCatchUpSteps = 3;

/// State broadcast rate per controller. `STATE` is unreliable, so sending it
/// faster only adds airtime without making the picture smoother.
inline constexpr std::uint8_t kStateHz = 30;

/// Re-send of an unchanged `INPUT` state. Without it the host cannot tell a
/// paddle holding still from a controller that stopped talking.
inline constexpr std::uint16_t kInputKeepaliveMs = 250;

/// Controller OLED refresh rate. A full 128x64 frame over I2C blocks for about
/// 23 ms, so the panel is redrawn at most this often even when the score moves.
inline constexpr std::uint8_t kControllerRedrawHz = 10;

}  // namespace netpong
