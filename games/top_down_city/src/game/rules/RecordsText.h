#pragma once
#include <cstddef>
#include <cstdint>

#include "game/rules/Records.h"

/**
 * The words the record banner says, built into a caller-owned buffer.
 *
 * Pure and engine-free so a host test pins every string. The banner borrows
 * its text, so the scene keeps one `kTextCapacity` buffer per line and passes
 * it to `CityBanner::show`.
 *
 * Both builders REFUSE rather than truncate: when the buffer cannot hold the
 * whole text plus its terminator (or is null / zero-sized), they return 0 and,
 * if there is room for one byte, leave an empty string. A half-written record
 * ("BEST STREAK 25") would read as a real number.
 */
namespace top_down_city::records_text {

/// Longest text is "BEST STREAK 255  $9999  CH 3/3" (30) plus the terminator,
/// with a little headroom.
constexpr std::size_t kTextCapacity = 32;

/// The whole book: "NO RECORDS YET" when nothing is recorded, else
/// "BEST STREAK 7  $1234" with "  CH 2/3" appended once a chapter is done.
/// @return characters written (terminator excluded), or 0 on refusal.
std::size_t bookLine(const records::Record& book, char* out, std::size_t capacity);

/// The announcement for a beaten record. One line, most meaningful field wins:
/// chapter ("CHAPTER 2/3 CLEARED"), then streak ("NEW BEST STREAK 7"), then cash
/// ("NEW BEST $1234"). Values are read from `book`, the book AFTER the merge.
/// @return characters written, or 0 when nothing improved or on refusal.
std::size_t improvedLine(const records::Record& book,
                         const records::Improved& improved, char* out,
                         std::size_t capacity);

}  // namespace top_down_city::records_text
