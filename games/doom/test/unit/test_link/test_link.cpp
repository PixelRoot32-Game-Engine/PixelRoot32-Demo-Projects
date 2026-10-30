/*
 * Behaviour tests for the C <-> C++ seam of the DOOM port.
 *
 * D1.5 asks one question: can a C translation unit that will hold the DOOM core
 * call into the C++ engine and back, in both shipped builds, under the project's
 * compiler flags? The build answers half of it -- if the seam did not link, no
 * environment would produce a binary. These tests answer the other half: the
 * symbols the C header declares are the symbols the C++ side actually defines,
 * and a value that crosses the boundary arrives unchanged in both directions.
 *
 * Three properties are pinned, all through the public C API and never through
 * the implementation's own state:
 *
 *  - the checksum is FNV-1a over the table this header documents, stated here
 *    from the contract (an ASCII literal and the FNV constants), so a mistyped
 *    byte in the C table fails a test instead of agreeing with itself;
 *  - `doom_bridge_mix()` applies the salt this header declares, and nothing
 *    else, so a truncated or byte-swapped crossing cannot pass;
 *  - the C++ side can reach a symbol the port owns (`palette::indexTable()`),
 *    not only the engine, and the palette contract it pins holds through the
 *    boundary.
 *
 * Every `doom_bridge_*` entry point the header declares is called at least
 * once -- that is the declaration/definition agreement check: a declaration
 * without a definition is a link error here, and a definition without a
 * declaration is an unreachable symbol nothing can call.
 *
 * This environment installs no library dependency and `lib/doom` is compiled by
 * the C compiler, so the tests also prove the seam stayed free of engine
 * headers: the same bridge TU finds no `<platforms/PlatformLog.h>` here and
 * falls back to stdio.
 */
#include <cstdint>

#include <unity.h>

#include "doom_link_probe.h"

#include "../../test_config.h"

namespace {

/// The documented table, as a string: 30 printable bytes ("pixelroot32" is 11,
/// "/doom-link-probe" is 16, "/v1" is 3) plus the 2 zero bytes the C file's
/// 32-entry table ends with. Written as text here on purpose -- the C side
/// spells it as character constants, so the two cannot share a typo.
const char kProbeTable[] = "pixelroot32/doom-link-probe/v1";
constexpr std::uint32_t kProbeTablePrintedBytes = 30u;
constexpr std::uint32_t kProbeTableZeroBytes = 2u;
constexpr std::uint32_t kProbeTableBytes = kProbeTablePrintedBytes + kProbeTableZeroBytes;

/// FNV-1a, as the header documents it: 32-bit offset basis and prime.
constexpr std::uint32_t kFnvOffsetBasis = 2166136261u;
constexpr std::uint32_t kFnvPrime = 16777619u;

/// The checksum the documented definition produces for the shipped parameters,
/// computed independently of the implementation (one literal per line of the
/// contract). 0xD14EAEF7 is the zero-pass boundary, 0x3E32AC08 one pass and
/// 0x5B5283C7 the shipped eight passes.
constexpr std::uint32_t kShippedChecksum = 0x5B5283C7u;

/// The documented definition, evaluated here in C++ over the printed table.
std::uint32_t expectedChecksum(std::uint32_t seed, std::uint32_t iterations) {
    std::uint32_t hash = kFnvOffsetBasis ^ seed;
    for (std::uint32_t pass = 0; pass < iterations; ++pass) {
        for (std::uint32_t index = 0; index < kProbeTableBytes; ++index) {
            const std::uint32_t byte = index < kProbeTablePrintedBytes
                                           ? static_cast<std::uint8_t>(kProbeTable[index])
                                           : 0u;
            hash = hash ^ byte;
            hash = hash * kFnvPrime;
        }
    }
    return hash;
}

}  // namespace

// The same arguments always produce the same answer, and the answer actually
// depends on both of them: a probe whose checksum ignored its seed would pass
// the round trip while testing nothing.
void test_checksum_is_deterministic_and_depends_on_both_arguments(void) {
    TEST_ASSERT_EQUAL_UINT32(doom_link_probe_checksum(DOOM_LINK_PROBE_SEED, 3u),
                             doom_link_probe_checksum(DOOM_LINK_PROBE_SEED, 3u));

    TEST_ASSERT_TRUE(doom_link_probe_checksum(1u, 3u) != doom_link_probe_checksum(2u, 3u));
    TEST_ASSERT_TRUE(doom_link_probe_checksum(1u, 3u) != doom_link_probe_checksum(1u, 4u));
}

// Zero passes is the documented boundary of the loop, not a special case: the
// answer is the offset basis XOR the seed, with no table byte mixed in yet.
void test_zero_iterations_returns_the_offset_basis_xor_seed(void) {
    TEST_ASSERT_EQUAL_UINT32(0xD14EAEF7u,
                             doom_link_probe_checksum(DOOM_LINK_PROBE_SEED, 0u));
    TEST_ASSERT_EQUAL_UINT32(0x811C9DC5u, doom_link_probe_checksum(0u, 0u));
}

