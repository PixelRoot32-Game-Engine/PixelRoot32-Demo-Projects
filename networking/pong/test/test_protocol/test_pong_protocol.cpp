/*
 * Wire-contract tests for the networking Pong codec.
 *
 * A round-trip test alone cannot pin this protocol: if encode and decode agreed
 * on a big-endian word, or swapped ballX with ballY, both halves would mirror
 * the same mistake and the round trip would still pass. So the primary tests
 * here compare the encoded bytes against explicit vectors copied from the layout
 * in PongProtocol.h, and the round-trip tests only cover the value extremes that
 * a byte vector would be tedious to spell out.
 *
 * The rejection tests carry equal weight: a codec that reads a truncated frame
 * is how a demo starts reading stale stack past the end of an ESP-NOW payload.
 * Every decoder is checked against a short frame, a padded frame, a foreign tag
 * and a null pointer, and the destination struct must survive all four
 * untouched.
 */
#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "net/PongProtocol.h"

using namespace netpong::net;

namespace {

constexpr std::uint8_t kSentinel = 0xA5;
constexpr std::uint8_t kDirty = 0x5A;
constexpr std::uint8_t kUnknownTag = 27;
constexpr std::int16_t kInt16Min = static_cast<std::int16_t>(-32768);
constexpr std::int16_t kInt16Max = static_cast<std::int16_t>(32767);

/// Compares a frame byte by byte, reporting the offset so a shifted field is
/// obvious in the failure line.
void assertBytes(const std::uint8_t* actual, const std::uint8_t* expected, std::size_t length) {
    char message[64];
    for (std::size_t i = 0; i < length; ++i) {
        std::snprintf(message, sizeof(message), "payload byte %u", static_cast<unsigned>(i));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected[i], actual[i], message);
    }
}

void fill(std::uint8_t* buffer, std::size_t length, std::uint8_t value) {
    for (std::size_t i = 0; i < length; ++i) {
        buffer[i] = value;
    }
}

void assertSentinel(const std::uint8_t* buffer, std::size_t length, std::uint8_t value) {
    for (std::size_t i = 0; i < length; ++i) {
        TEST_ASSERT_EQUAL_UINT8(value, buffer[i]);
    }
}

/// Exercises the four rejections every decoder must make. `frame` is a valid
/// encoding of the message whose decoder is passed in, so only the defect under
/// test differs from a frame that would otherwise be accepted.
template <typename T>
void assertRejectsMalformed(bool (*decodeMessage)(const std::uint8_t*, std::size_t, T&),
                            const std::uint8_t* frame,
                            std::size_t size,
                            std::uint8_t foreignTag,
                            T& out) {
    std::uint8_t padded[32] = {};
    std::uint8_t retagged[32] = {};
    TEST_ASSERT_TRUE(size + 1 <= sizeof(padded));
    std::memcpy(padded, frame, size);
    std::memcpy(retagged, frame, size);
    retagged[0] = foreignTag;

    TEST_ASSERT_FALSE_MESSAGE(decodeMessage(frame, size - 1, out), "short frame accepted");
    TEST_ASSERT_FALSE_MESSAGE(decodeMessage(padded, size + 1, out), "padded frame accepted");
    TEST_ASSERT_FALSE_MESSAGE(decodeMessage(retagged, size, out), "foreign tag accepted");
    TEST_ASSERT_FALSE_MESSAGE(decodeMessage(nullptr, size, out), "null frame accepted");
}

/// Every encoder must refuse a buffer that cannot hold the whole message and
/// must leave that buffer alone when it does.
template <typename T>
void assertEncoderNeedsFullCapacity(std::size_t (*encodeMessage)(const T&, std::uint8_t*, std::size_t),
                                    const T& message,
                                    std::size_t size) {
    std::uint8_t buffer[32];
    fill(buffer, sizeof(buffer), kSentinel);

    TEST_ASSERT_EQUAL_UINT32(0, static_cast<std::uint32_t>(encodeMessage(message, buffer, size - 1)));
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<std::uint32_t>(encodeMessage(message, buffer, 0)));
    assertSentinel(buffer, sizeof(buffer), kSentinel);

    TEST_ASSERT_EQUAL_UINT32(0, static_cast<std::uint32_t>(encodeMessage(message, nullptr, size)));
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(size),
                             static_cast<std::uint32_t>(encodeMessage(message, buffer, size)));
}

