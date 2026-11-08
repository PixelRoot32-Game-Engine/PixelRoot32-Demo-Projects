#include "game/rules/Records.h"

namespace top_down_city::records {

namespace {

constexpr std::uint8_t kAllChapters = static_cast<std::uint8_t>(
    (1u << static_cast<unsigned>(contract::Chapter::Count)) - 1u);

}  // namespace

Record clear() {
    Record record;
    record.bestStreak = 0;
    record.chapters = 0;
    record.bestCash = 0;
    return record;
}

bool merge(Record& into, const Record& seen) {
    bool improved = false;
    if (seen.bestStreak > into.bestStreak) {
        into.bestStreak = seen.bestStreak;
        improved = true;
    }
    if (seen.bestCash > into.bestCash) {
        into.bestCash = seen.bestCash;
        improved = true;
    }
    const std::uint8_t added =
        static_cast<std::uint8_t>(seen.chapters & ~into.chapters);
    if (added != 0) {
        into.chapters = static_cast<std::uint8_t>(into.chapters | added);
        improved = true;
    }
    return improved;
}

Improved improved(const Record& base, const Record& seen) {
    Improved result;
    result.streak = seen.bestStreak > base.bestStreak;
    result.cash = seen.bestCash > base.bestCash;
    result.chapter = (seen.chapters & ~base.chapters) != 0;
    return result;
}

std::size_t encode(const Record& record, std::uint8_t* out, std::size_t capacity) {
    if (out == nullptr || capacity < kPayloadSize) return 0;
    out[0] = record.bestStreak;
    out[1] = record.chapters;
    out[2] = static_cast<std::uint8_t>(record.bestCash & 0xFFu);
    out[3] = static_cast<std::uint8_t>((record.bestCash >> 8) & 0xFFu);
    return kPayloadSize;
}

bool decode(const std::uint8_t* in, std::size_t size, Record& record) {
    if (in == nullptr || size != kPayloadSize) return false;
    const std::uint8_t chapters = in[1];
    const std::uint16_t cash =
        static_cast<std::uint16_t>(in[2] | (static_cast<unsigned>(in[3]) << 8));
    if ((chapters & ~kAllChapters) != 0) return false;
    if (cash > economy::kMaxCash) return false;
    record.bestStreak = in[0];
    record.chapters = chapters;
    record.bestCash = cash;
    return true;
}

}  // namespace top_down_city::records