// The checksum against the definition rather than against a copy of itself: a
// wrong byte in the C table, a wrong prime or a lost pass fails here.
void test_checksum_matches_the_documented_fnv1a_definition(void) {
    TEST_ASSERT_EQUAL_UINT32(0x3E32AC08u,
                             doom_link_probe_checksum(DOOM_LINK_PROBE_SEED, 1u));
    TEST_ASSERT_EQUAL_UINT32(expectedChecksum(DOOM_LINK_PROBE_SEED, 1u),
                             doom_link_probe_checksum(DOOM_LINK_PROBE_SEED, 1u));
    TEST_ASSERT_EQUAL_UINT32(expectedChecksum(0u, 2u), doom_link_probe_checksum(0u, 2u));
    TEST_ASSERT_EQUAL_UINT32(expectedChecksum(0xFFFFFFFFu, 5u),
                             doom_link_probe_checksum(0xFFFFFFFFu, 5u));
}

// The number the boot line prints for the shipped probe. Pinned so a refactor
// of the C file cannot quietly change what a serial log from yesterday means.
void test_shipped_checksum_is_the_pinned_value(void) {
    TEST_ASSERT_EQUAL_UINT32(kShippedChecksum,
                             doom_link_probe_checksum(DOOM_LINK_PROBE_SEED,
                                                      DOOM_LINK_PROBE_ITERATIONS));
}

// The C++ side applies the salt the header declares and nothing else; the C
// side's verification of the return path is exactly this comparison. Every bit
// of the input is covered, including both ends of the range.
void test_bridge_mix_applies_the_shared_salt(void) {
    const std::uint32_t values[] = {0u, 1u, 0x12345678u, 0xFFFFFFFFu, kShippedChecksum};
    for (std::uint32_t value : values) {
        TEST_ASSERT_EQUAL_UINT32(value ^ DOOM_LINK_SALT, doom_bridge_mix(value));
    }

    // The same value through the C allocator and back twice: a salt applied in
    // one direction only (or twice) cannot satisfy this.
    const std::uint32_t mixed = doom_bridge_mix(kShippedChecksum);
    TEST_ASSERT_EQUAL_UINT32(kShippedChecksum, mixed ^ DOOM_LINK_SALT);
}

// The C++ side reaches a symbol this port defines, not a stub inside the
// bridge: index 0 is black and index 15 white in the palette contract pinned by
// src/palette/DoomPalette.cpp.
void test_bridge_palette_byte_reads_the_port_table(void) {
    TEST_ASSERT_EQUAL_UINT32(DOOM_LINK_PALETTE_BLACK_BYTE,
                             doom_bridge_palette_byte(DOOM_LINK_PALETTE_BLACK_INDEX));
    TEST_ASSERT_EQUAL_UINT32(DOOM_LINK_PALETTE_WHITE_BYTE,
                             doom_bridge_palette_byte(DOOM_LINK_PALETTE_WHITE_INDEX));
    TEST_ASSERT_EQUAL_UINT32(0u, doom_bridge_palette_byte(256u));
}

// The whole probe, end to end: the C side computes, crosses, verifies and
// reports. It also prints the one `LINK,...` record the boot line is made of.
void test_probe_run_passes_and_reports_ok(void) {
    TEST_ASSERT_EQUAL_INT(DOOM_LINK_STATUS_OK, doom_link_probe_run());
}

// Declaration and definition agreement: every symbol the header lists is called
// here at least once, on both sides of the boundary. A missing definition is a
// link error in this environment, and a definition the header forgot would be
// invisible to the C core it exists for.
void test_every_declared_symbol_is_defined_and_callable(void) {
    // C++ side, called from C++ the way the C core calls it.
    TEST_ASSERT_EQUAL_UINT32(1u ^ DOOM_LINK_SALT, doom_bridge_mix(1u));
    TEST_ASSERT_EQUAL_UINT32(DOOM_LINK_PALETTE_BLACK_BYTE, doom_bridge_palette_byte(0u));
    // The fail word on purpose: this is the one place both status words are
    // produced, and it costs one extra line in the test log.
    doom_bridge_emit_line(DOOM_LINK_STATUS_FAIL, 0u);

    // C side, called from C++ the way the scene calls it.
    TEST_ASSERT_EQUAL_UINT32(kShippedChecksum, doom_link_probe_checksum(
                                                   DOOM_LINK_PROBE_SEED,
                                                   DOOM_LINK_PROBE_ITERATIONS));
    TEST_ASSERT_EQUAL_INT(DOOM_LINK_STATUS_OK, doom_link_probe_run());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    test_setup();
    UNITY_BEGIN();

    RUN_TEST(test_checksum_is_deterministic_and_depends_on_both_arguments);
    RUN_TEST(test_zero_iterations_returns_the_offset_basis_xor_seed);
    RUN_TEST(test_checksum_matches_the_documented_fnv1a_definition);
    RUN_TEST(test_shipped_checksum_is_the_pinned_value);
    RUN_TEST(test_bridge_mix_applies_the_shared_salt);
    RUN_TEST(test_bridge_palette_byte_reads_the_port_table);
    RUN_TEST(test_probe_run_passes_and_reports_ok);
    RUN_TEST(test_every_declared_symbol_is_defined_and_callable);

    const int result = UNITY_END();
    test_teardown();
    return result;
}