std::uint32_t sizeOf(std::size_t value) {
    return static_cast<std::uint32_t>(value);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// -----------------------------------------------------------------------------
// Contract constants
// -----------------------------------------------------------------------------

void test_contract_constants_match_the_documented_wire(void) {
    TEST_ASSERT_EQUAL_UINT8(20, kTagAppHello);
    TEST_ASSERT_EQUAL_UINT8(21, kTagInput);
    TEST_ASSERT_EQUAL_UINT8(22, kTagState);
    TEST_ASSERT_EQUAL_UINT8(23, kTagPoint);
    TEST_ASSERT_EQUAL_UINT8(24, kTagRoundStart);
    TEST_ASSERT_EQUAL_UINT8(25, kTagPause);
    TEST_ASSERT_EQUAL_UINT8(26, kTagMatchEnd);

    TEST_ASSERT_EQUAL_UINT8(1, kProtocolVersion);
    TEST_ASSERT_EQUAL_UINT8(1, kRoleHost);
    TEST_ASSERT_EQUAL_UINT8(2, kRoleController);

    TEST_ASSERT_EQUAL_UINT8(0x01, kButtonLeft);
    TEST_ASSERT_EQUAL_UINT8(0x02, kButtonRight);
    TEST_ASSERT_EQUAL_UINT8(0x03, static_cast<std::uint8_t>(kButtonLeft | kButtonRight));

    TEST_ASSERT_EQUAL_UINT8(0, kPhaseWaitingForPlayers);
    TEST_ASSERT_EQUAL_UINT8(1, kPhaseCountdown);
    TEST_ASSERT_EQUAL_UINT8(2, kPhasePlay);
    TEST_ASSERT_EQUAL_UINT8(3, kPhasePoint);
    TEST_ASSERT_EQUAL_UINT8(4, kPhaseMatchOver);
    TEST_ASSERT_EQUAL_UINT8(5, kPhasePaused);

    TEST_ASSERT_EQUAL_UINT32(3, sizeOf(kAppHelloSize));
    TEST_ASSERT_EQUAL_UINT32(5, sizeOf(kInputSize));
    TEST_ASSERT_EQUAL_UINT32(19, sizeOf(kStateSize));
    TEST_ASSERT_EQUAL_UINT32(6, sizeOf(kPointSize));
    TEST_ASSERT_EQUAL_UINT32(4, sizeOf(kRoundStartSize));
    TEST_ASSERT_EQUAL_UINT32(2, sizeOf(kPauseSize));
    TEST_ASSERT_EQUAL_UINT32(4, sizeOf(kMatchEndSize));
}

// -----------------------------------------------------------------------------
// Exact byte layouts: this is what pins little-endian order and field order.
// -----------------------------------------------------------------------------

void test_encode_app_hello_pins_the_wire_bytes(void) {
    AppHello message;
    message.version = kProtocolVersion;
    message.role = kRoleController;

    std::uint8_t buffer[kAppHelloSize + 4];
    fill(buffer, sizeof(buffer), kSentinel);
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kAppHelloSize), sizeOf(encodeAppHello(message, buffer, sizeof(buffer))));

    const std::uint8_t expected[kAppHelloSize] = {kTagAppHello, 0x01, 0x02};
    assertBytes(buffer, expected, kAppHelloSize);
    assertSentinel(buffer + kAppHelloSize, 4, kSentinel);
}

void test_encode_input_pins_the_wire_bytes(void) {
    Input message;
    message.seq = 0x1234;
    message.buttons = static_cast<std::uint8_t>(kButtonLeft | kButtonRight);
    message.heldMs = 0xAB;

    std::uint8_t buffer[kInputSize + 4];
    fill(buffer, sizeof(buffer), kSentinel);
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kInputSize), sizeOf(encodeInput(message, buffer, sizeof(buffer))));

    const std::uint8_t expected[kInputSize] = {kTagInput, 0x34, 0x12, 0x03, 0xAB};
    assertBytes(buffer, expected, kInputSize);
    assertSentinel(buffer + kInputSize, 4, kSentinel);
}

void test_encode_state_pins_the_wire_bytes(void) {
    State message;
    message.seq = 0xABCD;
    message.phase = kPhasePlay;
    message.ballX = 258;
    message.ballY = -2;
    message.ballVX = -3;
    message.ballVY = 0x0405;
    message.p1Y = 0x0607;
    message.p2Y = -1000;
    message.score1 = 4;
    message.score2 = 5;
    message.flags = 0x80;

    std::uint8_t buffer[kStateSize + 4];
    fill(buffer, sizeof(buffer), kSentinel);
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kStateSize), sizeOf(encodeState(message, buffer, sizeof(buffer))));

    const std::uint8_t expected[kStateSize] = {
        kTagState,
        0xCD, 0xAB,  // seq = 0xABCD
        0x02,        // phase = PLAY
        0x02, 0x01,  // ballX = 258
        0xFE, 0xFF,  // ballY = -2
        0xFD, 0xFF,  // ballVX = -3
        0x05, 0x04,  // ballVY = 0x0405
        0x07, 0x06,  // p1Y = 0x0607
        0x18, 0xFC,  // p2Y = -1000
        0x04,        // score1
        0x05,        // score2
        0x80,        // flags
    };
    assertBytes(buffer, expected, kStateSize);
    assertSentinel(buffer + kStateSize, 4, kSentinel);
}

void test_encode_point_pins_the_wire_bytes(void) {
    Point message;
    message.seq = 0x00FF;
    message.scorer = kRoleController;
    message.score1 = 1;
    message.score2 = 0;

    std::uint8_t buffer[kPointSize + 4];
    fill(buffer, sizeof(buffer), kSentinel);
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kPointSize), sizeOf(encodePoint(message, buffer, sizeof(buffer))));

    const std::uint8_t expected[kPointSize] = {kTagPoint, 0xFF, 0x00, 0x02, 0x01, 0x00};
    assertBytes(buffer, expected, kPointSize);
    assertSentinel(buffer + kPointSize, 4, kSentinel);
}

