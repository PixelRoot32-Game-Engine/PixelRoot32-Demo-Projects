/*
 * RED suite for the seeded generator (S20).
 *
 * A constant generator would satisfy "same seed, same sequence" and
 * "below(n) < n" trivially, so each test also demands that the sequence is not
 * degenerate. That is what makes these tests fail against the return-0 stub
 * instead of passing vacuously.
 */
#include <unity.h>

#include "rules/Rng.h"

using namespace spaceinvaders::rules;

void setUp(void) {}
void tearDown(void) {}

void test_rng_same_seed_reproduces_a_non_constant_sequence(void) {
    Rng first(0x1978u);
    Rng second(0x1978u);

    const uint32_t head = first.next();
    TEST_ASSERT_EQUAL_UINT32(head, second.next());

    bool sawDifferentValue = false;
    for (int draw = 0; draw < 15; ++draw) {
        const uint32_t value = first.next();
        TEST_ASSERT_EQUAL_UINT32(value, second.next());
        if (value != head) {
            sawDifferentValue = true;
        }
    }

    TEST_ASSERT_TRUE(sawDifferentValue);
}

void test_rng_different_seeds_produce_different_sequences(void) {
    Rng first(1u);
    Rng second(2u);

    bool differs = false;
    for (int draw = 0; draw < 16; ++draw) {
        if (first.next() != second.next()) {
            differs = true;
        }
    }

    TEST_ASSERT_TRUE(differs);
}

void test_rng_below_stays_inside_the_bound(void) {
    Rng rng(0x1978u);
    const uint32_t bound = 11;

    bool seen[bound] = {};
    int distinct = 0;
    for (int draw = 0; draw < 1000; ++draw) {
        const uint32_t value = rng.below(bound);
        TEST_ASSERT_LESS_THAN_UINT32(bound, value);
        if (!seen[value]) {
            seen[value] = true;
            ++distinct;
        }
    }

    // A constant draw reaches "value < bound" but never reaches this.
    TEST_ASSERT_GREATER_OR_EQUAL_INT(5, distinct);
}

void test_rng_reseed_reproduces_a_non_constant_sequence(void) {
    Rng rng(0x1978u);

    uint32_t expected[8] = {};
    for (int draw = 0; draw < 8; ++draw) {
        expected[draw] = rng.next();
    }

    rng.reseed(0x1978u);

    bool sawDifferentValue = false;
    for (int draw = 0; draw < 8; ++draw) {
        const uint32_t value = rng.next();
        TEST_ASSERT_EQUAL_UINT32(expected[draw], value);
        if (value != expected[0]) {
            sawDifferentValue = true;
        }
    }

    TEST_ASSERT_TRUE(sawDifferentValue);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_rng_same_seed_reproduces_a_non_constant_sequence);
    RUN_TEST(test_rng_different_seeds_produce_different_sequences);
    RUN_TEST(test_rng_below_stays_inside_the_bound);
    RUN_TEST(test_rng_reseed_reproduces_a_non_constant_sequence);

    return UNITY_END();
}
