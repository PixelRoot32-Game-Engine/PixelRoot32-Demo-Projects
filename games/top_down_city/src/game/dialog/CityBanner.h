#pragma once

#include <cstdint>

#include <gameplay/DialogRunner.h>
#include <gameplay/DialogTypes.h>

namespace top_down_city {

/**
 * @class CityBanner
 * @brief The top strip's one-line notice, as a one-line dialog script.
 *
 * A notice is a single `LineKind::Text` line with `autoAdvanceMs` set, so the
 * engine's DialogRunner owns the countdown and the "which notice is this"
 * question the frame skip asks. That retires the scene's own countdown and
 * the serial number it kept only because neither the text pointer nor the
 * visibility flag could tell two notices apart: `revision()` bumps on every
 * `show()`, and again when the notice runs out.
 *
 * Headless, like the runner under it. The strip keeps its own look -- a
 * full-width plate, an accent rule under it and centred text -- which
 * `graphics::DialogBox` cannot draw: it frames all four sides and draws text
 * from the padding, not centred.
 *
 * The line is runtime data rather than a constexpr table because two notices
 * are not literals: the weapon name and the rampage counter. Its text is still
 * BORROWED, as the runner's contract states for every DialogLine -- whatever
 * `label` points at must outlive the notice. Every caller passes a literal, a
 * static table entry or a scene-owned buffer.
 *
 * Not copyable: the runner holds a pointer to `script_`, which points at
 * `line_`, both inside this object.
 */
class CityBanner {
public:
    CityBanner();
    CityBanner(const CityBanner&) = delete;
    CityBanner& operator=(const CityBanner&) = delete;

    /**
     * @brief Put a notice up for `ms` milliseconds, replacing any other.
     * @param label What the strip says, or nullptr for the space's own name,
     *        which the scene reads live at draw time. Borrowed, not copied.
     * @param ms How long it stays up. Zero or less takes the strip down, the
     *        way the countdown this replaced read it; more than the runner's
     *        16-bit line timer holds is clamped to that maximum.
     */
    void show(const char* label, int ms);

    /// Take the strip down now.
    void hide();

    /// One frame of real time. A no-op while nothing is up.
    void update(unsigned long deltaMs);

    [[nodiscard]] bool visible() const;

    /// The notice's own words, or nullptr for the space's name or when down.
    [[nodiscard]] const char* label() const;

    /// Changes whenever what the strip shows may have changed. Compare by
    /// inequality only: it wraps.
    [[nodiscard]] std::uint16_t revision() const;

private:
    pixelroot32::gameplay::DialogLine   line_;
    pixelroot32::gameplay::DialogScript script_;
    pixelroot32::gameplay::DialogRunner runner_;
};

}  // namespace top_down_city
