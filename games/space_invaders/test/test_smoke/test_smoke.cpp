/*
 * Environment smoke test for host_test (S21).
 *
 * Proves the native toolchain really is C++17 and that the standard integer
 * widths the engine-free rules code will rely on hold. A wrong standard mode
 * or a broken toolchain fails here instead of silently building the suite.
 */
#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <limits>

static constexpr std::size_t UINT16_BYTES = 2;

void setUp(void) {}
void tearDown(void) {}

void test_standard_is_cplusplus17_or_newer(void) {
    TEST_ASSERT_GREATER_OR_EQUAL(201703, static_cast<int>(__cplusplus));
}

void test_uint16_t_is_two_bytes(void) {
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(UINT16_BYTES),
                             static_cast<std::uint32_t>(sizeof(std::uint16_t)));
}

void test_uint16_t_range_is_65535(void) {
    TEST_ASSERT_EQUAL_UINT32(65535U, static_cast<std::uint32_t>(
                                         std::numeric_limits<std::uint16_t>::max()));
}

void test_uint32_t_is_four_bytes(void) {
    TEST_ASSERT_EQUAL_UINT32(4U, static_cast<std::uint32_t>(sizeof(std::uint32_t)));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_standard_is_cplusplus17_or_newer);
    RUN_TEST(test_uint16_t_is_two_bytes);
    RUN_TEST(test_uint16_t_range_is_65535);
    RUN_TEST(test_uint32_t_is_four_bytes);

    return UNITY_END();
}
