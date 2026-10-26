/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */

#include "net/PongProtocol.h"

namespace netpong::net {
namespace {

/// Writes a 16-bit word low byte first, which is the only byte order any frame
/// in this protocol uses.
void writeU16(std::uint8_t* out, std::uint16_t value) {
    out[0] = static_cast<std::uint8_t>(value & 0xFFu);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

/// Writes a signed 16-bit word by reinterpreting its two's-complement bits; the
/// wire carries the bit pattern, so this is exact for every int16_t value.
void writeI16(std::uint8_t* out, std::int16_t value) {
    writeU16(out, static_cast<std::uint16_t>(value));
}

/// Reads a 16-bit little-endian word.
std::uint16_t readU16(const std::uint8_t* in) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[0]) |
                                      static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[1]) << 8));
}

/// Reads a signed 16-bit little-endian word.
///
/// The negative range is reconstructed arithmetically rather than by casting a
/// large unsigned value down to int16_t, which is implementation-defined before
/// C++20; this form is exact on every compiler the demo targets.
std::int16_t readI16(const std::uint8_t* in) {
    const std::uint16_t raw = readU16(in);
    if (raw < 0x8000u) {
        return static_cast<std::int16_t>(raw);
    }
    return static_cast<std::int16_t>(static_cast<std::int32_t>(raw) - 0x10000);
}

/// Reads a signed 8-bit payload byte, with the same portable sign reconstruction
/// as readI16.
std::int8_t readI8(const std::uint8_t* in) {
    const std::uint8_t raw = in[0];
    if (raw < 0x80u) {
        return static_cast<std::int8_t>(raw);
    }
    return static_cast<std::int8_t>(static_cast<std::int32_t>(raw) - 0x100);
}

}  // namespace

std::size_t encodeAppHello(const AppHello& message, std::uint8_t* out, std::size_t capacity) {
    if (out == nullptr || capacity < kAppHelloSize) {
        return 0;
    }

    out[0] = kTagAppHello;
    out[1] = message.version;
    out[2] = message.role;
    return kAppHelloSize;
}

bool decodeAppHello(const std::uint8_t* in, std::size_t len, AppHello& out) {
    if (in == nullptr || len != kAppHelloSize || in[0] != kTagAppHello) {
        return false;
    }

    // Built in a local and assigned only at the end, so a future early return
    // cannot leave the caller's struct half decoded.
    AppHello decoded;
    decoded.version = in[1];
    decoded.role = in[2];
    out = decoded;
    return true;
}

std::size_t encodeInput(const Input& message, std::uint8_t* out, std::size_t capacity) {
    if (out == nullptr || capacity < kInputSize) {
        return 0;
    }

    out[0] = kTagInput;
    writeU16(out + 1, message.seq);
    out[3] = message.buttons;
    out[4] = message.heldMs;
    return kInputSize;
}

bool decodeInput(const std::uint8_t* in, std::size_t len, Input& out) {
    if (in == nullptr || len != kInputSize || in[0] != kTagInput) {
        return false;
    }

    Input decoded;
    decoded.seq = readU16(in + 1);
    decoded.buttons = in[3];
    decoded.heldMs = in[4];
    out = decoded;
    return true;
}

std::size_t encodeState(const State& message, std::uint8_t* out, std::size_t capacity) {
    if (out == nullptr || capacity < kStateSize) {
        return 0;
    }

    out[0] = kTagState;
    writeU16(out + 1, message.seq);
    out[3] = message.phase;
    writeI16(out + 4, message.ballX);
    writeI16(out + 6, message.ballY);
    writeI16(out + 8, message.ballVX);
    writeI16(out + 10, message.ballVY);
    writeI16(out + 12, message.p1Y);
    writeI16(out + 14, message.p2Y);
    out[16] = message.score1;
    out[17] = message.score2;
    out[18] = message.flags;
    return kStateSize;
}

bool decodeState(const std::uint8_t* in, std::size_t len, State& out) {
    if (in == nullptr || len != kStateSize || in[0] != kTagState) {
        return false;
    }

    State decoded;
    decoded.seq = readU16(in + 1);
    decoded.phase = in[3];
    decoded.ballX = readI16(in + 4);
    decoded.ballY = readI16(in + 6);
    decoded.ballVX = readI16(in + 8);
    decoded.ballVY = readI16(in + 10);
    decoded.p1Y = readI16(in + 12);
    decoded.p2Y = readI16(in + 14);
    decoded.score1 = in[16];
    decoded.score2 = in[17];
    decoded.flags = in[18];
    out = decoded;
    return true;
}

std::size_t encodePoint(const Point& message, std::uint8_t* out, std::size_t capacity) {
    if (out == nullptr || capacity < kPointSize) {
        return 0;
    }

    out[0] = kTagPoint;
    writeU16(out + 1, message.seq);
    out[3] = message.scorer;
    out[4] = message.score1;
    out[5] = message.score2;
    return kPointSize;
}

bool decodePoint(const std::uint8_t* in, std::size_t len, Point& out) {
    if (in == nullptr || len != kPointSize || in[0] != kTagPoint) {
        return false;
    }

    Point decoded;
    decoded.seq = readU16(in + 1);
    decoded.scorer = in[3];
    decoded.score1 = in[4];
    decoded.score2 = in[5];
    out = decoded;
    return true;
}

std::size_t encodeRoundStart(const RoundStart& message, std::uint8_t* out, std::size_t capacity) {
    if (out == nullptr || capacity < kRoundStartSize) {
        return 0;
    }

    out[0] = kTagRoundStart;
    writeU16(out + 1, message.seq);
    out[3] = static_cast<std::uint8_t>(message.serveDir);
    return kRoundStartSize;
}

