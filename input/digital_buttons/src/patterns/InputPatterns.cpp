/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "patterns/InputPatterns.h"

namespace digital_buttons {

// =============================================================================
// RepeatTimer
// =============================================================================

RepeatTimer::RepeatTimer(unsigned long initialDelayMs, unsigned long intervalMs)
    : initialDelayMs_(initialDelayMs),
      // A zero interval would make the carry arithmetic below divide by zero,
      // and "repeat every 0 ms" has no useful meaning anyway.
      intervalMs_(intervalMs == 0 ? 1UL : intervalMs),
      remaining_(0),
      wasHeld_(false) {}

bool RepeatTimer::tick(bool held, unsigned long dt) {
    if (!held) {
        reset();
        return false;
    }

    if (!wasHeld_) {
        // Rising edge: fire now so the first press feels instant, then owe the
        // full initial delay before the train starts.
        wasHeld_ = true;
        remaining_ = initialDelayMs_;
        return true;
    }

    if (dt < remaining_) {
        remaining_ -= dt;
        return false;
    }

    // The frame reached or passed the deadline. Report exactly one repeat and
    // carry the surplus into the next interval so a long frame shifts the
    // train rather than stretching it.
    unsigned long carry = dt - remaining_;
    carry %= intervalMs_;
    remaining_ = intervalMs_ - carry;
    return true;
}

void RepeatTimer::reset() {
    wasHeld_ = false;
    remaining_ = 0;
}

// =============================================================================
// InputBuffer
// =============================================================================

InputBuffer::InputBuffer(unsigned long windowMs)
    : windowMs_(windowMs), pressTime_(0), armed_(false) {}

void InputBuffer::press(unsigned long now) {
    pressTime_ = now;
    armed_ = true;
}

bool InputBuffer::pending(unsigned long now) const {
    if (!armed_) return false;
    // now is monotonic in the game loop, but guard the subtraction anyway:
    // unsigned wraparound would turn a stale press into an eternal one.
    if (now < pressTime_) return true;
    return (now - pressTime_) <= windowMs_;
}

bool InputBuffer::consume(unsigned long now) {
    if (!armed_) return false;

    const bool fresh = pending(now);
    // Armed either way, the press is spent: a stale one is dropped here rather
    // than left to surprise a later consumer.
    armed_ = false;
    return fresh;
}

void InputBuffer::clear() {
    armed_ = false;
}

// =============================================================================
// ComboDetector
// =============================================================================

ComboDetector::ComboDetector(unsigned long toleranceMs)
    : toleranceMs_(toleranceMs),
      sinceA_(0),
      sinceB_(0),
      haveA_(false),
      haveB_(false) {}

bool ComboDetector::feed(bool aEdge, bool bEdge, unsigned long dt) {
    // Age first, then record: an edge fed with a dt larger than the tolerance
    // has already expired by the time its partner is looked at this frame.
    if (haveA_) {
        sinceA_ += dt;
        if (sinceA_ > toleranceMs_) haveA_ = false;
    }
    if (haveB_) {
        sinceB_ += dt;
        if (sinceB_ > toleranceMs_) haveB_ = false;
    }

    if (aEdge) {
        haveA_ = true;
        sinceA_ = 0;
    }
    if (bEdge) {
        haveB_ = true;
        sinceB_ = 0;
    }

    if (haveA_ && haveB_) {
        // One combo per pair: both slots are spent by the fire.
        reset();
        return true;
    }

    return false;
}

void ComboDetector::reset() {
    haveA_ = false;
    haveB_ = false;
    sinceA_ = 0;
    sinceB_ = 0;
}

// =============================================================================
// EdgeRateMeter
// =============================================================================

EdgeRateMeter::EdgeRateMeter() : stamps_{}, head_(0), count_(0), peak_(0) {}

void EdgeRateMeter::edge(unsigned long now) {
    stamps_[head_] = now;
    head_ = static_cast<uint8_t>((head_ + 1) % kCapacity);
    if (count_ < kCapacity) {
        ++count_;
    }
    // Overwriting the oldest slot is the intended behaviour: kCapacity edges
    // inside one second is already past the rate the debounce allows.
}

uint8_t EdgeRateMeter::ratePerSecond(unsigned long now) {
    uint8_t rate = 0;

    for (uint8_t i = 0; i < count_; ++i) {
        const unsigned long stamp = stamps_[i];
        if (now < stamp) continue;              // never happens in the loop; cheap guard
        if ((now - stamp) < kWindowMs) ++rate;  // trailing window, open at the far end
    }

    if (rate > peak_) {
        peak_ = rate;
    }
    return rate;
}

void EdgeRateMeter::reset() {
    head_ = 0;
    count_ = 0;
    peak_ = 0;
    for (uint8_t i = 0; i < kCapacity; ++i) {
        stamps_[i] = 0;
    }
}

} // namespace digital_buttons