void test_encode_round_start_pins_the_wire_bytes(void) {
    RoundStart message;
    message.seq = 0x0001;
    message.serveDir = -1;

    std::uint8_t buffer[kRoundStartSize + 4];
    fill(buffer, sizeof(buffer), kSentinel);
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kRoundStartSize),
                             sizeOf(encodeRoundStart(message, buffer, sizeof(buffer))));

    const std::uint8_t expected[kRoundStartSize] = {kTagRoundStart, 0x01, 0x00, 0xFF};
    assertBytes(buffer, expected, kRoundStartSize);
    assertSentinel(buffer + kRoundStartSize, 4, kSentinel);
}

void test_encode_pause_pins_the_wire_bytes(void) {
    Pause message;
    message.paused = 1;

    std::uint8_t buffer[kPauseSize + 4];
    fill(buffer, sizeof(buffer), kSentinel);
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kPauseSize), sizeOf(encodePause(message, buffer, sizeof(buffer))));

    const std::uint8_t expected[kPauseSize] = {kTagPause, 0x01};
    assertBytes(buffer, expected, kPauseSize);
    assertSentinel(buffer + kPauseSize, 4, kSentinel);
}

void test_encode_match_end_pins_the_wire_bytes(void) {
    MatchEnd message;
    message.winner = kRoleHost;
    message.score1 = 5;
    message.score2 = 3;

    std::uint8_t buffer[kMatchEndSize + 4];
    fill(buffer, sizeof(buffer), kSentinel);
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kMatchEndSize), sizeOf(encodeMatchEnd(message, buffer, sizeof(buffer))));

    const std::uint8_t expected[kMatchEndSize] = {kTagMatchEnd, 0x01, 0x05, 0x03};
    assertBytes(buffer, expected, kMatchEndSize);
    assertSentinel(buffer + kMatchEndSize, 4, kSentinel);
}

// -----------------------------------------------------------------------------
// Round trips with field extremes
// -----------------------------------------------------------------------------

void test_app_hello_round_trips_version_and_role(void) {
    AppHello message;
    message.version = 0;
    message.role = 255;

    std::uint8_t buffer[kAppHelloSize];
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kAppHelloSize), sizeOf(encodeAppHello(message, buffer, sizeof(buffer))));

    AppHello decoded;
    TEST_ASSERT_TRUE(decodeAppHello(buffer, kAppHelloSize, decoded));
    TEST_ASSERT_EQUAL_UINT8(0, decoded.version);
    TEST_ASSERT_EQUAL_UINT8(255, decoded.role);
}

void test_input_round_trips_sequence_wrap_and_buttons(void) {
    Input message;
    message.seq = 0xFFFF;
    message.buttons = 0;
    message.heldMs = 255;

    std::uint8_t buffer[kInputSize];
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kInputSize), sizeOf(encodeInput(message, buffer, sizeof(buffer))));

    Input decoded;
    TEST_ASSERT_TRUE(decodeInput(buffer, kInputSize, decoded));
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, decoded.seq);
    TEST_ASSERT_EQUAL_UINT8(0, decoded.buttons);
    TEST_ASSERT_EQUAL_UINT8(255, decoded.heldMs);

    message.seq = 0;
    message.buttons = static_cast<std::uint8_t>(kButtonLeft | kButtonRight);
    message.heldMs = 0;
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kInputSize), sizeOf(encodeInput(message, buffer, sizeof(buffer))));
    TEST_ASSERT_TRUE(decodeInput(buffer, kInputSize, decoded));
    TEST_ASSERT_EQUAL_UINT16(0, decoded.seq);
    TEST_ASSERT_EQUAL_UINT8(3, decoded.buttons);
    TEST_ASSERT_EQUAL_UINT8(0, decoded.heldMs);
}

void test_state_round_trips_extreme_fields(void) {
    State message;
    message.seq = 0xFFFF;
    message.phase = kPhasePaused;
    message.ballX = kInt16Min;
    message.ballY = kInt16Max;
    message.ballVX = kInt16Min;
    message.ballVY = kInt16Max;
    message.p1Y = -1;
    message.p2Y = 1;
    message.score1 = 255;
    message.score2 = 0;
    message.flags = 0xFF;

    std::uint8_t buffer[kStateSize];
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kStateSize), sizeOf(encodeState(message, buffer, sizeof(buffer))));

    State decoded;
    TEST_ASSERT_TRUE(decodeState(buffer, kStateSize, decoded));
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, decoded.seq);
    TEST_ASSERT_EQUAL_UINT8(kPhasePaused, decoded.phase);
    TEST_ASSERT_EQUAL_INT16(kInt16Min, decoded.ballX);
    TEST_ASSERT_EQUAL_INT16(kInt16Max, decoded.ballY);
    TEST_ASSERT_EQUAL_INT16(kInt16Min, decoded.ballVX);
    TEST_ASSERT_EQUAL_INT16(kInt16Max, decoded.ballVY);
    TEST_ASSERT_EQUAL_INT16(-1, decoded.p1Y);
    TEST_ASSERT_EQUAL_INT16(1, decoded.p2Y);
    TEST_ASSERT_EQUAL_UINT8(255, decoded.score1);
    TEST_ASSERT_EQUAL_UINT8(0, decoded.score2);
    TEST_ASSERT_EQUAL_UINT8(0xFF, decoded.flags);
}