bool decodeRoundStart(const std::uint8_t* in, std::size_t len, RoundStart& out) {
    if (in == nullptr || len != kRoundStartSize || in[0] != kTagRoundStart) {
        return false;
    }

    RoundStart decoded;
    decoded.seq = readU16(in + 1);
    decoded.serveDir = readI8(in + 3);
    out = decoded;
    return true;
}

std::size_t encodePause(const Pause& message, std::uint8_t* out, std::size_t capacity) {
    if (out == nullptr || capacity < kPauseSize) {
        return 0;
    }

    out[0] = kTagPause;
    out[1] = message.paused;
    return kPauseSize;
}

bool decodePause(const std::uint8_t* in, std::size_t len, Pause& out) {
    if (in == nullptr || len != kPauseSize || in[0] != kTagPause) {
        return false;
    }

    Pause decoded;
    decoded.paused = in[1];
    out = decoded;
    return true;
}

std::size_t encodeMatchEnd(const MatchEnd& message, std::uint8_t* out, std::size_t capacity) {
    if (out == nullptr || capacity < kMatchEndSize) {
        return 0;
    }

    out[0] = kTagMatchEnd;
    out[1] = message.winner;
    out[2] = message.score1;
    out[3] = message.score2;
    return kMatchEndSize;
}

bool decodeMatchEnd(const std::uint8_t* in, std::size_t len, MatchEnd& out) {
    if (in == nullptr || len != kMatchEndSize || in[0] != kTagMatchEnd) {
        return false;
    }

    MatchEnd decoded;
    decoded.winner = in[1];
    decoded.score1 = in[2];
    decoded.score2 = in[3];
    out = decoded;
    return true;
}

std::size_t encode(const Message& message, std::uint8_t* out, std::size_t capacity) {
    switch (message.kind) {
        case MessageKind::AppHello:
            return encodeAppHello(message.appHello, out, capacity);
        case MessageKind::Input:
            return encodeInput(message.input, out, capacity);
        case MessageKind::State:
            return encodeState(message.state, out, capacity);
        case MessageKind::Point:
            return encodePoint(message.point, out, capacity);
        case MessageKind::RoundStart:
            return encodeRoundStart(message.roundStart, out, capacity);
        case MessageKind::Pause:
            return encodePause(message.pause, out, capacity);
        case MessageKind::MatchEnd:
            return encodeMatchEnd(message.matchEnd, out, capacity);
        case MessageKind::Unknown:
            break;
    }
    return 0;
}

bool decode(const std::uint8_t* in, std::size_t len, Message& out) {
    if (in == nullptr) {
        return false;
    }

    Message decoded;
    switch (kindOf(in, len)) {
        case MessageKind::AppHello:
            if (!decodeAppHello(in, len, decoded.appHello)) {
                return false;
            }
            decoded.kind = MessageKind::AppHello;
            break;
        case MessageKind::Input:
            if (!decodeInput(in, len, decoded.input)) {
                return false;
            }
            decoded.kind = MessageKind::Input;
            break;
        case MessageKind::State:
            if (!decodeState(in, len, decoded.state)) {
                return false;
            }
            decoded.kind = MessageKind::State;
            break;
        case MessageKind::Point:
            if (!decodePoint(in, len, decoded.point)) {
                return false;
            }
            decoded.kind = MessageKind::Point;
            break;
        case MessageKind::RoundStart:
            if (!decodeRoundStart(in, len, decoded.roundStart)) {
                return false;
            }
            decoded.kind = MessageKind::RoundStart;
            break;
        case MessageKind::Pause:
            if (!decodePause(in, len, decoded.pause)) {
                return false;
            }
            decoded.kind = MessageKind::Pause;
            break;
        case MessageKind::MatchEnd:
            if (!decodeMatchEnd(in, len, decoded.matchEnd)) {
                return false;
            }
            decoded.kind = MessageKind::MatchEnd;
            break;
        case MessageKind::Unknown:
        default:
            return false;
    }

    out = decoded;
    return true;
}

MessageKind kindOf(const std::uint8_t* in, std::size_t len) {
    if (in == nullptr || len < 1) {
        return MessageKind::Unknown;
    }

    switch (in[0]) {
        case kTagAppHello:
            return MessageKind::AppHello;
        case kTagInput:
            return MessageKind::Input;
        case kTagState:
            return MessageKind::State;
        case kTagPoint:
            return MessageKind::Point;
        case kTagRoundStart:
            return MessageKind::RoundStart;
        case kTagPause:
            return MessageKind::Pause;
        case kTagMatchEnd:
            return MessageKind::MatchEnd;
        default:
            return MessageKind::Unknown;
    }
}

const char* kindName(MessageKind kind) {
    switch (kind) {
        case MessageKind::AppHello:
            return "AppHello";
        case MessageKind::Input:
            return "Input";
        case MessageKind::State:
            return "State";
        case MessageKind::Point:
            return "Point";
        case MessageKind::RoundStart:
            return "RoundStart";
        case MessageKind::Pause:
            return "Pause";
        case MessageKind::MatchEnd:
            return "MatchEnd";
        case MessageKind::Unknown:
            break;
    }
    return "Unknown";
}

bool isNewerSequence(std::uint16_t candidate, std::uint16_t reference) {
    // RFC 1982 serial arithmetic: the subtraction is taken in 16 bits, so the
    // wrap at 0xFFFF costs nothing, and the half-open window rejects the
    // ambiguous 0x8000 distance in both directions.
    return candidate != reference && static_cast<std::uint16_t>(candidate - reference) < 0x8000;
}

bool isCompatibleVersion(std::uint8_t version) {
    return version == kProtocolVersion;
}

}  // namespace netpong::net
