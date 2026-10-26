/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @file PongProtocol.h
 * @brief Frozen ESP-NOW wire contract shared by the CYD host and the two C3 controllers.
 *
 * Every payload starts with a one-byte tag followed by fixed-width fields in
 * little-endian order. The bytes are written and read field by field instead of
 * casting the structs: the host is an ESP32 and the controllers are ESP32-C3,
 * but a struct cast would still depend on compiler padding, on the chosen
 * underlying width of an enum and on both compilers agreeing about layout. A
 * hand-encoded frame is compiler independent, and it lets the host test suite
 * assert every byte of every message instead of only proving that the two halves
 * of the codec happen to mirror each other.
 *
 * Decoding never trusts the payload length. Each decoder compares it against the
 * exact size of its message before reading a single field, so a truncated or
 * padded frame is rejected rather than read past its end, and a rejected frame
 * leaves the destination untouched.
 *
 * Wire layout (tag byte first, integers little endian, i8/i16 signed):
 * @code
 *   AppHello:   [20][version u8][role u8]                                            (3 bytes)
 *   Input:      [21][seq u16][buttons u8][heldMs u8]                                 (5 bytes)
 *   State:      [22][seq u16][phase u8][ballX i16][ballY i16][ballVX i16][ballVY i16]
 *                   [p1Y i16][p2Y i16][score1 u8][score2 u8][flags u8]               (19 bytes)
 *   Point:      [23][seq u16][scorer u8][score1 u8][score2 u8]                       (6 bytes)
 *   RoundStart: [24][seq u16][serveDir i8]                                           (4 bytes)
 *   Pause:      [25][paused u8]                                                      (2 bytes)
 *   MatchEnd:   [26][winner u8][score1 u8][score2 u8]                                (4 bytes)
 * @endcode
 *
 * Budget contract: fixed-size buffers only, no heap, no exceptions, no global
 * state, no Arduino and no engine header. The same translation unit therefore
 * compiles for the host test suite, for the CYD host and for both C3 boards.
 */