void test_state_round_trips_every_phase(void) {
    std::uint8_t buffer[kStateSize];
    State decoded;

    for (std::uint8_t phase = kPhaseWaitingForPlayers; phase <= kPhasePaused; ++phase) {
        State message;
        message.seq = 7;
        message.phase = phase;

        TEST_ASSERT_EQUAL_UINT32(sizeOf(kStateSize), sizeOf(encodeState(message, buffer, sizeof(buffer))));
        TEST_ASSERT_TRUE(decodeState(buffer, kStateSize, decoded));
        TEST_ASSERT_EQUAL_UINT8(phase, decoded.phase);
    }
}

void test_point_round_trips_scores(void) {
    Point message;
    message.seq = 0x8000;
    message.scorer = 0;
    message.score1 = 255;
    message.score2 = 255;

    std::uint8_t buffer[kPointSize];
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kPointSize), sizeOf(encodePoint(message, buffer, sizeof(buffer))));

    Point decoded;
    TEST_ASSERT_TRUE(decodePoint(buffer, kPointSize, decoded));
    TEST_ASSERT_EQUAL_UINT16(0x8000, decoded.seq);
    TEST_ASSERT_EQUAL_UINT8(0, decoded.scorer);
    TEST_ASSERT_EQUAL_UINT8(255, decoded.score1);
    TEST_ASSERT_EQUAL_UINT8(255, decoded.score2);
}

void test_round_start_round_trips_signed_serve_direction(void) {
    const std::int8_t directions[4] = {-128, -1, 1, 127};
    std::uint8_t buffer[kRoundStartSize];

    for (std::size_t i = 0; i < 4; ++i) {
        RoundStart message;
        message.seq = 0x0102;
        message.serveDir = directions[i];

        TEST_ASSERT_EQUAL_UINT32(sizeOf(kRoundStartSize),
                                 sizeOf(encodeRoundStart(message, buffer, sizeof(buffer))));

        RoundStart decoded;
        TEST_ASSERT_TRUE(decodeRoundStart(buffer, kRoundStartSize, decoded));
        TEST_ASSERT_EQUAL_UINT16(0x0102, decoded.seq);
        TEST_ASSERT_EQUAL_INT8(directions[i], decoded.serveDir);
    }
}

void test_pause_round_trips_both_states(void) {
    std::uint8_t buffer[kPauseSize];

    for (std::uint8_t paused = 0; paused <= 1; ++paused) {
        Pause message;
        message.paused = paused;

        TEST_ASSERT_EQUAL_UINT32(sizeOf(kPauseSize), sizeOf(encodePause(message, buffer, sizeof(buffer))));

        Pause decoded;
        TEST_ASSERT_TRUE(decodePause(buffer, kPauseSize, decoded));
        TEST_ASSERT_EQUAL_UINT8(paused, decoded.paused);
    }
}

void test_match_end_round_trips_winner_and_scores(void) {
    MatchEnd message;
    message.winner = kRoleController;
    message.score1 = 0;
    message.score2 = 255;

    std::uint8_t buffer[kMatchEndSize];
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kMatchEndSize), sizeOf(encodeMatchEnd(message, buffer, sizeof(buffer))));

    MatchEnd decoded;
    TEST_ASSERT_TRUE(decodeMatchEnd(buffer, kMatchEndSize, decoded));
    TEST_ASSERT_EQUAL_UINT8(kRoleController, decoded.winner);
    TEST_ASSERT_EQUAL_UINT8(0, decoded.score1);
    TEST_ASSERT_EQUAL_UINT8(255, decoded.score2);
}

// -----------------------------------------------------------------------------
// Decoder rejection: short, padded, foreign tag, null pointer. The destination
// must be untouched by every rejected frame.
// -----------------------------------------------------------------------------

void test_app_hello_decoder_rejects_malformed_frames(void) {
    AppHello source;
    source.version = kProtocolVersion;
    source.role = kRoleHost;

    std::uint8_t frame[kAppHelloSize];
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kAppHelloSize), sizeOf(encodeAppHello(source, frame, sizeof(frame))));

    AppHello out;
    out.version = kDirty;
    out.role = kDirty;

    assertRejectsMalformed(decodeAppHello, frame, kAppHelloSize, kTagInput, out);

    TEST_ASSERT_EQUAL_UINT8(kDirty, out.version);
    TEST_ASSERT_EQUAL_UINT8(kDirty, out.role);
}

