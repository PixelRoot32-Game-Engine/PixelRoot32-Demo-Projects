#pragma once
#include <cstdint>

/**
 * The main mission: a story told in chapters, and the phase the courier run
 * never needed.
 *
 * `Mission.h` is deliberately a target index and a clock with no phase, which
 * for a courier is the whole design: every leg looks like every other, so
 * there is never anything to be in the middle of. Chapter 1 is two halves that
 * do not look alike -- find the car, then drive it somewhere -- and which half
 * the player is in decides what the marker points at, what the HUD says and
 * what counts as failing. That is a state machine, and bolting one onto
 * `mission::` would have made the courier loop carry a phase it never enters.
 * Chapter 2 vindicates it: a walk to the station on a clock, then a retreat
 * with no clock at all, where the wanted level is the thing counting down --
 * which a target-and-timer could not have expressed, there being nothing to
 * time. So the two live apart: the courier run is what is always available,
 * this is what runs out, and `Chapter` advances only on a completed job and
 * never rewinds, which is what makes it a story rather than a second loop.
 *
 * **Why every transition here is guarded.** The scene calls `begin` from an
 * input edge and `boarded`/`delivered` from a collision test, and both fire
 * more than once on the same job as a matter of course. None of those double
 * calls draws anything wrong, which is exactly why they are dangerous -- a
 * re-granted clock reads as a generous mission, a chapter skipped by a second
 * delivery reads as content the player simply never saw. Every entry point
 * below is therefore a no-op from every phase but the one it belongs to, and
 * every one of those refusals is pinned in `test_contract`.
 *
 * Engine-free like the rest of `game/rules/`: nothing here draws, and a
 * contract stuck in the wrong phase looks identical from the outside to one
 * running correctly.
 */
