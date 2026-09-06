#pragma once
#include <cstdint>

/**
 * Which way a frightened person runs, and how close a fright has to be. One
 * mechanism, several callers -- a gunshot and a moving car are the same thing
 * to somebody on a pavement -- since writing it twice is two chances to get the
 * tie-breaking wrong.
 *
 * Engine-free like the rest of `game/rules/`, because every edge case here
 * produces a PLAUSIBLE wrong answer rather than a crash: a tie that returns no
 * direction is a pedestrian who freezes when startled, a square radius is a
 * crowd that reacts to a shot they are nowhere near. Neither reads as a bug --
 * they read as the game being lifeless.
 */
namespace top_down_city::threat {

/**
 * One whole step along exactly one axis. Never zero, except from `intoLine`.
 * Never diagonal: there is nothing to draw at 45 degrees (three sets of frames
 * plus a mirror), and both axes set would cover 1.41x the intended speed.
 */
struct Bearing {
    std::int8_t dx;
    std::int8_t dy;
};

/**
 * @brief Where to run.
 * @param offsetX,offsetY  FROM the threat TO the person, in world pixels.
 *
 * The dominant axis wins: somebody mostly to the right of a gunshot runs right,
 * because the smaller axis would carry them ACROSS the line of fire. An exact
 * diagonal picks the horizontal, consistently. A threat on the same pixel --
 * which is what a bullet is at the instant it connects -- still returns a
 * direction: standing perfectly still on the spot you were just shot at is the
 * one reaction worse than none.
 */
Bearing awayFrom(int offsetX, int offsetY);

/**
 * @brief Where to shoot.
 * @param offsetX,offsetY  FROM the shooter TO the target, in world pixels.
 *
 * The SAME computation as awayFrom: a person at P flees a threat at S along
 * P - S, and a shooter at S aims along P - S too, so the sign that makes one
 * "away" and the other "toward" lives entirely in which offset the caller
 * subtracts. Both names exist because `awayFrom(player - officer)` at an
 * officer's call site is a sentence that means the opposite of what it does;
 * the first draft negated one to "fix" that and a test caught it, and the tests
 * still pin them together so it cannot come back.
 */
Bearing toward(int offsetX, int offsetY);

/**
 * @brief Is the target lined up well enough to be worth a shot?
 *
 * A four-way aim fired at anything diagonal sprays past its target, and an
 * officer emptying a magazine into the pavement beside the player reads as
 * broken rather than unlucky. So a shot is only taken when the MINOR axis --
 * perpendicular to the way the bullet travels -- is inside `tolerancePx`.
 * Distance is not this function's business; ask `within`.
 */
bool hasLineOfFire(int offsetX, int offsetY, int tolerancePx);

/**
 * @brief Which way a shooter should step to bring a target into their line.
 * @param offsetX,offsetY  FROM the shooter TO the target, in world pixels.
 * @return A unit step, or {0, 0} when the shot is already on -- an officer
 *         who keeps sidestepping walks straight back out of the line.
 *
 * The step is ACROSS the bullet, never along it: the minor axis is what the
 * miss is made of, and walking the major one covers the whole distance without
 * ever lining up. The tie-break matches `hasLineOfFire`, or on an exact
 * diagonal the two would fight each other forever.
 */
Bearing intoLine(int offsetX, int offsetY, int tolerancePx);

/**
 * @brief Is a threat at this offset close enough to notice?
 *
 * A circle, not a bounding box: the corner of a square is 1.41 radii out.
 * Squared distances only, so there is no root and no float.
 */
bool within(int offsetX, int offsetY, int radiusPx);

}  // namespace top_down_city::threat