void test_input_decoder_rejects_malformed_frames(void) {
    Input source;
    source.seq = 1;
    source.buttons = kButtonLeft;
    source.heldMs = 10;

    std::uint8_t frame[kInputSize];
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kInputSize), sizeOf(encodeInput(source, frame, sizeof(frame))));

    Input out;
    out.seq = kDirty;
    out.buttons = kDirty;
    out.heldMs = kDirty;

    assertRejectsMalformed(decodeInput, frame, kInputSize, kTagState, out);

    TEST_ASSERT_EQUAL_UINT16(kDirty, out.seq);
    TEST_ASSERT_EQUAL_UINT8(kDirty, out.buttons);
    TEST_ASSERT_EQUAL_UINT8(kDirty, out.heldMs);
}

void test_state_decoder_rejects_malformed_frames(void) {
    State source;
    source.seq = 0x1234;
    source.phase = kPhasePlay;
    source.ballX = 10;
    source.ballY = 20;
    source.ballVX = -1;
    source.ballVY = 1;
    source.p1Y = 3;
    source.p2Y = 4;
    source.score1 = 1;
    source.score2 = 2;
    source.flags = 0x1;

    std::uint8_t frame[kStateSize];
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kStateSize), sizeOf(encodeState(source, frame, sizeof(frame))));

    State out;
    out.seq = kDirty;
    out.ballX = kDirty;
    out.score1 = kDirty;
    out.flags = kDirty;

    assertRejectsMalformed(decodeState, frame, kStateSize, kTagPoint, out);

    TEST_ASSERT_EQUAL_UINT16(kDirty, out.seq);
    TEST_ASSERT_EQUAL_INT16(kDirty, out.ballX);
    TEST_ASSERT_EQUAL_UINT8(kDirty, out.score1);
    TEST_ASSERT_EQUAL_UINT8(kDirty, out.flags);
}

void test_point_decoder_rejects_malformed_frames(void) {
    Point source;
    source.seq = 9;
    source.scorer = 2;
    source.score1 = 3;
    source.score2 = 4;

    std::uint8_t frame[kPointSize];
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kPointSize), sizeOf(encodePoint(source, frame, sizeof(frame))));

    Point out;
    out.seq = kDirty;
    out.scorer = kDirty;
    out.score1 = kDirty;
    out.score2 = kDirty;

    assertRejectsMalformed(decodePoint, frame, kPointSize, kTagRoundStart, out);

    TEST_ASSERT_EQUAL_UINT16(kDirty, out.seq);
    TEST_ASSERT_EQUAL_UINT8(kDirty, out.scorer);
    TEST_ASSERT_EQUAL_UINT8(kDirty, out.score1);
    TEST_ASSERT_EQUAL_UINT8(kDirty, out.score2);
}

void test_round_start_decoder_rejects_malformed_frames(void) {
    RoundStart source;
    source.seq = 11;
    source.serveDir = -1;

    std::uint8_t frame[kRoundStartSize];
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kRoundStartSize),
                             sizeOf(encodeRoundStart(source, frame, sizeof(frame))));

    RoundStart out;
    out.seq = kDirty;
    out.serveDir = static_cast<std::int8_t>(kDirty);

    assertRejectsMalformed(decodeRoundStart, frame, kRoundStartSize, kTagPause, out);

    TEST_ASSERT_EQUAL_UINT16(kDirty, out.seq);
    TEST_ASSERT_EQUAL_INT8(static_cast<std::int8_t>(kDirty), out.serveDir);
}

void test_pause_decoder_rejects_malformed_frames(void) {
    Pause source;
    source.paused = 1;

    std::uint8_t frame[kPauseSize];
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kPauseSize), sizeOf(encodePause(source, frame, sizeof(frame))));

    Pause out;
    out.paused = kDirty;

    assertRejectsMalformed(decodePause, frame, kPauseSize, kTagMatchEnd, out);

    TEST_ASSERT_EQUAL_UINT8(kDirty, out.paused);
}

void test_match_end_decoder_rejects_malformed_frames(void) {
    MatchEnd source;
    source.winner = 1;
    source.score1 = 5;
    source.score2 = 3;

    std::uint8_t frame[kMatchEndSize];
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kMatchEndSize), sizeOf(encodeMatchEnd(source, frame, sizeof(frame))));

    MatchEnd out;
    out.winner = kDirty;
    out.score1 = kDirty;
    out.score2 = kDirty;

    assertRejectsMalformed(decodeMatchEnd, frame, kMatchEndSize, kTagAppHello, out);

    TEST_ASSERT_EQUAL_UINT8(kDirty, out.winner);
    TEST_ASSERT_EQUAL_UINT8(kDirty, out.score1);
    TEST_ASSERT_EQUAL_UINT8(kDirty, out.score2);
}

// -----------------------------------------------------------------------------
// Encoder capacity: one byte short is a refusal, not a partial write.
// -----------------------------------------------------------------------------

void test_app_hello_encoder_needs_full_capacity(void) {
    AppHello message;
    message.version = kProtocolVersion;
    message.role = kRoleHost;
    assertEncoderNeedsFullCapacity(encodeAppHello, message, kAppHelloSize);
}

