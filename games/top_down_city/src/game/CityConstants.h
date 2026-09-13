#pragma once
#include <cstdint>

#include <platforms/EngineConfig.h>

#include "game/rules/Facing.h"
#include "game/rules/Threat.h"
#include "game/rules/Mission.h"
#include "game/rules/NightLights.h"
#include "game/rules/AudioCues.h"
#include "game/rules/Armor.h"
#include "game/rules/Contract.h"
#include "game/rules/Economy.h"
#include "game/rules/Shop.h"
#include "game/rules/Wanted.h"
#include "game/rules/Weapon.h"

#include "generated/tilemaps/city_scene.h"
#include "generated/tilemaps/corner_shop.h"
#include "generated/tilemaps/police_station.h"
#include "generated/sprites/PedestrianSprites.h"
#include "generated/sprites/PlayerSprites.h"
#include "generated/sprites/VehicleSprites.h"

namespace top_down_city {

/**
 * @brief World geometry, fixed-step timing, the movement budget and the
 *        collision strategy.
 *
 * Every dimension is derived from the exported scene, so resizing the city is
 * a change to `tools/generate_city_assets.py` alone -- no C++ needs editing.
 * The static_asserts live here because the engine-free, host-tested rule
 * modules under game/rules/ keep their own copies of numbers that live here,
 * and this is the only header that sees both ends.
 */

namespace scene = city_scene;
namespace station = police_station;
/* The ROOM, not the catalogue. `shop::` is game/rules/Shop.h -- what the till
 * sells; `shop_room::` is the exported tilemap -- where the till is. One name
 * would have meant an engine-free header and a pure-engine one sharing it. */
namespace shop_room = corner_shop;

/* Input button IDs, in the order the platform files pass them to InputConfig. */
constexpr std::uint8_t BTN_UP    = 0;
constexpr std::uint8_t BTN_DOWN  = 1;
constexpr std::uint8_t BTN_LEFT  = 2;
constexpr std::uint8_t BTN_RIGHT = 3;
constexpr std::uint8_t BTN_RUN   = 4;
constexpr std::uint8_t BTN_FIRE  = 5;

/* World geometry. 128x128 tiles of 16px = 2048x2048 px, roughly 72 screenfuls
 * on a 240x240 panel. Three layers of one byte per cell each live in flash. */
constexpr int kTilePx      = scene::TILE_SIZE;
constexpr int kWorldTilesX = scene::MAP_WIDTH;
constexpr int kWorldTilesY = scene::MAP_HEIGHT;
constexpr int kWorldWidth  = kWorldTilesX * kTilePx;
constexpr int kWorldHeight = kWorldTilesY * kTilePx;

static_assert(kWorldWidth > DISPLAY_WIDTH && kWorldHeight > DISPLAY_HEIGHT,
              "The camera clamp assumes the world is larger than the viewport.");

/* Fixed logic step. Movement is integrated in whole steps rather than against
 * raw deltaTime, so walking speed is identical whether the simulator runs at
 * 200 fps or the ESP32 panel manages 25. */
constexpr int kLogicStepMs           = 16;   // 62.5 steps/second
constexpr int kMaxLogicStepsPerFrame = 4;    // catch-up clamp: 64ms of backlog

static_assert(audio_cues::kLogicStepMs == kLogicStepMs,
              "AudioCues.h keeps its own copy of the logic step so a cue's "
              "cooldown can be checked against a weapon's fire rate without "
              "that header learning what a scene is; this is the tie-down.");

/* Sub-pixel movement: fixed-point with 8 fractional bits. No float, which is
 * what keeps the walk smooth on an ESP32 without an FPU in the hot path. */
constexpr int kSubPixelShift = 8;
constexpr int kSubPixelOne   = 1 << kSubPixelShift;

/* One pixel per step is 62.5 px/s -- a 16 px tile in a quarter of a second, a
 * 240 px screen in 3.8 s, the band the 8- and 16-bit top-down games this city
 * is drawn like walk at. It also makes a tile cost exactly sixteen steps,
 * which the courier allowance is written against, with no fractional
 * remainder. The sprint is 1.75x rather than 2x: twice the walk is also twice
 * ORDINARY TRAFFIC, which made the player the fastest thing in the city. */
constexpr int kWalkSpeedSub = 256;   // 1 px/step     = 62.5 px/s
constexpr int kRunSpeedSub  = 448;   // 1.75 px/step  = 109 px/s

/* ADR-12: the footstep stride is distance-locked, not time-locked -- a
 * footfall costs the same ground at either gait. That only holds while the
 * two stride lengths are in the inverse ratio of the two speeds. */
static_assert(audio_cues::kWalkStrideSteps * kWalkSpeedSub
                  == audio_cues::kRunStrideSteps * kRunSpeedSub,
              "a footfall must cost the same ground whether the player "
              "walks or runs -- the stride steps and the gait speeds have "
              "drifted out of their inverse ratio");

/* 1/sqrt(2) in the same fixed-point scale, so a diagonal is not faster than a
 * straight line. */
constexpr int kDiagonalScaleNum = 181;
constexpr int kDiagonalScaleDen = 256;

/* Player collision box, in pixels relative to the 16x16 sprite's top-left.
 * Only the feet collide: a top-down character whose whole sprite collides
 * cannot stand next to a wall its head visually overlaps, which is what makes
 * cheap tile collision feel wrong. */
constexpr int kPlayerSpriteW    = kPlayerSpriteSize;
constexpr int kPlayerSpriteH    = kPlayerSpriteSize;
constexpr int kPlayerBoxOffsetX = 3;
constexpr int kPlayerBoxOffsetY = 9;
constexpr int kPlayerBoxWidth   = 10;
constexpr int kPlayerBoxHeight  = 6;

/**
 * @brief How the Items layer is tested for collision.
 *
 * `WholeTile` blocks the entire 16x16 cell of any tile flagged `TILE_SOLID`.
 * `PerPixel` decodes the tile's 4bpp bitmap and blocks only where a pixel is
 * opaque -- which is only meaningful because props live on their own layer
 * with a transparent surround. `PerPixelEroded` additionally requires the
 * whole (2r+1)^2 square around a pixel to be opaque, so the player slides past
 * a lamp post or the thin edge of a tree canopy instead of snagging on it.
 */
enum class CollisionMode : std::uint8_t { WholeTile, PerPixel, PerPixelEroded };

inline constexpr CollisionMode kCollisionMode = CollisionMode::PerPixelEroded;
inline constexpr int kPropErosionPx = 1;

/* Walk animation: neutral, left foot, neutral, right foot. Advanced by
 * distance travelled rather than by time, so the legs never skate. */
constexpr int kAnimPixelsPerFrame = 7;
constexpr std::uint8_t kWalkCycle[4] = {1, 0, 2, 0};
constexpr std::uint8_t kWalkCycleLength = 4;

/* HUD. The district banner shows on entry to a new district and fades out. */
constexpr int kZoneBannerMs   = 2400;

/* Being caught: the one moment the demo stops. Two seconds is long enough to
 * read one word and short enough that nobody reaches for the button. The city
 * is still drawn behind it, frozen -- a black screen would make it a scene
 * transition rather than the end of what just happened. */
constexpr int kBustedHoldMs = 2000;

/* Where a caught player comes back: the tile directly below the entrance
 * notch, not the map's start tile across the island. It also puts the one
 * interior within a step of every restart. The generator checks the tile is
 * walkable and connected; see generate_city_assets.py. */
constexpr int kBustedSpawnTileX = scene::POLICE_DOOR_TILE_X;
constexpr int kBustedSpawnTileY = scene::POLICE_DOOR_TILE_Y + 1;
static_assert(kBustedSpawnTileX < kWorldTilesX
              && kBustedSpawnTileY < kWorldTilesY,
              "the station steps are off the map");

/* ------------------------------------------------------------------------
 * The doors
 * ------------------------------------------------------------------------
 * Every building the player can walk into, as one table: a doorway is four
 * facts that have to agree -- where it is, which room is behind it, what the
 * hint says, and whether the room hides you -- and three of them used to be
 * spelled into separate coordinate comparisons in CityScene. `interior`
 * indexes CityWorld's bound scenes, so this table's order and the bind call's
 * are the same order; CityWorld's bind takes the count and static_asserts
 * against kNumDoorways.
 */
struct Doorway {
    std::uint16_t tileX;
    std::uint16_t tileY;
    /// Index into CityWorld's interior scenes.
    std::uint8_t  interior;
    /// True when there is somebody inside who answers to the police. It is
    /// the station, and it is why the station needs no hideout rule: the duty
    /// staff have eyes and game/rules/Hideout.h is for the rooms that do not.
    bool          staffed;
};

constexpr Doorway kDoorways[] = {
    { scene::POLICE_DOOR_TILE_X, scene::POLICE_DOOR_TILE_Y, 0, true  },
    { scene::SHOP_DOOR_TILE_X,   scene::SHOP_DOOR_TILE_Y,   1, false },
};
constexpr std::uint8_t kNumDoorways =
    static_cast<std::uint8_t>(sizeof(kDoorways) / sizeof(kDoorways[0]));

static_assert(kDoorways[0].tileX != kDoorways[1].tileX
              || kDoorways[0].tileY != kDoorways[1].tileY,
              "two doors on the same cell: the second one is unreachable");

/* Which row is the station, named rather than assumed. `CityScene::takeHit`
 * needs it: chapter 2 is taken outdoors and has to reach the duty staff before
 * the clock starts.
 *
 * The assert below proves one thing only -- row 0 is still the row somebody
 * wrote the station's door into, so reordering `kDoorways` without moving this
 * constant stops the build. It cannot see the `interiorScenes[]` array each
 * platform header hand-orders, which is what the caller actually depends on,
 * and whose own comment admits a wrong order leaves both doors working and
 * opening onto each other's rooms. A `static_cast<PoliceStationScene*>` once
 * stood on this assert as though it could, turning a swapped row into
 * undefined behaviour that draws as an ordinary frame over a build with RTTI
 * off. The cast is gone -- `interiorScene` hands back a `BaseCityScene*` and
 * the room answers `stationStaff()` for itself -- so a mis-ordered table is
 * now a nullptr the caller already tests. */
constexpr std::uint8_t kPoliceDoorway = 0;
static_assert(kDoorways[kPoliceDoorway].tileX == scene::POLICE_DOOR_TILE_X
              && kDoorways[kPoliceDoorway].tileY == scene::POLICE_DOOR_TILE_Y,
              "kPoliceDoorway no longer names the station's door, so chapter 2 "
              "would be asking a different room for its duty staff");
constexpr int kZoneBannerH    = 15;

/* The map overlay is a radar, not an atlas: three city blocks square around
 * whoever the camera is following. A whole-island map answers "where am I",
 * which the district banner already answers in words; three blocks answers
 * "what is around the next corner". Sized in BLOCK_PITCH_TILES rather than in
 * tiles, so the generator's street pitch resizes the overlay with it. One
 * pixel per tile puts it at 48 px square, a fifth of the panel's width. */
constexpr int kMinimapBlocks  = 3;
constexpr int kMinimapTiles   = kMinimapBlocks * scene::BLOCK_PITCH_TILES;
constexpr int kMinimapScale   = 1;   // one pixel per tile
constexpr int kMinimapSize    = kMinimapTiles * kMinimapScale;
constexpr int kMinimapOriginX = 5;
constexpr int kMinimapOriginY = 5;

/* The marker never moves: the window is centred on the focus tile and
 * deliberately NOT clamped to the map, so the player stays dead centre even
 * against the coast and the off-map cells simply go undrawn -- which is how
 * the overlay shows the edge of the island. */
constexpr int kMinimapCentreX = kMinimapOriginX
                              + (kMinimapTiles / 2) * kMinimapScale;
constexpr int kMinimapCentreY = kMinimapOriginY
                              + (kMinimapTiles / 2) * kMinimapScale;

/* The drop on the radar: a green core inside a dark border. The border is
 * what makes it one marker rather than three -- the overlay has three grounds
 * (street yellow, carriageway grey, the near-black plate the sea shows
 * through) and a bare dot reads differently against each. */
constexpr int kMinimapDropPx = 3;   // the green core; the border adds one a side

/* ------------------------------------------------------------------------
 * HUD and radar ink
 * ---------------------------------------------------------------------- */

/* Every colour the HUD and the overlay draw with, as ENGINE palette
 * enumerators, and here rather than at each call site because of the rule: a
 * drawing PRIMITIVE (every rectangle, line, pixel and glyph) resolves its
 * Color against the engine's BACKGROUND palette, which this demo never
 * replaces -- it binds its own palettes into SPRITE slots. So primitives take
 * gfx::Color and sprites take ink::, and nothing outside a drawSprite() call
 * may name an ink:: enumerator. Breaking that does not fail to compile: it
 * silently draws a different colour. */
namespace hud {
using pixelroot32::graphics::Color;

constexpr Color kPanel     = Color::White;   ///< #FFFFFF, every readout's plate
constexpr Color kInk       = Color::Navy;    ///< #1B1F3B, text on that plate
constexpr Color kInkMuted  = Color::Gray;    ///< #8D8D8D, a readout with nothing in it
constexpr Color kDanger    = Color::Red;     ///< #C1121F, hurt, nearly out, wanted
constexpr Color kEmptyStar = Color::Black;   ///< #000000, a star not yet earned
constexpr Color kObjective = Color::Green;   ///< #2ECC40, the drop, in all three places

/* The main mission's marker: the SECOND hue on the radar that is not a state
 * of the city. It and the courier drop are where you are GOING, drawn in the
 * same three shapes -- ground ring, radar dot, timer pip -- so only the colour
 * tells them apart, and it must read as "not the green one" against all three
 * of the overlay's grounds (kRadarGround's yellow, kRadarStreet's grey, the
 * near-black plate the sea shows through). Each obvious candidate loses for
 * its own reason:
 *
 *  - Blue #0047FF is kRadarCar: one more dot in a field of two dozen.
 *  - Orange #FF9F1C is already kAccent and one step from the island's own
 *    #FFD500, so on the ground ink -- most of the radar -- it barely separates.
 *  - Red #C1121F is kRadarYou and kDanger: a second red is a second danger.
 *  - LightGreen #A8FF9E is kObjective with the lights on, exactly the confusion
 *    a second hue exists to avoid.
 *  - Magenta is #CECECE in this palette, a light grey lost in the carriageway.
 *    The enumerator's NAME is the trap here, not its value.
 *  - Purple #7B2CBF is darker than the coast plate it would stand on; the dot's
 *    border rescues it there, nothing rescues the ring.
 *
 * Cyan is the one fully saturated colour left that no part of the city is
 * painted in. Its luminance lands within a couple of points of the
 * carriageway's grey, so chroma alone separates it there -- the first thing a
 * 12-bit panel spends, which is why the radar dot keeps the same one-pixel dark
 * border the drop has rather than being trusted bare. */
constexpr Color kContract  = Color::Cyan;    ///< #00C2FF, the chapter, in all three places

constexpr Color kAccent    = Color::Orange;  ///< #FF9F1C, the rule under the banner
constexpr Color kEdge      = Color::Black;   ///< #000000, a one-pixel border
constexpr Color kTracer    = Color::White;   ///< #FFFFFF, a round in flight

/* The radar's plate doubles as the sea and as everything past the coastline,
 * which is why it is the one dark surface here: an edge that is simply not
 * painted is cheaper and truer than one drawn on. */
constexpr Color kRadarPlate   = Color::Navy;   ///< #1B1F3B, the frame and the water
constexpr Color kRadarGround  = Color::Yellow; ///< #FFD500, the island
constexpr Color kRadarStreet  = Color::Gray;   ///< #8D8D8D, asphalt
constexpr Color kRadarCar     = Color::Blue;   ///< #0047FF, a parked car in reach
constexpr Color kRadarYou     = Color::Red;    ///< #C1121F, the cross at the centre
constexpr Color kRadarYouCore = Color::White;  ///< #FFFFFF, its one bright pixel
}  // namespace hud

/* The generator writes the radar's two inks into the tilemap header as raw
 * indices. These are the two ends of that agreement. */
static_assert(scene::MINIMAP_INK_STREET
                  == static_cast<std::uint8_t>(hud::kRadarStreet),
              "the generator's street ink is not the colour the radar means");
static_assert(scene::MINIMAP_INK_GROUND
                  == static_cast<std::uint8_t>(hud::kRadarGround),
              "the generator's ground ink is not the colour the radar means");
static_assert(scene::MINIMAP_INK_STREET != 0xFF
                  && scene::MINIMAP_INK_GROUND != 0xFF,
              "a radar ink collides with minimap::kAbsent, so those tiles "
              "would silently stop being drawn");

/* The two objective hues against the three grounds and against each other.
 * Every one of these failures draws SOMETHING -- a marker painted the colour
 * of the tile under it is still a marker, just an invisible one -- which is
 * why they are asserted rather than left to be noticed. */
static_assert(hud::kContract != hud::kObjective,
              "the chapter and the courier drop would be the same marker in "
              "the same three places");
static_assert(hud::kContract != hud::kRadarGround
                  && hud::kContract != hud::kRadarStreet
                  && hud::kContract != hud::kRadarPlate,
              "the chapter marker is painted the colour of a ground it has to "
              "be seen against");
static_assert(hud::kContract != hud::kRadarCar,
              "the chapter marker is the colour of the two dozen parked cars "
              "the radar already draws");
static_assert(hud::kContract != hud::kEdge,
              "the radar dot is drawn inside a border of this colour, so the "
              "marker would be a solid square of border");

/* ------------------------------------------------------------------------
 * Stage 2: traffic
 * ---------------------------------------------------------------------- */

/* Sprite palette slots. The engine keeps a bank of eight and every sprite
 * names the one it draws through. Slot 0 is deliberately unbound: nothing
 * draws through it, and CityDayNight starts tinting at slot 1. */
constexpr std::uint8_t kVehiclePaletteSlot    = 1;
/* + tint index. Slots 2..5 are the four street recolours and slot 6 is the
 * police uniform, kept out of PEDESTRIAN_TINTS so the random crowd can never
 * roll a uniform: the station spawns kPoliceTint by name. */
constexpr std::uint8_t kPedestrianPaletteSlot = 2;
constexpr std::uint8_t kPlayerPaletteSlot     = 7;   // the player, the gun, the pickups
static_assert(kPedestrianPaletteSlot + kPedestrianPaletteCount
                  <= kPlayerPaletteSlot,
              "the pedestrian palettes must not run into the player's slot");
static_assert(kPlayerPaletteSlot < 8,
              "the engine's sprite palette bank holds eight slots");

/* A lit window is a tile, not a sprite, so the city's lights live in the
 * BACKGROUND bank. NightLights.h is engine-free for host testing and carries
 * its own copy of which entry in which slot is a light; the generator emits
 * the art's copy. A drift between the two is not a bug anyone would report --
 * it is a city that leaves its windows dark at midnight. */
constexpr bool lightTableMatchesArt() {
    if (nightlights::kLightCount != scene::LIT_ENTRY_COUNT) {
        return false;
    }
    for (std::uint8_t i = 0; i < nightlights::kLightCount; ++i) {
        if (nightlights::kLightSlot[i] != scene::LIT_ENTRY_SLOT[i]
                || nightlights::kLightEntry[i] != scene::LIT_ENTRY_INDEX[i]) {
            return false;
        }
    }
    return true;
}
static_assert(lightTableMatchesArt(),
              "NightLights.h and the generated scene disagree about which "
              "palette entries are lights -- regenerate, or follow the art");

/* Entry 0 of every slot is the transparency sentinel. A light there would not
 * glow, it would fill in every hole in the tileset. */
constexpr bool noLightOnTheTransparentEntry() {
    for (std::uint8_t i = 0; i < nightlights::kLightCount; ++i) {
        if (nightlights::kLightEntry[i] == 0) {
            return false;
        }
    }
    return true;
}
static_assert(noLightOnTheTransparentEntry(),
              "entry 0 is transparent, not a colour, and cannot be a light");

/* Vehicles. All city_scene::NUM_VEHICLES are simulated all the time rather
 * than streamed, which is affordable because a parked car's step is a single
 * early return -- only the car being driven integrates anything. */
constexpr int kVehicleSpriteW = kVehicleSpriteSize;
constexpr int kVehicleSpriteH = kVehicleSpriteSize;

/* The collision box is NOT a constant here: it is VEHICLE_BOXES in the
 * generated VehicleSprites.h, one {x, y, w, h} per heading, measured from the
 * art. The drawing is not centred in its cell -- one blank row above the body,
 * two below -- so rotating it to face east moves the box by a pixel, and a
 * hand-copied constant would be wrong for two of the four headings in a way
 * nothing on screen would show. */

/* Driving. A car tops out at 1.25x a sprint, which feels like a car on a
 * 240x240 viewport without outrunning what the player can see coming: at the
 * 1.5x it started at, a car crossed the whole viewport in 1.2 s. */
constexpr int kVehicleMaxSpeedSub     = 640;   // 2.5 px/step = 156 px/s
constexpr int kVehicleMaxReverseSub   = 256;   // 1 px/step = 62.5 px/s
constexpr int kVehicleAccelSub        = 16;    // ~0.64 s to top speed
constexpr int kVehicleBrakeSub        = 44;    // ~0.23 s from top speed
constexpr int kVehicleFrictionSub     = 8;     // coasting, ~1.3 s to a stop

/* A turn keeps three quarters of the speed, and no more than one turn every
 * kVehicleTurnCooldownSteps: without the cooldown a held diagonal flips the
 * car between two headings every step and reads as a vibration. */
constexpr int kVehicleTurnKeepNum = 3;
constexpr int kVehicleTurnKeepDen = 4;
constexpr int kVehicleTurnCooldownSteps = 6;   // ~96 ms

/* Below this the car is manoeuvring, not driving: it can still nudge a
 * pedestrian aside but it does not run one over. */
constexpr int kVehicleSquashSpeedSub = 128;    // 0.5 px/step = 31 px/s

/* AudioCues.h keeps its own copy of the audible-crash threshold so the rule
 * module can decide whether a stop was loud enough without learning what a
 * vehicle sprite is. */
static_assert(audio_cues::kCrashAudibleSpeedSub > kVehicleSquashSpeedSub,
              "a crash worth hearing must be faster than a manoeuvring "
              "nudge, or a parking bump would play it every time");
static_assert(audio_cues::kCrashAudibleSpeedSub < kVehicleMaxSpeedSub,
              "a crash worth hearing must still be reachable at the car's "
              "own top speed");

/* How close the player has to stand to get in, measured between boxes. */
constexpr int kVehicleEnterRangePx = 6;

/* Traffic: a streamed population on top of the parked cars, spawned on a lane
 * just off screen and released once left far enough behind. Six is what a
 * 15x15-tile viewport can show without the street reading as a queue. These
 * cars do not accelerate: a 90-degree turn is only safe on a whole tile --
 * turn a pixel early and the car straddles two lanes for the rest of its life
 * -- so a constant speed that divides a tile exactly is what makes every
 * boundary reachable. The price, traffic starting and stopping instantly, the
 * eye cannot catch at 1 px per step. */
static_assert(lanes::kNearLaneOffset > 0
              && lanes::kFarLaneOffset < lanes::kBandWidthTiles - 1
              && lanes::kNearLaneOffset != lanes::kFarLaneOffset,
              "both lanes must be inside the band, and neither may be a kerb");

constexpr std::uint16_t kTrafficPoolSize = 6;
constexpr int kTrafficCruiseSpeedSub = 256;    // 1 px/step = 62.5 px/s

static_assert(((kTilePx << kSubPixelShift) % kTrafficCruiseSpeedSub) == 0,
              "traffic must land exactly on tile boundaries, or it can never "
              "turn without drifting off its lane");
static_assert(kTrafficCruiseSpeedSub > kVehicleSquashSpeedSub,
              "traffic below the lethal speed would drive through the crowd");
static_assert(kTrafficCruiseSpeedSub < kVehicleMaxSpeedSub,
              "the player must be able to overtake");

/* ADR-19: the brake-audible threshold is pinned to EXACTLY this speed, so a
 * screech cannot fire below the pace this demo calls driving. */
static_assert(audio_cues::kBrakeAudibleSpeedSub == kTrafficCruiseSpeedSub,
              "a screech worth hearing must require at least ordinary "
              "traffic speed, not merely inching forward");

/* How far ahead a driver looks for something to stop for, from the front of
 * its own box. A shade under one tile: far enough to stop with a gap rather
 * than nose to bumper, close enough not to brake for the junction it is about
 * to drive through. */
constexpr int kTrafficLookaheadPx = 14;

/* The odds of carrying straight on where a turn is available. Traffic that
 * turns at every junction never gets anywhere; traffic that never turns runs
 * the same six streets forever. */
constexpr int kTrafficStraightOddsIn = 3;

/* Half of kCarThreatPx, deliberately: twenty pixels is the lane and its kerb,
 * the people actually in danger. The player's car is the drama and gets the
 * wide radius; at forty, every pavement in the city would bolt each time
 * anything drove past. */
constexpr int kTrafficThreatPx = 20;

/* Getting hit by a car gets its own invulnerability window because a bumper
 * is not a bullet. A round arrives on the shooter's cooldown, so gunfire
 * paces itself; an overlapping car overlaps on all 62 steps a second it is
 * touching you, and one shared window long enough to survive that would have
 * halved every police round the player takes. */
constexpr std::uint8_t kCarImpactDamage      = 25;    // four to put you down
constexpr int          kCarImpactCooldownSteps = 45;  // ~0.7 s

/* The spawn ring, wider than the crowd's on both sides because a car covers
 * ground four times faster than somebody walking. */
constexpr int kTrafficSpawnMarginPx   = 64;
constexpr int kTrafficDespawnMarginPx = 176;
constexpr int kTrafficSpawnAttempts   = 8;     // per logic step, at most
constexpr int kTrafficSpawnClearPx    = 24;    // gap to anything already there

/* Police cars. How many are on the street is `wanted::patrolCars`, a rung of
 * the ladder rather than a property of a car. What one IS lives here: it takes
 * the turn at each junction that gets it CLOSER to the player rather than the
 * one the dice picked -- no pathfinder, because the lane table is already a
 * graph and "the legal exit that shortens the distance" is a good enough route
 * on a grid. Below is the colour row CAR_POLICE occupies in kVehicleSprites,
 * in the traffic pool's random spread since stage 6 as nothing but a paint
 * job. */
constexpr std::uint8_t kPoliceCarColor = 6;
static_assert(kPoliceCarColor < kVehicleColorCount,
              "the police livery is not in the vehicle sprite table");

/* Pedestrians: a pool, not an array. The city is 128x128 tiles and only the
 * dozen people near the camera are ever real. A slot is acquired when someone
 * walks into range and released when they leave it, or when the body has lain
 * in the road long enough. */
constexpr std::uint16_t kPedestrianPoolSize = 12;

constexpr int kPedWalkSpeedSub = 192;          // 0.75 px/step = 47 px/s

/* How long a pedestrian holds a direction and how often they stand still.
 * Re-rolled on every change, so a crowd does not fall into step. */
constexpr int kPedMinHoldSteps = 30;           // 0.5 s
constexpr int kPedMaxHoldSteps = 170;          // 2.7 s
constexpr int kPedIdleChanceIn = 4;            // one direction roll in four

/* Panic: long enough to clear the pavement and read as fright, short enough
 * that the street settles while the player is still standing there. Re-armed
 * rather than accumulated on a second scare, so a burst of fire keeps a crowd
 * running without locking them into a direction they cannot escape. The speed
 * sits between the two the demo already has -- faster than their stroll,
 * slower than the player's sprint. A civilian who outruns the person with the
 * gun is a civilian nobody believes. */
constexpr int kPedPanicSteps    = 90;    // 1.44 s
constexpr int kPedPanicSpeedSub = 320;   // 1.25 px/step = 78 px/s

/* One street pedestrian in six is an officer. A spawn rule, NOT a widened
 * tint roll: kPoliceTint stays out of PEDESTRIAN_TINTS so a uniform only
 * appears where something put it. An officer who is shot at does not flee --
 * civilians do that. They turn, stand, and return fire for kOfficerAlertSteps:
 * long enough that walking away is a decision, short enough that the street
 * settles if you stop. */
constexpr int kOfficerSpawnChanceIn = 6;
constexpr int kOfficerAlertSteps    = 312;   // 5.0 s

/* An officer shoots only when the player is lined up on one of the four axes
 * within half a tile of the perpendicular, which is about the player's own
 * width. A four-way aim fired at anything diagonal sprays past its target,
 * and that reads as broken rather than as unlucky. */
constexpr int kOfficerAimTolerancePx = 8;

/* Five police rounds, or two of your own if somebody turns one on you. */
constexpr std::uint8_t kPlayerHealth = 100;

/* A gunshot carries further than a car is seen, which is the right way round:
 * a pistol at four and a half tiles is loud, and a car you have not noticed
 * at two and a half is already too late. */
constexpr int kGunshotHearingPx = 72;
constexpr int kCarThreatPx      = 40;

/* A body stays where it fell for this long, then the slot goes back to the
 * pool. Long enough to be seen in the mirror, short enough that a busy
 * junction does not fill up with them. */
constexpr int kPedSquashLingerSteps = 500;     // 8 s

/* The kerb rule's release valve: somebody unable to move for this long gives
 * up on finding a crossing and walks into the road. The rule is a preference
 * laid over a map only ever proved connected for WALKABLE tiles, so a scrap of
 * pavement whose only exit is asphalt is a legal spawn and a dead end -- and a
 * walk cycle played on the spot until the pool recycles them is a far worse
 * artefact than one crossing mid-street. */
constexpr int kPedKerbGiveUpSteps = 60;        // ~1 s

/* An officer on a chase, between the player's two gaits. You can outrun the
 * police, but only by running -- which is what makes the sprint button mean
 * something once the star counter is up. */
constexpr int kOfficerChaseSpeedSub = 288;     // 1.125 px/step = 70 px/s

/* Mirrored into game/rules/Wanted.h so the pursuit range can be written as
 * "further than they can shoot" without that header pulling in the armoury.
 * test_wanted holds the two together, because `weapons::spec` is a runtime
 * lookup and a static_assert cannot reach it. */
constexpr int kOfficerWeaponRangePx = wanted::kOfficerWeaponRangePx;

/* Wanted.h carries its own copies of these three so its tables can be written
 * as claims -- "never slower than the base", "never as fast as a running
 * player", "never the whole road" -- and checked by a host test. */
static_assert(kOfficerChaseSpeedSub == wanted::kOfficerChaseSpeedSub,
              "the chase speed the star ladder is built on is not the one "
              "the officers actually walk at");
static_assert(kRunSpeedSub == wanted::kPlayerRunSpeedSub,
              "the run the star ladder is checked against is not the "
              "player's");
static_assert(kTrafficPoolSize == wanted::kStreetCarSlots,
              "the road the patrol table leaves room on is not the one the "
              "cars are parked in");

/* What a car is worth as cover, as a divisor on incoming police damage.
 * Halved rather than ignored: rounds that stopped dead at the windscreen made
 * "get in a car and wait" clear any level in the game. */
constexpr int kDrivingCoverDivisor = 2;

/* The spawn ring. New pedestrians appear inside this margin around the
 * viewport but never inside it, so nobody pops into existence on screen;
 * they are released once they fall outside the wider one. */
constexpr int kPedSpawnMarginPx   = 40;
constexpr int kPedDespawnMarginPx = 96;
constexpr int kPedSpawnAttempts   = 6;         // per logic step, at most

/* The first fill is the one exception to "never in view": keep this far clear
 * of the middle of the screen, where the player stands when the scene opens. */
constexpr int kPedPrimeClearPx = 40;

/* ------------------------------------------------------------------------
 * Stage 5: the weapon
 * ---------------------------------------------------------------------- */

/* A pedestrian and an officer are the same figure and take the same
 * punishment; the pistol's 50 damage makes that two shots. A car kills
 * outright rather than dealing damage -- being run over is not a quantity. */
constexpr std::uint8_t kPersonHealth = 100;
static_assert(kPersonHealth == weapons::kSmallestTargetHealth,
              "the weapon table balances its damage against a mirrored copy "
              "of this, and a shotgun that no longer drops somebody in one "
              "shell is a shotgun nobody would pick up");

/* Live projectiles across every weapon at once. Eight is four times what a
 * 14-step fire rate can put in the air within one bullet's 16-step life, so
 * the pool cannot run dry at the shipped rate. A plain array rather than an
 * ObjectPool: a projectile is trivially constructible, so pooling it would
 * buy indirection instead of memory. */
constexpr std::uint8_t kMaxProjectiles = 8;

/* The muzzle flash: long enough to be seen at 25 fps on the panel, short
 * enough that a held trigger reads as a series of shots rather than a lamp. */
constexpr int kMuzzleFlashSteps = 3;   // ~48 ms
constexpr int kMuzzleFlashPx    = 3;

/* Where a bullet is born, forward from the player's centre along the aim. Far
 * enough to clear the 16 px sprite so a shot never appears to come from the
 * middle of the chest, short enough not to clear a wall the player is
 * standing against -- which would shoot through it. */
constexpr int kMuzzleOffsetPx = 7;

/* There is no pickup respawn any more, and the absence is the design rather
 * than an omission: a collected gun stays collected for the whole run, the
 * street holds exactly one pistol, and everything after it is bought. See
 * WeaponPickups.h for the argument, and game/rules/Economy.h for what replaced
 * the walk. The map is what enforces the count -- generate_city_assets.py
 * fails the build on a second free weapon -- but the static_assert below is
 * what would notice a generator that grew one silently. */
static_assert(scene::NUM_WEAPON_PICKUPS == 1,
              "the city hands out exactly one free pistol; a second free "
              "weapon is a shop nobody has to visit");

/* kProjectileBoxPx lives in Weapon.h, beside the tunnelling check that is the
 * only reason its exact value matters. This is the other half of that check:
 * the weapon assumes nothing it must hit is shorter than kSmallestTargetPx. */
static_assert(kPlayerBoxHeight >= weapons::kSmallestTargetPx,
              "a projectile could step over the crowd's collision box");
static_assert(kPlayerBoxWidth >= weapons::kSmallestTargetPx,
              "a projectile could step over the crowd's collision box");

/* A pickup's footprint, centred in its tile. The sprite is drawn small, so
 * testing the whole 16 px cell would collect the gun from a step away --
 * which reads as the weapon jumping into the player's hand. */
constexpr int kPickupBoxPx = 8;

/* HUD hint for the one contextual button. */
constexpr int kHintHeight = 11;

/* The engine's built-in font at scale 1, measured rather than assumed. The
 * clock's five characters fit 30 px of its 34 px plate, and everything below
 * sizes itself off the same number. */
constexpr int kGlyphAdvancePx = 6;

/* Shared HUD geometry. Every right-aligned element -- the clock and the whole
 * column under it -- measures back from this edge, so it has to be declared
 * before the first of them. */
constexpr int kHudRightEdge = DISPLAY_WIDTH - 2;
constexpr int kHudPadPx     = 2;

/* The in-game clock, top right. It shares the top strip with the district
 * banner and is drawn after it: the banner's label is centred and never
 * reaches this far right, so the two overlap without either being lost. */
constexpr int kClockWidth   = 34;
constexpr int kClockHeight  = 11;
constexpr int kClockOriginX = kHudRightEdge - kClockWidth;
constexpr int kClockOriginY = 3;

/* ------------------------------------------------------------------------
 * The right-hand HUD column
 * ---------------------------------------------------------------------- */

/* Everything the player checks rather than watches stacks under the clock:
 * hit points, then the star row. The radar owns the top-left corner and is the
 * one element read while moving, so it gets the opposite side. Health is a
 * NUMBER, not a bar: a bar is read as a proportion, a number as a number,
 * which is what "can I take one more hit" wants -- and three digits cost less
 * width than a legible bar, which is what let the column fit at all. */

/* The drop marker, on the ground and on the radar. A ring rather than a
 * filled pad: a solid block hides the tile it is marking. */
constexpr int kMarkerSize      = 5;
constexpr int kMarkerRingPx    = 12;       // drawn size in the world
constexpr int kMarkerRadiusPx  = 10;       // close enough to have arrived

/* The caught notice, across the middle rather than in the top strip the
 * district banner uses: the banner is meant to be ignorable, and this one is
 * not. Eight pixels is the built-in font's cell height. */
constexpr int kBustedNoticeScale = 3;
constexpr int kBustedNoticeH     = 8 * kBustedNoticeScale + 2 * kHudPadPx;
constexpr int kBustedNoticeY     = (DISPLAY_HEIGHT - kBustedNoticeH) / 2;
static_assert(kBustedNoticeY > 0
                  && kBustedNoticeY + kBustedNoticeH < DISPLAY_HEIGHT,
              "the caught notice does not fit on the screen");

/* What the courier has been paid, directly under the clock and above
 * everything else in the column. The column splits in two at this row, and the
 * split is the order: the clock and the purse are checked BETWEEN decisions
 * and change slowly enough that a glance is the whole interaction, while
 * health, the stars, the delivery timer and the weapon are the four read WHILE
 * something is happening and are worth keeping adjacent rather than split by a
 * row that never moves in a fight.
 *
 * Four digits, matching economy::kMaxCash, with a currency mark in front. The
 * mark is not decoration: this column already draws bare digits for hit
 * points, for seconds left and for rounds left, so a fourth run of them would
 * read as a fourth count of something rather than as money. */
constexpr int kCashDigits   = 4;
constexpr int kCashHeight   = 11;
constexpr int kCashWidth    = kHudPadPx + (1 + kCashDigits) * kGlyphAdvancePx
                            + kHudPadPx;
constexpr int kCashOriginX  = kHudRightEdge - kCashWidth;
/* The one row that overlaps the plate above it by two pixels, because the one
 * above it is the clock and the clock sits in the top strip the district
 * banner shares. Everything below this stacks flush. */
constexpr int kCashOriginY  = kClockOriginY + kClockHeight - 2;

static_assert(kCashOriginX > kMinimapOriginX + kMinimapSize,
              "the cash readout would run into the radar");
static_assert(economy::kMaxCash < 10000,
              "the cash readout draws four digits; a fifth would be dropped "
              "silently and the purse would appear to stop growing");

/* Five pixels square is the smallest heart that stays a heart rather than a
 * red smudge -- and it has to be a heart, because three bare digits in the
 * corner of a driving game read as a lap counter. */
constexpr int kHeartSize     = 5;

constexpr int kHealthDigits  = 3;          // 100 is the widest it gets
constexpr int kHealthHeight  = 11;

/* The vest shares the heart's plate rather than taking a row of its own, and
 * that is a constraint rather than a preference: the column already runs from
 * the clock to the prompt strip, and `kWeaponOriginY` at the bottom of it is
 * asserted clear of that strip by a handful of pixels. There is no seventh row
 * to be had.
 *
 * Sharing is the better reading anyway. Armour is the first half of the same
 * number, spent before the hit points are, so the shield is drawn to the RIGHT
 * of them: the column is right-aligned, the eye lands on the outer edge first,
 * and the shield is what the next round hits. Two digits, because a vest is
 * worth less than a life and the assert below keeps it so. Drawn even at zero,
 * like the star row: a readout that appears only once it has a value is one
 * the player has never seen and does not go looking for. */
constexpr int kShieldSize    = 5;
constexpr int kArmorDigits   = 2;
static_assert(armor::kVestPoints < 100,
              "the armour readout draws two digits; a third would be dropped "
              "silently and the vest would appear to stop filling");

constexpr int kHealthWidth   = kHudPadPx + kHeartSize + kHudPadPx
                             + kHealthDigits * kGlyphAdvancePx + kHudPadPx
                             + kShieldSize + kHudPadPx
                             + kArmorDigits * kGlyphAdvancePx + kHudPadPx;
constexpr int kHealthOriginX = kHudRightEdge - kHealthWidth;
constexpr int kHealthOriginY = kCashOriginY + kCashHeight;

/* Where the shield and its digits start, measured from the plate's left edge
 * so the two halves cannot drift apart when either count changes width. */
constexpr int kArmorOffsetX  = kHudPadPx + kHeartSize + kHudPadPx
                             + kHealthDigits * kGlyphAdvancePx + kHudPadPx;

/* The star row, right-aligned with the two above it. A five-pixel diamond
 * rather than a five-pointed star: a real star at this size is nine pixels of
 * which four are the points, and the points are the first thing a 12-bit panel
 * loses. It is also the one LIGHT panel in the HUD, because its two states are
 * black and red: on the dark plate the clock and the heart use, the unearned
 * stars would be invisible until they had all been earned. */
constexpr int kStarSize     = 5;
constexpr int kStarGap      = 2;
constexpr int kStarRowW     = wanted::kMaxStars * (kStarSize + kStarGap)
                            - kStarGap;
constexpr int kStarWidth    = kStarRowW + 2 * kHudPadPx;
constexpr int kStarHeight   = kStarSize + 2 * kHudPadPx;
constexpr int kStarOriginX  = kHudRightEdge - kStarWidth;
constexpr int kStarOriginY  = kHealthOriginY + kHealthHeight;

/* The column has to clear the radar on the other side of the panel, or the
 * two overlap on the rows they share. */
static_assert(kStarOriginX > kMinimapOriginX + kMinimapSize,
              "the HUD column would run into the radar");
static_assert(kHealthOriginX > kMinimapOriginX + kMinimapSize,
              "the health readout would run into the radar");

/* Seconds left on the current delivery. Seconds rather than steps for one
 * reason beyond legibility: every change is a frame the panel cannot skip,
 * and the counter behind this one changes 62 times more often. */
constexpr int kTimerDigits  = 3;           // legs run to about 93 s
constexpr int kTimerHeight  = 11;
constexpr int kTimerWidth   = kHudPadPx + kMarkerSize + kHudPadPx
                            + kTimerDigits * kGlyphAdvancePx + kHudPadPx;
constexpr int kTimerOriginX = kHudRightEdge - kTimerWidth;
constexpr int kTimerOriginY = kStarOriginY + kStarHeight;

/* Under this many seconds the timer and its pip turn red. Ten is roughly
 * where a player on foot has to decide to find a car instead. */
constexpr int kTimerWarningSeconds = 10;

static_assert(kStarOriginY + kStarHeight < DISPLAY_HEIGHT,
              "the HUD column would run off the bottom of the panel");
static_assert(kTimerOriginY + kTimerHeight < DISPLAY_HEIGHT,
              "the delivery timer would run off the bottom of the panel");
static_assert(kTimerOriginX > kMinimapOriginX + kMinimapSize,
              "the delivery timer would run into the radar");

/* What is in the player's hands and how much is left. Only needed since the
 * shotgun, the first weapon that can run out. Widest row in the column: the
 * name is what makes the number mean anything, and seven characters covers
 * SHOTGUN. */
constexpr int kWeaponNameChars = 7;
constexpr int kWeaponAmmoChars = 3;
constexpr int kWeaponHeight    = 11;
constexpr int kWeaponWidth     = kHudPadPx
                               + (kWeaponNameChars + 1 + kWeaponAmmoChars)
                                 * kGlyphAdvancePx
                               + kHudPadPx;
constexpr int kWeaponOriginX   = kHudRightEdge - kWeaponWidth;
constexpr int kWeaponOriginY   = kTimerOriginY + kTimerHeight;

static_assert(kWeaponOriginX > kMinimapOriginX + kMinimapSize,
              "the weapon readout would run into the radar");
static_assert(kWeaponOriginY + kWeaponHeight < DISPLAY_HEIGHT - kHintHeight,
              "the weapon readout would run into the prompt strip");
static_assert(kCashOriginY + kCashHeight <= kHealthOriginY,
              "the cash row overlaps the health readout under it");

/* ------------------------------------------------------------------------
 * The shop's picker
 *
 * A modal panel over the room, in the shape the chess demo's promotion picker
 * uses: a bordered plate, a title, one row per choice with the current one
 * highlighted, and a footer naming the two buttons. Theirs is driven by a
 * finger and this one by a D-pad, so while it is up it OWNS the pad -- and the
 * corner shop is the one room where that costs the player nothing, because
 * there is nobody to shoot and nowhere to be. Sized from game/rules/Shop.h
 * rather than from a number typed here -- the row count, the longest label and
 * the widest price live over there with the catalogue -- so a fourth line
 * resizes the panel instead of being drawn off the bottom of it.
 * ---------------------------------------------------------------------- */
constexpr int kShopRowH      = 13;
constexpr int kShopTitleH    = 12;
constexpr int kShopFooterH   = 11;

/* The caret column. A marker rather than a highlight bar alone: on a 12-bit
 * panel a filled row and an unfilled one differ by one colour, and a caret
 * survives a photograph, a stream and a badly lit desk. */
constexpr int kShopMarkerW   = 6;

/* "$9999" -- the currency mark plus economy::kMaxCash's four digits. */
constexpr int kShopPriceChars = 5;

/* The footer naming the picker's two buttons. Counted from the literal itself
 * rather than measured, because the panel has to be wide enough for it at
 * compile time and TextLayout::measureWidthPx is a runtime call -- it could not
 * feed the constexpr width below or the static_asserts after it. */
constexpr char kShopFooterText[] = "FIRE BUY  RUN CLOSE";
constexpr int kShopFooterChars = sizeof(kShopFooterText) - 1;

constexpr int kShopRowNeedPx =
    kHudPadPx + kShopMarkerW
    + shop::kMaxLabelChars * kGlyphAdvancePx
    + kGlyphAdvancePx                              // a space between the two
    + kShopPriceChars * kGlyphAdvancePx + kHudPadPx;
constexpr int kShopFooterNeedPx =
    kHudPadPx + kShopFooterChars * kGlyphAdvancePx + kHudPadPx;

/* The wider of the two demands, so neither the longest row nor the footer is
 * ever cut. Text here is drawn from a pointer and never measured: an
 * over-long line is not wrapped, it runs off the edge of the plate. */
constexpr int kShopPickerW = kShopRowNeedPx > kShopFooterNeedPx
                                 ? kShopRowNeedPx : kShopFooterNeedPx;
constexpr int kShopPickerH = kHudPadPx + kShopTitleH
                           + shop::kLineCount * kShopRowH
                           + kShopFooterH + kHudPadPx;
constexpr int kShopPickerX = (DISPLAY_WIDTH - kShopPickerW) / 2;
constexpr int kShopPickerY = (DISPLAY_HEIGHT - kShopPickerH) / 2;
constexpr int kShopRowsY   = kShopPickerY + kHudPadPx + kShopTitleH;
/* Where the price ends, measured from the plate's RIGHT edge so it stays put
 * whatever the label beside it does. */
constexpr int kShopPriceX  = kShopPickerX + kShopPickerW - kHudPadPx
                           - kShopPriceChars * kGlyphAdvancePx;

static_assert(kShopPickerX > 0 && kShopPickerY > 0,
              "the shop picker is wider or taller than the panel");
static_assert(kShopPickerY + kShopPickerH < DISPLAY_HEIGHT - kHintHeight,
              "the shop picker overlaps the prompt strip it replaces");
static_assert(kShopPriceX
                  >= kShopPickerX + kHudPadPx + kShopMarkerW
                         + shop::kMaxLabelChars * kGlyphAdvancePx,
              "the longest label in the catalogue runs into the price beside "
              "it -- text is drawn from a pointer here, not measured");
static_assert(shop::kMaxDrawnPrice <= 9999,
              "the picker formats a price into four digits; a fifth would be "
              "dropped silently and the line would advertise the wrong sum");

/* ------------------------------------------------------------------------
 * Stage 8: the courier run
 * ---------------------------------------------------------------------- */

/* "Arrived" is measured from the drop tile's centre to whatever the camera is
 * following, so a car counts -- the point of a delivery run is that driving
 * it is better. */
static_assert(kMarkerRadiusPx < kTilePx, "a drop must be one tile, not two");

/* The player walks 1 px a step, so a 16 px tile costs exactly 16 steps. The
 * courier allowance is written against sixteen in a header that cannot see
 * this one; if the walk ever gets slower, every leg becomes one only a driver
 * can finish and nothing says so. */
static_assert((kTilePx << kSubPixelShift) <=
                  kWalkSpeedSub * mission::kOnFootStepsPerTile,
              "mission::kOnFootStepsPerTile is now shorter than a walked "
              "tile -- every courier leg would be unwalkable");

/* ------------------------------------------------------------------------
 * The main mission: chapter 1, "Boost"
 * ------------------------------------------------------------------------
 * `game/rules/Contract.h` is a state machine with no tables of its own: a
 * chapter, a vehicle index, a drop index, an officer index and a body count,
 * where every index indexes something the generator emitted. This is where the
 * two ends meet. The count is the exception, with no table behind it: chapter
 * 3's section below pins it against the CROWD, and its other two bounds (the
 * two magazines) live in `test_contract` where a host test can reach them.
 */

/* What keeps the phone quiet at the end of the story, and it is deliberately
 * NOT the phase. `contract::delivered` only stops at `Phase::Complete` once it
 * has run out of ENUM, so through two of this demo's releases there were more
 * chapters in that enum than payphones in the map: past the last shipped
 * chapter the phase went `Idle` again and `phoneLive` said yes -- to a chapter
 * with no phone, no car and no drop. The scene rings on this bound instead.
 *
 * The two numbers are equal today and the bound stays anyway: it is what made
 * chapters 2 and 3 a ROW HERE rather than a branch in the scene, and what a
 * fourth chapter added to the enum before its phone exists would land on. The
 * assert covers the other direction, which nothing on screen would show: more
 * rows than chapters is a payphone that can never ring. */
static_assert(scene::NUM_CONTRACT_PHONES
                  <= static_cast<std::uint8_t>(contract::Chapter::Count),
              "there are more payphones than chapters to hand out, so the "
              "last of them can never ring");
static_assert(scene::NUM_CONTRACT_PHONES > 0,
              "no payphone means no main mission, and nothing would say so -- "
              "the city would simply never offer a job");

/* The two indices chapter 1 hands to `contract::begin`. They are read back as
 * subscripts into `vehicles_` and into `MISSION_TARGETS` on the very next
 * frame, once to point the marker and once to test the delivery. */
static_assert(scene::BOOST_VEHICLE_INDEX < scene::NUM_VEHICLES,
              "chapter 1 sends the player to a car that is not parked "
              "anywhere on the map");
static_assert(scene::BOOST_DROP_INDEX < scene::NUM_MISSION_TARGETS,
              "chapter 1 ends at a drop the courier's own table does not have");

/* ------------------------------------------------------------------------
 * The main mission: chapter 2, "The Hit"
 * ---------------------------------------------------------------------- */

/* The one index chapter 2 hands to `contract::beginHit`, and this header is
 * the only place that sees both tables: the CITY's generator emits the mark --
 * it is a fact about the story -- and it subscripts the STATION's roster,
 * carried across the doorway untouched by `contract::State.mark`.
 *
 * Out of range is the quietest bug this chapter has. `StationStaff::mark`
 * refuses an index it does not have, so nobody is marked, `markIsDown` stays
 * false forever, and the chapter parks in `ToTarget` until the clock runs out
 * -- over a station full of officers, one of whom the player has certainly
 * already shot looking for the one that counts. */
static_assert(scene::HIT_OFFICER_INDEX < station::NUM_OFFICERS,
              "chapter 2 marks an officer the station never posts, so there "
              "is nobody in the room whose death ends the chapter");

/* The other half of the chapter's clock: how far the mark is from the mat, in
 * steps through the room's furniture rather than across it. Emitted beside the
 * index by the same flood fill that chose it, because the room's own plan is
 * the only thing that knows the desk is solid -- see `CityScene::takeHit`.
 *
 * Zero puts the mark on the mat, which makes the hit a doorway and a trigger
 * pull and the lobby a room the player never enters. The generator refuses it
 * too; this refuses a hand-edited header, the only way it could arrive here.
 * How much MORE than zero is not a compile-time question -- `OFFICERS` is a
 * `static const` array rather than `constexpr`, so C++ cannot read a post out
 * of it in a constant expression at all, and the walk-versus-straight-line
 * comparison lives in validate() where both numbers exist. */
static_assert(scene::HIT_OFFICER_STEPS_IN > 0,
              "chapter 2's mark is standing on the station's exit mat, so the "
              "hit can be finished from the doorway and the room it is about "
              "never happens");

/* ------------------------------------------------------------------------
 * The main mission: chapter 3, "Frenzy"
 * ------------------------------------------------------------------------
 * The one chapter with no table behind it. It names no car, no drop and no
 * officer but a COUNT, bounded by the pistol, the shotgun and the crowd. The
 * first two are pinned in `test_contract`; the third cannot be, because
 * `PedestrianPool` is an engine actor pool and `game/rules/` may not see one,
 * so it is pinned here -- the only place that sees both.
 */

/* The chapter must be finishable out of ONE POOL of people. The crowd is
 * `kPedestrianPoolSize` slots streamed around the camera; ask for more bodies
 * than it can hold at once and the chapter stops being a fight and becomes a
 * wait, the player clearing what is in front of them and then standing in an
 * empty street while the pool refills one slot per logic step behind the
 * corpses still fading in it.
 *
 * The two numbers are equal today by coincidence, not agreement -- this is
 * what turns it into one. Lower the pool for RAM, the obvious ESP32 move and
 * the one this demo is about, and the build fails here instead of shipping a
 * failure with no symptom: the clock runs out, the banner says JOB LOST
 * exactly as it would for a chapter the player fumbled, and nothing anywhere
 * says there was nobody left to shoot. */
static_assert(contract::kFrenzyTargets <= kPedestrianPoolSize,
              "chapter 3 asks for more bodies than the crowd can hold at "
              "once, so finishing it depends on respawns arriving rather than "
              "on the player");

/* And one body must be worth at least as long as one body BLOCKS. A pedestrian
 * who goes down keeps their slot for `kPedSquashLingerSteps` while they fade,
 * unless they leave the despawn margin first -- the common case, because a
 * rampage moves, and exactly why this is a bound rather than a schedule. The
 * player who stands still and clears the corner pays the full fade before the
 * pool can offer anybody new, and the budget per target has to cover that or
 * the chapter is unwinnable for the one playstyle "street corner" invites.
 *
 * Written against the linger rather than in seconds because the seconds are
 * what would drift: retune the fade and this line has to move with it, and the
 * only other thing that would notice is a player who cannot finish the last
 * chapter. */
static_assert(contract::kFrenzyStepsPerTarget >= kPedSquashLingerSteps,
              "chapter 3 budgets less time per body than a body spends "
              "blocking the crowd slot the next one has to come out of");

/* One agreement here is NOT asserted, for a language rule rather than an
 * oversight: a payphone must not share a cell with a courier drop, or the two
 * rings are drawn over each other -- and since the courier's is hidden while a
 * chapter runs, the collision would read as a marker that vanishes when the
 * job is taken. `CONTRACT_PHONES` and `MISSION_TARGETS` are emitted as
 * `static const` arrays of a struct, not `constexpr`, so C++ cannot read them
 * in a constant expression at all. generate_city_assets.py places every phone
 * a block clear of every other ring and is the only place that check can
 * live. */

/* ------------------------------------------------------------------------
 * Small integer helpers
 *
 * Here rather than in one scene's anonymous namespace because both spaces
 * need them: the camera clamp is in the base scene and the radar clamp is in
 * the city's.
 * ---------------------------------------------------------------------- */

constexpr int absInt(int value) {
    return value < 0 ? -value : value;
}

constexpr int clampInt(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

}  // namespace top_down_city
