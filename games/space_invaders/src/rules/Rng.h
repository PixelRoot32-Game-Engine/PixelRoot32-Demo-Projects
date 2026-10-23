#pragma once

#include <cstdint>

namespace spaceinvaders::rules {

/**
 * @file Rng.h
 * @brief Seeded generator behind a one-function interface (S20, F25).
 *
 * The scene used to draw columns from the process-wide std::rand(), which no
 * test could reproduce. This object owns its state instead: same seed, same
 * sequence, and no allocation or global state.
 */

/**
 * @class Rng
 * @brief Deterministic 32-bit generator with a bounded draw.
 *
 * Header-only on purpose: every rule that needs randomness takes an Rng by
 * reference, so the layer stays free of a translation-unit pairing that the
 * tests would have to link.
 */
class Rng {
public:
    /**
     * @brief Seeds the generator.
     * @param seed Initial state; 0x1978 is the default run seed.
     */
    explicit Rng(uint32_t seed = 0x1978u) : state(seed) {}

    /**
     * @brief Produces the next value in the sequence.
     * @return Next 32-bit value.
     */
    uint32_t next() {
        return 0;  // RED: implemented in T1.3
    }

    /**
     * @brief Produces a value in [0, bound).
     *
     * The bound must be greater than zero; callers own that precondition
     * because the rules never ask for a zero-sized range.
     *
     * @param bound Exclusive upper bound.
     * @return Value below bound.
     */
    uint32_t below(uint32_t bound) {
        (void)bound;
        return 0;  // RED: implemented in T1.3
    }

    /**
     * @brief Replaces the state, restarting the sequence.
     * @param seed New state.
     */
    void reseed(uint32_t seed) {
        state = seed;
        // RED: implemented in T1.3
    }

private:
    uint32_t state;
};

}  // namespace spaceinvaders::rules