void test_input_encoder_needs_full_capacity(void) {
    Input message;
    message.seq = 0xFFFF;
    message.buttons = 3;
    message.heldMs = 255;
    assertEncoderNeedsFullCapacity(encodeInput, message, kInputSize);
}

void test_state_encoder_needs_full_capacity(void) {
    State message;
    message.seq = 0xFFFF;
    message.phase = kPhaseMatchOver;
    message.ballX = kInt16Min;
    message.ballY = kInt16Max;
    message.ballVX = kInt16Min;
    message.ballVY = kInt16Max;
    message.p1Y = kInt16Min;
    message.p2Y = kInt16Max;
    message.score1 = 255;
    message.score2 = 255;
    message.flags = 0xFF;
    assertEncoderNeedsFullCapacity(encodeState, message, kStateSize);
}

void test_point_encoder_needs_full_capacity(void) {
    Point message;
    message.seq = 0x8000;
    message.scorer = 2;
    message.score1 = 255;
    message.score2 = 255;
    assertEncoderNeedsFullCapacity(encodePoint, message, kPointSize);
}

void test_round_start_encoder_needs_full_capacity(void) {
    RoundStart message;
    message.seq = 0xFFFF;
    message.serveDir = -128;
    assertEncoderNeedsFullCapacity(encodeRoundStart, message, kRoundStartSize);
}

void test_pause_encoder_needs_full_capacity(void) {
    Pause message;
    message.paused = 1;
    assertEncoderNeedsFullCapacity(encodePause, message, kPauseSize);
}

void test_match_end_encoder_needs_full_capacity(void) {
    MatchEnd message;
    message.winner = 2;
    message.score1 = 5;
    message.score2 = 5;
    assertEncoderNeedsFullCapacity(encodeMatchEnd, message, kMatchEndSize);
}

// -----------------------------------------------------------------------------
// kindOf / kindName
// -----------------------------------------------------------------------------

void test_kind_of_maps_every_known_tag(void) {
    std::uint8_t frame[1];

    frame[0] = kTagAppHello;
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::AppHello), static_cast<int>(kindOf(frame, 1)));
    frame[0] = kTagInput;
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Input), static_cast<int>(kindOf(frame, 1)));
    frame[0] = kTagState;
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::State), static_cast<int>(kindOf(frame, 1)));
    frame[0] = kTagPoint;
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Point), static_cast<int>(kindOf(frame, 1)));
    frame[0] = kTagRoundStart;
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::RoundStart), static_cast<int>(kindOf(frame, 1)));
    frame[0] = kTagPause;
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Pause), static_cast<int>(kindOf(frame, 1)));
    frame[0] = kTagMatchEnd;
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::MatchEnd), static_cast<int>(kindOf(frame, 1)));
}

void test_kind_of_rejects_unknown_tag_and_empty_frame(void) {
    std::uint8_t frame[4] = {kUnknownTag, 0, 0, 0};

    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Unknown), static_cast<int>(kindOf(frame, sizeof(frame))));
    frame[0] = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Unknown), static_cast<int>(kindOf(frame, sizeof(frame))));
    frame[0] = kTagMatchEnd;
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Unknown), static_cast<int>(kindOf(frame, 0)));
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Unknown),
                      static_cast<int>(kindOf(nullptr, kStateSize)));
}

void test_kind_name_is_stable_for_logs(void) {
    TEST_ASSERT_EQUAL_STRING("AppHello", kindName(MessageKind::AppHello));
    TEST_ASSERT_EQUAL_STRING("Input", kindName(MessageKind::Input));
    TEST_ASSERT_EQUAL_STRING("State", kindName(MessageKind::State));
    TEST_ASSERT_EQUAL_STRING("Point", kindName(MessageKind::Point));
    TEST_ASSERT_EQUAL_STRING("RoundStart", kindName(MessageKind::RoundStart));
    TEST_ASSERT_EQUAL_STRING("Pause", kindName(MessageKind::Pause));
    TEST_ASSERT_EQUAL_STRING("MatchEnd", kindName(MessageKind::MatchEnd));
    TEST_ASSERT_EQUAL_STRING("Unknown", kindName(MessageKind::Unknown));
}

// -----------------------------------------------------------------------------
// Aggregate dispatch
// -----------------------------------------------------------------------------