namespace top_down_city::contract {

/// The main mission in order. `Count` is the end of the story, not a chapter
/// -- see `Phase::Complete`, which is where a finished contract sits instead.
enum class Chapter : std::uint8_t { Boost, Hit, Frenzy, Count };

/**
 * @brief Where inside a chapter the player is. `Idle` is the phone ringing;
 *        `Complete` is the phone gone.
 *
 * `ToCar`/`ToDrop` are chapter 1's two halves. Chapter 2's are not the same
 * shape: `ToTarget` is a walk on a clock like everything above it, but
 * `Struck` -- the officer down, the stars climbing, the player walking home --
 * deliberately has no clock at all (see `struck`). `Rampage` is a third shape
 * again: a clock, but what it counts down to is a NUMBER rather than a place,
 * which is why the three predicates below are nested widenings rather than one
 * question with an exception in it (see `active`).
 *
 * Adding members here is safe for the scene's two `switch`es on this enum
 * because both carry a `default:`; it is NOT safe for anything that stores
 * the phase across a boundary, and nothing does.
 */
enum class Phase : std::uint8_t {
    Idle,
    ToCar,
    ToDrop,
    ToTarget,
    Struck,
    Rampage,
    Complete
};

/// The job clock is a uint16_t like the courier's, and for the same reason:
/// it is the largest counter the HUD can draw and the smallest one that holds
/// the longest job the map can produce. The clamp is a guard against the
/// intermediate wrapping, not a design decision --
/// `test_the_clamp_is_a_guard_and_not_a_rule` asserts no reachable job comes
/// near it.
constexpr std::uint16_t kMaxJobSteps = 0xFFFFu;

/**
 * @brief Everything a chapter is, in one struct there is exactly one of.
 *
 * **Why `mark` is a field and not a reuse of `drop`.** Both are a `uint8_t`
 * index and only one is ever live -- which is precisely the argument for one
 * field, and precisely why it is wrong: `drop` indexes `MISSION_TARGETS`,
 * `mark` indexes the station's officers, two tables of two lengths with no
 * relationship between row 3 of one and row 3 of the other. A single field
 * would carry an officer index into `contractTileX`'s `ToDrop` arm the first
 * time a transition went somewhere unplanned, and that draws as a marker
 * pointing at an ordinary street corner -- not a crash, not anything a
 * screenshot would catch. The price is one byte (two, once the `uint16_t`
 * alignment is paid) on a struct there is one instance of.
 *
 * `targetsLeft` is the same argument reaching its third answer: it is a COUNT,
 * not an index, so folding it into `mark` or `drop` would point the marker at
 * officer number seven of three. It counts DOWN rather than up, so the number
 * the HUD wants and the number the ending tests are the same one.
 */
struct State {
    Chapter       chapter;    ///< the chapter whose phone is live, or the one running
    Phase         phase;
    std::uint8_t  vehicle;    ///< index into city_scene::VEHICLE_SPAWNS
    std::uint8_t  drop;       ///< index into city_scene::MISSION_TARGETS -- chapter 1
    std::uint8_t  mark;       ///< index into the station's officers -- chapter 2
    std::uint8_t  targetsLeft;///< bodies still owed -- chapter 3
    std::uint16_t stepsLeft;
};

/**
 * @brief One clock for the whole job: the walk to the car plus the drive to
 *        the drop.
 *
 * One budget rather than two, so the player decides how to spend it: a car
 * moves faster than a courier walks, so time saved on the first leg is time
 * bought for the second, which is the only thing that makes the choice of
 * route on foot worth anything.
 *
 * Built out of `mission::allowanceSteps` for each leg rather than out of a
 * balance number of its own: that allowance is already proven walkable over
 * every distance this map can produce (`test_every_leg_is_long_enough_to_walk`)
 * and a second balance surface is a second thing that can be silently wrong.
 * The generosity is the point -- the failure this rule guards against is a
 * mission nobody can finish, not one somebody finishes with time to spare.
 * The sum is widened before the clamp rather than after: two legs at
 * `mission::kMaxAllowanceSteps` are 131070, which a uint16_t does not hold.
 * Negative inputs count as zero, as they do everywhere here: the scene
 * subtracts two tile coordinates to get them, and a sign slip should shorten a
 * job rather than wrap it.
 */
std::uint16_t jobSteps(int toCarTiles, int carToDropTiles);

/// No job, no chapter played. What a run starts with.
State clear();

/**
 * @brief Answer the phone: name the car, the drop, and the clock.
 *
 * A no-op unless the phase is `Idle`: the scene calls this from an input edge,
 * and a second press -- or one edge that latches twice -- must not re-grant
 * the clock and quietly turn the chapter into a mission that cannot be failed.
 * It is also what keeps the phone silent once the contract is `Complete`.
 */
void begin(State& state, std::uint8_t vehicle, std::uint8_t drop,
           int toCarTiles, int carToDropTiles);

/// One fixed logic step off the clock, and only while a job is running.
/// Saturates at zero rather than wrapping: the scene notices the expiry on
/// the same step and fails the job, but "the scene notices" is not something
/// a counter should depend on.
void tick(State& state);

/**
 * @brief Is the contract pointing the player somewhere on the map?
 *
 * The narrowest of three nested predicates, and the one the marker and the
 * radar dot are drawn from: true on exactly the phases that have a destination
 * -- the walk to the car, the drive to the drop, the walk to the station.
 * False during `Struck`, whose objective is the star row, and during
 * `Rampage`, whose objective is a count: a cyan ring over the corner the
 * player is standing on points at nothing they have left to do. It exists so
 * `contractTileX`/`contractTileY` have exactly one guard, testable off a
 * scene; every phase true here has an arm in both of those switches, and the
 * day one does not, this predicate is what has to change rather than a
 * `default:` quietly returning a payphone.
 */
bool hasDestination(const State& state);

/**
 * @brief Is a job running WITH A CLOCK -- on foot toward the car, driving
 *        toward the drop, walking to the station, or mid-rampage?
 *
 * The emphasis carries the function. `Struck` is a chapter very much in
 * progress and deliberately NOT active, which is how the countdown stops:
 * `tick` and `expired` are both gated on this one answer, so the clock
 * stopping and the phase changing are one event rather than two to keep in
 * step, where a `bool timed` beside the phase would be a second source of
 * truth for something the phase already says.
 *
 * Written as `hasDestination` plus `Rampage` rather than as a list of four,
 * the way `underway` is written as `active` plus `Struck`: three nested
 * widenings, each one term wider than the last, so no phase can fall out of
 * the middle one unnoticed -- `test_contract` pins them as the nesting rather
 * than as three lists. For "does the main mission own the player right now",
 * see `underway`.
 */
bool active(const State& state);

/**
 * @brief Is a chapter under way at all, clock or no clock?
 *
 * `active` plus `Struck`, and that difference is the only reason this exists.
 * The scene suspends the courier leg and hides the courier ring for as long as
 * the main mission owns the player, and that ownership outlasts the clock by
 * exactly one phase: between the kill and getting clean there is no countdown,
 * but there is absolutely a chapter in progress. Asking `active` instead would
 * relight the courier run in the middle of the Hit -- two missions competing
 * for one marker, with the courier's clock quietly expiring under the one the
 * player can see.
 */
bool underway(const State& state);

bool expired(const State& state);

/// Took the car: `ToCar` -> `ToDrop`, and nothing else. This arrives from a
/// collision test, and a player sitting in the car collides with it on every
/// step -- only the first of those is a boarding. The clock is deliberately
/// untouched: one budget for the whole job.
void boarded(State& state);

/**
 * @brief Made the drop: the chapter is over.
 *
 * `ToDrop` -> the next chapter, phone live again, and `Complete` after the
 * last one.
 *
 * A no-op from every other phase. The drop is a collision test too, and the
 * player parks in it -- a second delivery would hand out a chapter nobody
 * played. Delivering on foot is refused for a different reason: the point of
 * this chapter is the vehicle.
 */
void delivered(State& state);

/* ------------------------------------------------------------------------
 * Chapter 2 -- The Hit
 *
 * Walk into the station, shoot the marked officer, walk back out and shed the
 * stars it cost you. Two phases like the boost, but the second half is a
 * different animal: `ToDrop` is a drive against a clock, `Struck` a retreat
 * against the police.
 * ---------------------------------------------------------------------- */

/**
 * @brief Answer the phone: name the officer and the walk to the station.
 * @param officer          index into the station's officers, stored in `mark`
 * @param toStationTiles   Manhattan distance to the station, in tiles
 *
 * `Idle` -> `ToTarget`, and only on `Chapter::Hit`. The chapter guard is the
 * half `phase == Idle` cannot cover and the half with no visual failure: both
 * payphones are Idle-and-live as far as the state machine is concerned, so
 * without it chapter 1's phone could start chapter 2 -- a chapter played out
 * of order, its marker following an officer index nobody named, drawing as a
 * perfectly ordinary mission pointing somewhere odd.
 *
 * The clock is `mission::allowanceSteps` over the single leg, for exactly the
 * reason `jobSteps` is built out of it twice. One leg rather than a sum
 * because there is one -- the way back is untimed.
 */
void beginHit(State& state, std::uint8_t officer, int toStationTiles);

/**
 * @brief The marked officer is down: `ToTarget` -> `Struck`.
 *
 * **And the clock stops here, deliberately.** Getting clean is untimed by
 * design: the beat already has a better pressure of its own -- up to five
 * stars, six seconds a star, counting only while nobody on the force has eyes
 * on the player (`wanted::tick`). A countdown over the same moment would be
 * two pressures on one beat, failing for two reasons that look identical from
 * the player's chair -- caught, or simply late? -- with nothing to learn from
 * either. One pressure per beat, and this beat's is the manhunt.
 *
 * The clock is stopped rather than zeroed: `active` excludes `Struck`, so
 * `tick` and `expired` both stop consulting `stepsLeft` on the same step the
 * phase changes, and one fact beats two that could disagree. Leaving the
 * residue is also the safer failure -- a gate widened by mistake later reads
 * as a generous clock rather than an instant expiry over a kill already made.
 *
 * A no-op from every other phase. This arrives from a damage test, and an
 * officer on the ground can be shot again -- by the player, or by the next
 * pellet of the same shotgun spread on the same step. Every other officer in
 * the city can be shot at any point in the run, too; only the marked one,
 * during the Hit, is a hit.
 */
void struck(State& state);

/**
 * @brief Clean: the stars are gone and the chapter is over.
 *
 * `Struck` -> the next chapter, phone live again -- and `Complete` after the
 * last, through the same advancement `delivered` ends on, so the two endings
 * cannot drift apart.
 *
 * This is what a scene calls on the step `wanted::isWanted` goes false. A
 * no-op from every other phase, and that guard is load-bearing twice over:
 * zero stars is a level the player then STAYS at, so the scene's test is true
 * on every subsequent step; and a player who walked to the station with a
 * clean record is at zero stars for the whole of `ToTarget`, which would
 * otherwise complete the chapter without a body in it.
 */
void cleaned(State& state);

/* ------------------------------------------------------------------------
 * Chapter 3 -- Frenzy
 *
 * A corner, a clock and a number of bodies. No car, no room, and nowhere to
 * walk to: the objective is a count, which is the one shape neither chapter
 * before it has. It is also the chapter the finite magazine was built for --
 * see `kFrenzyTargets`.
 * ---------------------------------------------------------------------- */

/**
 * @brief How many bodies the rampage asks for.
 *
 * Held between the two magazines in `test_contract`, and both bounds are the
 * chapter's whole design. **More than a pistol holds** is the soft gate:
 * nothing refuses the phone to an under-armed player -- refusing would be a
 * phone that does not answer, which says nothing -- the arithmetic simply does
 * not work out, and the shop is the only place it can be made to, so "go and
 * buy the shotgun" is a sentence the player says to themselves. **No more than
 * a shotgun holds** keeps that gate honest: the gun the chapter sends them to
 * buy has to finish it, without a second trip to the counter halfway through a
 * five-star manhunt.
 *
 * A count rather than a duration is also what makes the chapter survive the
 * crowd running out: the pool is a fixed number of slots streamed around the
 * camera, so the city hands out targets at a rate the player cannot raise, but
 * hands them out for as long as the player keeps moving, and a count waits
 * where a fixed body-per-second rate would not. That pool is the third bound
 * and the only one not about guns -- `CityConstants.h` holds this at or under
 * `kPedestrianPoolSize`, so one screenful of people is enough to finish the
 * chapter and the rampage never DEPENDS on a respawn arriving. It will usually
 * get several anyway, since a corpse that leaves the despawn margin frees its
 * slot immediately and a rampage moves; the bound is for the player who does
 * not.
 */
constexpr std::uint8_t kFrenzyTargets = 12;

/// What one body is allowed to cost, in fixed logic steps -- eight seconds
/// each at 62.5 steps a second, and a CROWD budget rather than an aiming one.
/// The obvious reading, crossing the screen and lining somebody up, gives
/// about four seconds and is the wrong question: a body that goes down keeps
/// its pool slot while it fades, so a player who clears everyone in sight is
/// waiting on the pool rather than on their own aim, and the longest wait is
/// one corpse's fade. That is the worst case, once per body, and
/// `CityConstants.h` asserts it against `kPedSquashLingerSteps` rather than
/// leaving the two numbers to agree by luck -- `kFrenzyTargets` never DEPENDS
/// on a respawn, and this budget is what makes one survivable when the chapter
/// needs it anyway. Generous on purpose, for the reason `jobSteps` is.
///
/// The clock is built out of THIS rather than typed as a total, the same
/// argument `jobSteps` makes about `mission::allowanceSteps` one shape along:
/// the size of the chapter and the length of it become one number, so a
/// rampage retuned to fewer targets is automatically shorter and there is no
/// second constant to forget.
constexpr std::uint16_t kFrenzyStepsPerTarget = 500;

/**
 * @brief The rampage clock: one window per body.
 *
 * Clamped like `jobSteps`, and the clamp is a guard against the multiplication
 * rather than a design decision:
 * `test_the_shipped_rampage_never_reaches_the_clamp` asserts the shipped
 * chapter comes nowhere near it, because a clock that stopped growing with the
 * count would quietly stop meaning one window per body.
 */
std::uint16_t frenzySteps(std::uint8_t targets);

/**
 * @brief Answer the phone: start the clock and the count where the player
 *        stands.
 *
 * `Idle` -> `Rampage`, and only on `Chapter::Frenzy`. There is no leg to price
 * and no distance to pass in, because the corner IS the phone: this is the one
 * chapter that begins at the marker instead of pointing at one.
 *
 * Both guards are the ones `beginHit` carries, and neither implies the other
 * -- every live phone is `Idle` as far as this file can see, so the chapter
 * test is what stops chapter 1's phone starting chapter 3. The phase test
 * stops a RUN edge that latches across two frames from re-granting the clock,
 * and here a second counter as well: a rampage that hands its bodies back is a
 * chapter nobody can finish rather than one nobody can fail.
 */
void beginFrenzy(State& state);

/**
 * @brief A body went down during the rampage: one off the count.
 *
 * A no-op from every phase but `Rampage`, and that guard carries more weight
 * than the others in this file because of where the call comes from: the one
 * place in the demo that reports a death, firing in every space and on every
 * step of the run -- the crowd under a car during a courier leg, the officer
 * put down in the station lobby during chapter 2, a bystander caught by a
 * stray pellet before the story has started. Only a body during the rampage is
 * a target.
 *
 * Every kill counts, civilian or police, and that is deliberate: by the third
 * star the street is mostly uniforms and the crowd has scattered, so a rule
 * that counted only civilians would stall the chapter exactly when the city
 * stops producing them, through no decision of the player's. The last one ends
 * the story through the same `advanceChapter` `delivered` and `cleaned` use --
 * three endings, one implementation, so they cannot drift.
 *
 * One death in the city deliberately never reaches here: somebody run over by
 * TRAFFIC rather than by the player. The scene does not report it, because
 * billing the player for an accident they had no hand in was already decided
 * against for the star counter -- and the same decision means that body costs
 * the chapter a crowd slot without paying it a target. That is a reason the
 * clock is generous, not a reason to start counting accidents: a rampage the
 * city could finish on the player's behalf is not a rampage.
 */
void culled(State& state);

/// Ran out of time, lost the car, or got arrested: the job ends, the clock is
/// zeroed, and the chapter stays exactly where it was. There is no game over
/// in this demo and no menu to send anybody to -- the phone is still there,
/// still offering the same job.
///
/// Gated on `underway` rather than on `active`, the one place the wider
/// predicate changes behaviour rather than just answering a question: an
/// arrest between the kill and getting clean has to end the job, and `Struck`
/// has no clock, so nothing else can end it -- a failure that left the chapter
/// parked there would owe the player a clean-up for a mission they had already
/// lost, with no phone to answer and no marker to follow. Still a no-op
/// outside a chapter, since the scene can see an expiry and a wrecked car on a
/// step where nothing is live.
void failed(State& state);

/// Is there a job to answer? Idle and not finished -- and `Complete` is a
/// phase rather than a fourth chapter precisely so this is one comparison.
/// Deliberately NOT the whole question the scene asks (see `offered`): this
/// half knows about the state machine and nothing about the map.
bool phoneLive(const State& state);

/**
 * @brief Is a phone ringing for this chapter on a map with this many of them?
 * @param chapterCount  Payphones the map actually has -- the scene passes
 *                      `scene::NUM_CONTRACT_PHONES`.
 *
 * `phoneLive` alone is not enough and the gap is silent. `delivered` walks the
 * chapter forward until it runs out of ENUM, and `Chapter` has three members
 * while the shipped map has one payphone: finish chapter 1 and the phase is
 * `Idle` again, so `phoneLive` says yes -- for a chapter with no row in
 * `CONTRACT_PHONES`, no car and no drop. The scene reads
 * `CONTRACT_PHONES[chapter]` to point the marker and to test the ring, so that
 * yes is an out-of-range read that draws as a phone ringing somewhere
 * plausible rather than as a crash.
 *
 * The bound lives here rather than in the scene because a rule in a scene is a
 * rule no host test can reach, and because chapter 2 is written by adding a
 * row to the generator, which moves this answer without touching a line of it.
 * A count of zero offers nothing, including to chapter zero: an empty table
 * has no row 0 either, and CityConstants.h's static_assert on the shipped
 * count cannot cover a count that arrives as an argument.
 */
bool offered(const State& state, std::uint8_t chapterCount);

}  // namespace top_down_city::contract