namespace netpong::net {

/** @brief Tag byte of APP_HELLO, the version/role handshake. */
inline constexpr std::uint8_t kTagAppHello = 20;

/** @brief Tag byte of INPUT, a controller's held direction. */
inline constexpr std::uint8_t kTagInput = 21;

/** @brief Tag byte of STATE, the host's authoritative snapshot. */
inline constexpr std::uint8_t kTagState = 22;

/** @brief Tag byte of POINT, the discrete end-of-rally event. */
inline constexpr std::uint8_t kTagPoint = 23;

/** @brief Tag byte of ROUND_START, sent before each serve. */
inline constexpr std::uint8_t kTagRoundStart = 24;

/** @brief Tag byte of PAUSE, sent when a peer is lost or comes back. */
inline constexpr std::uint8_t kTagPause = 25;

/** @brief Tag byte of MATCH_END, the final score. */
inline constexpr std::uint8_t kTagMatchEnd = 26;

/// Application protocol version exchanged in APP_HELLO.
///
/// This is not the framework version: it changes only when the layouts above
/// change, and a peer that reports a different value is refused rather than
/// parsed with the wrong field widths.
inline constexpr std::uint8_t kProtocolVersion = 1;

/** @brief Role value of the CYD that owns the match. */
inline constexpr std::uint8_t kRoleHost = 1;

/** @brief Role value of a C3 controller. */
inline constexpr std::uint8_t kRoleController = 2;

/** @brief Held left in INPUT.buttons. */
inline constexpr std::uint8_t kButtonLeft = 1u << 0;

/** @brief Held right in INPUT.buttons. */
inline constexpr std::uint8_t kButtonRight = 1u << 1;

/** @brief Phase value while the host waits for both controllers. */
inline constexpr std::uint8_t kPhaseWaitingForPlayers = 0;

/** @brief Phase value during the pre-serve countdown. */
inline constexpr std::uint8_t kPhaseCountdown = 1;

/** @brief Phase value while the ball is in play. */
inline constexpr std::uint8_t kPhasePlay = 2;

/** @brief Phase value between a point and the next serve. */
inline constexpr std::uint8_t kPhasePoint = 3;

/** @brief Phase value once a side reached the target score. */
inline constexpr std::uint8_t kPhaseMatchOver = 4;

/** @brief Phase value while the match is suspended after a peer loss. */
inline constexpr std::uint8_t kPhasePaused = 5;

/** @brief Bytes of an APP_HELLO frame, tag included. */
inline constexpr std::size_t kAppHelloSize = 3;

/** @brief Bytes of an INPUT frame, tag included. */
inline constexpr std::size_t kInputSize = 5;

/** @brief Bytes of a STATE frame, tag included. */
inline constexpr std::size_t kStateSize = 19;

/** @brief Bytes of a POINT frame, tag included. */
inline constexpr std::size_t kPointSize = 6;

/** @brief Bytes of a ROUND_START frame, tag included. */
inline constexpr std::size_t kRoundStartSize = 4;

/** @brief Bytes of a PAUSE frame, tag included. */
inline constexpr std::size_t kPauseSize = 2;

/** @brief Bytes of a MATCH_END frame, tag included. */
inline constexpr std::size_t kMatchEndSize = 4;

// The declared sizes are the wire contract the two boards and the test vectors
// depend on, so they are pinned to the sum of their own fields here: changing a
// field width without changing the size (or the reverse) stops the build.
static_assert(kAppHelloSize == 1u + sizeof(std::uint8_t) + sizeof(std::uint8_t),
              "APP_HELLO: tag + version u8 + role u8");
static_assert(kInputSize == 1u + sizeof(std::uint16_t) + sizeof(std::uint8_t) + sizeof(std::uint8_t),
              "INPUT: tag + seq u16 + buttons u8 + heldMs u8");
static_assert(kStateSize == 1u + sizeof(std::uint16_t) + sizeof(std::uint8_t) +
                                 6u * sizeof(std::int16_t) + 3u * sizeof(std::uint8_t),
              "STATE: tag + seq u16 + phase u8 + 6*i16 + 3*u8");
static_assert(kPointSize == 1u + sizeof(std::uint16_t) + 3u * sizeof(std::uint8_t),
              "POINT: tag + seq u16 + scorer u8 + score1 u8 + score2 u8");
static_assert(kRoundStartSize == 1u + sizeof(std::uint16_t) + sizeof(std::int8_t),
              "ROUND_START: tag + seq u16 + serveDir i8");
static_assert(kPauseSize == 1u + sizeof(std::uint8_t), "PAUSE: tag + paused u8");
static_assert(kMatchEndSize == 1u + 3u * sizeof(std::uint8_t),
              "MATCH_END: tag + winner u8 + score1 u8 + score2 u8");

/**
 * @enum MessageKind
 * @brief Discriminator of one decoded frame.
 *
 * Unknown is the value every decoder reports instead of guessing: a frame whose
 * tag this build does not know must be ignored, never coerced into the closest
 * message it resembles.
 */
enum class MessageKind : std::uint8_t {
    AppHello,    ///< Version/role handshake.
    Input,       ///< Controller held direction.
    State,       ///< Host authoritative snapshot.
    Point,       ///< Discrete end-of-rally event.
    RoundStart,  ///< Serve direction for the next round.
    Pause,       ///< Match suspended or resumed.
    MatchEnd,    ///< Final result.
    Unknown      ///< Not a frame this build understands.
};

/**
 * @struct AppHello
 * @brief Version/role handshake sent once a peer appears.
 *
 * The host refuses a controller whose version differs, so a half-flashed board
 * reports a version mismatch instead of corrupting the match with fields read at
 * the wrong offsets.
 */
struct AppHello {
    std::uint8_t version = 0;  ///< Sender's kProtocolVersion.
    std::uint8_t role = 0;     ///< kRoleHost or kRoleController.
};

/**
 * @struct Input
 * @brief One controller's held direction.
 *
 * Only the held state travels, never a delta: the host owns the paddles, so a
 * lost or reordered INPUT frame cannot move a paddle by an accumulated amount.
 */
struct Input {
    std::uint16_t seq = 0;   ///< Sender's monotonic frame counter.
    std::uint8_t buttons = 0;  ///< kButtonLeft / kButtonRight bit mask.
    std::uint8_t heldMs = 0;   ///< How long the current mask has been held.
};

/**
 * @struct State
 * @brief The host's authoritative snapshot of the match.
 *
 * Sent unreliably at kStateHz, so it is a full picture rather than a delta: a
 * dropped frame costs one refresh, not permanent drift. `seq` lets a controller
 * drop a frame that overtook a newer one.
 */
struct State {
    std::uint16_t seq = 0;   ///< Host monotonic frame counter.
    std::uint8_t phase = 0;  ///< One of the kPhase* values.
    std::int16_t ballX = 0;  ///< Ball X in court units.
    std::int16_t ballY = 0;  ///< Ball Y in court units.
    std::int16_t ballVX = 0; ///< Ball X velocity in court units per step.
    std::int16_t ballVY = 0; ///< Ball Y velocity in court units per step.
    std::int16_t p1Y = 0;    ///< Left paddle centre Y.
    std::int16_t p2Y = 0;    ///< Right paddle centre Y.
    std::uint8_t score1 = 0; ///< Points scored by player one.
    std::uint8_t score2 = 0; ///< Points scored by player two.
    std::uint8_t flags = 0;  ///< Presentation bits reserved for later tasks.
};

/**
 * @struct Point
 * @brief A rally ended and the host awarded the point.
 *
 * Sent reliably: the controllers paint the score from it, and a lost point would
 * leave an OLED showing a score that never happened.
 */
struct Point {
    std::uint16_t seq = 0;   ///< Host monotonic event counter.
    std::uint8_t scorer = 0; ///< 1 for player one, 2 for player two.
    std::uint8_t score1 = 0; ///< Score after the point.
    std::uint8_t score2 = 0; ///< Score after the point.
};

/**
 * @struct RoundStart
 * @brief Tells the controllers which way the next serve travels.
 */
struct RoundStart {
    std::uint16_t seq = 0;  ///< Host monotonic event counter.
    std::int8_t serveDir = 0;  ///< -1 serves left, +1 serves right.
};

/**
 * @struct Pause
 * @brief Suspends or resumes the match.
 *
 * Pausing is a host decision on peer loss; controllers render it so a player
 * knows the board is frozen rather than broken.
 */
struct Pause {
    std::uint8_t paused = 0;  ///< 1 to suspend, 0 to resume.
};

/**
 * @struct MatchEnd
 * @brief Final result of the single-set match.
 */
struct MatchEnd {
    std::uint8_t winner = 0; ///< 1 for player one, 2 for player two.
    std::uint8_t score1 = 0; ///< Final score of player one.
    std::uint8_t score2 = 0; ///< Final score of player two.
};

/**
 * @struct Message
 * @brief One frame of any kind, with every payload resident.
 *
 * Larger than the wire frame on purpose: the network layer decodes into one
 * stack instance and then reads only the payload that matches `kind`, which
 * keeps the receive path allocation free and branch free after the dispatch.
 */
struct Message {
    MessageKind kind = MessageKind::Unknown;  ///< Which payload is valid.
    AppHello appHello{};      ///< Valid when kind == AppHello.
    Input input{};            ///< Valid when kind == Input.
    State state{};            ///< Valid when kind == State.
    Point point{};            ///< Valid when kind == Point.
    RoundStart roundStart{};  ///< Valid when kind == RoundStart.
    Pause pause{};            ///< Valid when kind == Pause.
    MatchEnd matchEnd{};      ///< Valid when kind == MatchEnd.
};

/**
 * @brief Encodes an APP_HELLO frame.
 * @param message Payload to encode.
 * @param out Destination buffer, or nullptr to probe the size only.
 * @param capacity Bytes available at @p out.
 * @return kAppHelloSize on success, or 0 when @p out is null or @p capacity is
 *         smaller than kAppHelloSize. Nothing is written when it returns 0.
 */
[[nodiscard]] std::size_t encodeAppHello(const AppHello& message, std::uint8_t* out, std::size_t capacity);

/**
 * @brief Decodes an APP_HELLO frame.
 * @param in Source frame, tag included.
 * @param len Bytes available at @p in.
 * @param out Destination, written only on success.
 * @return true when the tag and the length match exactly; false for a null
 *         pointer, a foreign tag or any other length, leaving @p out untouched.
 */
[[nodiscard]] bool decodeAppHello(const std::uint8_t* in, std::size_t len, AppHello& out);

/**
 * @brief Encodes an INPUT frame.
 * @param message Payload to encode.
 * @param out Destination buffer, or nullptr to probe the size only.
 * @param capacity Bytes available at @p out.
 * @return kInputSize on success, or 0 when the buffer cannot hold the frame.
 */
[[nodiscard]] std::size_t encodeInput(const Input& message, std::uint8_t* out, std::size_t capacity);

/**
 * @brief Decodes an INPUT frame.
 * @param in Source frame, tag included.
 * @param len Bytes available at @p in.
 * @param out Destination, written only on success.
 * @return true when the tag and the length match exactly; false otherwise.
 */
[[nodiscard]] bool decodeInput(const std::uint8_t* in, std::size_t len, Input& out);

/**
 * @brief Encodes a STATE frame.
 * @param message Payload to encode.
 * @param out Destination buffer, or nullptr to probe the size only.
 * @param capacity Bytes available at @p out.
 * @return kStateSize on success, or 0 when the buffer cannot hold the frame.
 */
[[nodiscard]] std::size_t encodeState(const State& message, std::uint8_t* out, std::size_t capacity);

/**
 * @brief Decodes a STATE frame.
 * @param in Source frame, tag included.
 * @param len Bytes available at @p in.
 * @param out Destination, written only on success.
 * @return true when the tag and the length match exactly; false otherwise.
 */
[[nodiscard]] bool decodeState(const std::uint8_t* in, std::size_t len, State& out);

/**
 * @brief Encodes a POINT frame.
 * @param message Payload to encode.
 * @param out Destination buffer, or nullptr to probe the size only.
 * @param capacity Bytes available at @p out.
 * @return kPointSize on success, or 0 when the buffer cannot hold the frame.
 */
[[nodiscard]] std::size_t encodePoint(const Point& message, std::uint8_t* out, std::size_t capacity);

/**
 * @brief Decodes a POINT frame.
 * @param in Source frame, tag included.
 * @param len Bytes available at @p in.
 * @param out Destination, written only on success.
 * @return true when the tag and the length match exactly; false otherwise.
 */
[[nodiscard]] bool decodePoint(const std::uint8_t* in, std::size_t len, Point& out);

/**
 * @brief Encodes a ROUND_START frame.
 * @param message Payload to encode.
 * @param out Destination buffer, or nullptr to probe the size only.
 * @param capacity Bytes available at @p out.
 * @return kRoundStartSize on success, or 0 when the buffer cannot hold the frame.
 */
[[nodiscard]] std::size_t encodeRoundStart(const RoundStart& message, std::uint8_t* out, std::size_t capacity);

/**
 * @brief Decodes a ROUND_START frame.
 * @param in Source frame, tag included.
 * @param len Bytes available at @p in.
 * @param out Destination, written only on success.
 * @return true when the tag and the length match exactly; false otherwise.
 */
[[nodiscard]] bool decodeRoundStart(const std::uint8_t* in, std::size_t len, RoundStart& out);

/**
 * @brief Encodes a PAUSE frame.
 * @param message Payload to encode.
 * @param out Destination buffer, or nullptr to probe the size only.
 * @param capacity Bytes available at @p out.
 * @return kPauseSize on success, or 0 when the buffer cannot hold the frame.
 */
[[nodiscard]] std::size_t encodePause(const Pause& message, std::uint8_t* out, std::size_t capacity);

/**
 * @brief Decodes a PAUSE frame.
 * @param in Source frame, tag included.
 * @param len Bytes available at @p in.
 * @param out Destination, written only on success.
 * @return true when the tag and the length match exactly; false otherwise.
 */
[[nodiscard]] bool decodePause(const std::uint8_t* in, std::size_t len, Pause& out);

/**
 * @brief Encodes a MATCH_END frame.
 * @param message Payload to encode.
 * @param out Destination buffer, or nullptr to probe the size only.
 * @param capacity Bytes available at @p out.
 * @return kMatchEndSize on success, or 0 when the buffer cannot hold the frame.
 */
[[nodiscard]] std::size_t encodeMatchEnd(const MatchEnd& message, std::uint8_t* out, std::size_t capacity);

/**
 * @brief Decodes a MATCH_END frame.
 * @param in Source frame, tag included.
 * @param len Bytes available at @p in.
 * @param out Destination, written only on success.
 * @return true when the tag and the length match exactly; false otherwise.
 */
[[nodiscard]] bool decodeMatchEnd(const std::uint8_t* in, std::size_t len, MatchEnd& out);

/**
 * @brief Encodes whichever payload @p message.kind selects.
 * @param message Frame to encode; kind must not be Unknown.
 * @param out Destination buffer, or nullptr to probe the size only.
 * @param capacity Bytes available at @p out.
 * @return The bytes written (the size of that kind's frame), or 0 for an
 *         unknown kind or a buffer that cannot hold the frame.
 */
[[nodiscard]] std::size_t encode(const Message& message, std::uint8_t* out, std::size_t capacity);

/**
 * @brief Decodes a frame of any kind into @p out.
 * @param in Source frame, tag included.
 * @param len Bytes available at @p in.
 * @param out Destination, written only on success.
 * @return true when the tag is known and the length matches that kind exactly;
 *         false otherwise, including a null pointer and an unknown tag.
 */
[[nodiscard]] bool decode(const std::uint8_t* in, std::size_t len, Message& out);

/**
 * @brief Classifies a frame without decoding it.
 * @param in Source frame; only the first byte is read.
 * @param len Bytes available at @p in.
 * @return The kind named by the tag byte, or Unknown for a null pointer, an
 *         empty buffer or a tag this build does not know.
 */
[[nodiscard]] MessageKind kindOf(const std::uint8_t* in, std::size_t len);

/**
 * @brief Returns a stable, human-readable name for a kind.
 * @param kind Kind to name.
 * @return Static string; never null. Intended for logs and test output.
 */
[[nodiscard]] const char* kindName(MessageKind kind);

/**
 * @brief Wrap-safe comparison of two 16-bit sequence numbers (RFC 1982).
 * @param candidate Sequence number of the frame being considered.
 * @param reference Sequence number of the frame already accepted.
 * @return true when @p candidate is strictly newer than @p reference.
 *
 * The window is half open upward: a distance of exactly 0x8000 is not an
 * ordering in either direction, so both calls return false and the receiver
 * keeps what it has instead of guessing.
 */
[[nodiscard]] bool isNewerSequence(std::uint16_t candidate, std::uint16_t reference);

/**
 * @brief Tells whether a peer's APP_HELLO version can be parsed by this build.
 * @param version Version byte reported by the peer.
 * @return true only for kProtocolVersion.
 */
[[nodiscard]] bool isCompatibleVersion(std::uint8_t version);

}  // namespace netpong::net