void test_aggregate_round_trips_every_kind(void) {
    std::uint8_t buffer[kStateSize + 1];
    Message out;

    Message hello;
    hello.kind = MessageKind::AppHello;
    hello.appHello.version = kProtocolVersion;
    hello.appHello.role = kRoleController;
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kAppHelloSize), sizeOf(encode(hello, buffer, sizeof(buffer))));
    TEST_ASSERT_TRUE(decode(buffer, kAppHelloSize, out));
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::AppHello), static_cast<int>(out.kind));
    TEST_ASSERT_EQUAL_UINT8(kProtocolVersion, out.appHello.version);
    TEST_ASSERT_EQUAL_UINT8(kRoleController, out.appHello.role);

    Message input;
    input.kind = MessageKind::Input;
    input.input.seq = 0x4242;
    input.input.buttons = kButtonRight;
    input.input.heldMs = 40;
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kInputSize), sizeOf(encode(input, buffer, sizeof(buffer))));
    TEST_ASSERT_TRUE(decode(buffer, kInputSize, out));
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Input), static_cast<int>(out.kind));
    TEST_ASSERT_EQUAL_UINT16(0x4242, out.input.seq);
    TEST_ASSERT_EQUAL_UINT8(kButtonRight, out.input.buttons);
    TEST_ASSERT_EQUAL_UINT8(40, out.input.heldMs);

    Message state;
    state.kind = MessageKind::State;
    state.state.seq = 0x7777;
    state.state.phase = kPhasePoint;
    state.state.ballX = -300;
    state.state.ballY = 300;
    state.state.score1 = 4;
    state.state.score2 = 4;
    state.state.flags = 0x02;
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kStateSize), sizeOf(encode(state, buffer, sizeof(buffer))));
    TEST_ASSERT_TRUE(decode(buffer, kStateSize, out));
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::State), static_cast<int>(out.kind));
    TEST_ASSERT_EQUAL_UINT16(0x7777, out.state.seq);
    TEST_ASSERT_EQUAL_UINT8(kPhasePoint, out.state.phase);
    TEST_ASSERT_EQUAL_INT16(-300, out.state.ballX);
    TEST_ASSERT_EQUAL_INT16(300, out.state.ballY);
    TEST_ASSERT_EQUAL_UINT8(4, out.state.score1);
    TEST_ASSERT_EQUAL_UINT8(0x02, out.state.flags);

    Message point;
    point.kind = MessageKind::Point;
    point.point.seq = 3;
    point.point.scorer = kRoleHost;
    point.point.score1 = 5;
    point.point.score2 = 2;
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kPointSize), sizeOf(encode(point, buffer, sizeof(buffer))));
    TEST_ASSERT_TRUE(decode(buffer, kPointSize, out));
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Point), static_cast<int>(out.kind));
    TEST_ASSERT_EQUAL_UINT8(kRoleHost, out.point.scorer);
    TEST_ASSERT_EQUAL_UINT8(5, out.point.score1);
    TEST_ASSERT_EQUAL_UINT8(2, out.point.score2);

    Message roundStart;
    roundStart.kind = MessageKind::RoundStart;
    roundStart.roundStart.seq = 0x00FF;
    roundStart.roundStart.serveDir = -1;
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kRoundStartSize), sizeOf(encode(roundStart, buffer, sizeof(buffer))));
    TEST_ASSERT_TRUE(decode(buffer, kRoundStartSize, out));
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::RoundStart), static_cast<int>(out.kind));
    TEST_ASSERT_EQUAL_UINT16(0x00FF, out.roundStart.seq);
    TEST_ASSERT_EQUAL_INT8(-1, out.roundStart.serveDir);

    Message pause;
    pause.kind = MessageKind::Pause;
    pause.pause.paused = 1;
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kPauseSize), sizeOf(encode(pause, buffer, sizeof(buffer))));
    TEST_ASSERT_TRUE(decode(buffer, kPauseSize, out));
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Pause), static_cast<int>(out.kind));
    TEST_ASSERT_EQUAL_UINT8(1, out.pause.paused);

    Message matchEnd;
    matchEnd.kind = MessageKind::MatchEnd;
    matchEnd.matchEnd.winner = kRoleController;
    matchEnd.matchEnd.score1 = 5;
    matchEnd.matchEnd.score2 = 5;
    TEST_ASSERT_EQUAL_UINT32(sizeOf(kMatchEndSize), sizeOf(encode(matchEnd, buffer, sizeof(buffer))));
    TEST_ASSERT_TRUE(decode(buffer, kMatchEndSize, out));
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::MatchEnd), static_cast<int>(out.kind));
    TEST_ASSERT_EQUAL_UINT8(kRoleController, out.matchEnd.winner);
    TEST_ASSERT_EQUAL_UINT8(5, out.matchEnd.score1);
}

void test_aggregate_decode_rejects_unknown_tag_short_frame_and_null(void) {
    Message out;

    const std::uint8_t unknown[4] = {kUnknownTag, 0, 0, 0};
    TEST_ASSERT_FALSE(decode(unknown, sizeof(unknown), out));
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Unknown), static_cast<int>(out.kind));

    const std::uint8_t shortState[2] = {kTagState, 0x01};
    TEST_ASSERT_FALSE(decode(shortState, sizeof(shortState), out));

    const std::uint8_t shortPause[1] = {kTagPause};
    TEST_ASSERT_FALSE(decode(shortPause, sizeof(shortPause), out));

    TEST_ASSERT_FALSE(decode(nullptr, kPauseSize, out));
    TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Unknown), static_cast<int>(out.kind));
}

void test_aggregate_encode_refuses_unknown_kind(void) {
    Message message;
    message.kind = MessageKind::Unknown;

    std::uint8_t buffer[kStateSize];
    fill(buffer, sizeof(buffer), kSentinel);

    TEST_ASSERT_EQUAL_UINT32(0, sizeOf(encode(message, buffer, sizeof(buffer))));
    assertSentinel(buffer, sizeof(buffer), kSentinel);

    message.kind = static_cast<MessageKind>(200);
    TEST_ASSERT_EQUAL_UINT32(0, sizeOf(encode(message, buffer, sizeof(buffer))));
    assertSentinel(buffer, sizeof(buffer), kSentinel);
}

// -----------------------------------------------------------------------------
// Sequence and version helpers
// -----------------------------------------------------------------------------

void test_is_newer_sequence_wraps_past_ffff(void) {
    TEST_ASSERT_TRUE(isNewerSequence(0x0000, 0xFFFF));
    TEST_ASSERT_FALSE(isNewerSequence(0xFFFF, 0x0000));
    TEST_ASSERT_TRUE(isNewerSequence(101, 100));
    TEST_ASSERT_FALSE(isNewerSequence(100, 101));
    TEST_ASSERT_FALSE(isNewerSequence(100, 100));
}

void test_is_newer_sequence_treats_the_half_range_as_ambiguous(void) {
    // The RFC1982 window is half open: a distance of exactly 0x8000 is not an
    // ordering in either direction, so both calls must refuse.
    TEST_ASSERT_FALSE(isNewerSequence(0x8000, 0x0000));
    TEST_ASSERT_FALSE(isNewerSequence(0x0000, 0x8000));
    TEST_ASSERT_TRUE(isNewerSequence(0x7FFF, 0x0000));
    TEST_ASSERT_FALSE(isNewerSequence(0x8001, 0x0000));
}

void test_is_compatible_version_accepts_only_the_current_version(void) {
    TEST_ASSERT_TRUE(isCompatibleVersion(kProtocolVersion));
    TEST_ASSERT_TRUE(isCompatibleVersion(1));
    TEST_ASSERT_FALSE(isCompatibleVersion(0));
    TEST_ASSERT_FALSE(isCompatibleVersion(2));
    TEST_ASSERT_FALSE(isCompatibleVersion(255));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_contract_constants_match_the_documented_wire);

    RUN_TEST(test_encode_app_hello_pins_the_wire_bytes);
    RUN_TEST(test_encode_input_pins_the_wire_bytes);
    RUN_TEST(test_encode_state_pins_the_wire_bytes);
    RUN_TEST(test_encode_point_pins_the_wire_bytes);
    RUN_TEST(test_encode_round_start_pins_the_wire_bytes);
    RUN_TEST(test_encode_pause_pins_the_wire_bytes);
    RUN_TEST(test_encode_match_end_pins_the_wire_bytes);

    RUN_TEST(test_app_hello_round_trips_version_and_role);
    RUN_TEST(test_input_round_trips_sequence_wrap_and_buttons);
    RUN_TEST(test_state_round_trips_extreme_fields);
    RUN_TEST(test_state_round_trips_every_phase);
    RUN_TEST(test_point_round_trips_scores);
    RUN_TEST(test_round_start_round_trips_signed_serve_direction);
    RUN_TEST(test_pause_round_trips_both_states);
    RUN_TEST(test_match_end_round_trips_winner_and_scores);

    RUN_TEST(test_app_hello_decoder_rejects_malformed_frames);
    RUN_TEST(test_input_decoder_rejects_malformed_frames);
    RUN_TEST(test_state_decoder_rejects_malformed_frames);
    RUN_TEST(test_point_decoder_rejects_malformed_frames);
    RUN_TEST(test_round_start_decoder_rejects_malformed_frames);
    RUN_TEST(test_pause_decoder_rejects_malformed_frames);
    RUN_TEST(test_match_end_decoder_rejects_malformed_frames);

    RUN_TEST(test_app_hello_encoder_needs_full_capacity);
    RUN_TEST(test_input_encoder_needs_full_capacity);
    RUN_TEST(test_state_encoder_needs_full_capacity);
    RUN_TEST(test_point_encoder_needs_full_capacity);
    RUN_TEST(test_round_start_encoder_needs_full_capacity);
    RUN_TEST(test_pause_encoder_needs_full_capacity);
    RUN_TEST(test_match_end_encoder_needs_full_capacity);

    RUN_TEST(test_kind_of_maps_every_known_tag);
    RUN_TEST(test_kind_of_rejects_unknown_tag_and_empty_frame);
    RUN_TEST(test_kind_name_is_stable_for_logs);

    RUN_TEST(test_aggregate_round_trips_every_kind);
    RUN_TEST(test_aggregate_decode_rejects_unknown_tag_short_frame_and_null);
    RUN_TEST(test_aggregate_encode_refuses_unknown_kind);

    RUN_TEST(test_is_newer_sequence_wraps_past_ffff);
    RUN_TEST(test_is_newer_sequence_treats_the_half_range_as_ambiguous);
    RUN_TEST(test_is_compatible_version_accepts_only_the_current_version);

    return UNITY_END();
}
