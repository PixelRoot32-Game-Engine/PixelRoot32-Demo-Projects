#include "PoolScene.h"

#include <core/Engine.h>

#include <cstdint>
#include <cstdio>

#include "pool/Fixed.h"
#include "pool/Geometry.h"
#include "pool/Tables.h"
#include "pool/Trig.h"

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace pool {

namespace {

// Button indices follow the InputConfig order in platforms/native.h and
// platforms/esp32_dev.h: Up, Down, Left, Right, A, B. Only six buttons are
// wired, so B doubles as pause where Start would sit on a NES pad.
constexpr uint8_t kBtnUp = 0;
constexpr uint8_t kBtnDown = 1;
constexpr uint8_t kBtnLeft = 2;
constexpr uint8_t kBtnRight = 3;
constexpr uint8_t kBtnA = 4;
constexpr uint8_t kBtnB = 5;

/// Row-scan span buffer for one felt-fill row. 16 covers table 1's simplest
/// shape (2 spans/row almost everywhere) with headroom for a row that
/// crosses a pocket notch and picks up extra crossings.
constexpr uint8_t kMaxRowSpans = 16;

/// Aim-guide length in pixels, drawn from the cue ball's edge.
constexpr int kAimGuidePx = 44;

/// HUD band height in pixels; the table area starts below it.
constexpr int kHudHeight = 40;

/// Catch-up cap per engine tick; beyond it the game slows down instead of
/// spiralling (each step stays deterministic, only wall-clock slips).
constexpr uint8_t kMaxStepsPerTick = 4;

// Target ball colors by ball number (index = number - 1); the cue is White.
constexpr pr32::graphics::Color kTargetColors[6] = {
    pr32::graphics::Color::Yellow,   pr32::graphics::Color::Orange,
    pr32::graphics::Color::LightRed, pr32::graphics::Color::Magenta,
    pr32::graphics::Color::Cyan,     pr32::graphics::Color::Green,
};

// 3x5 ball-number digits, one row per uint16_t. drawSprite() walks bits
// MSB-to-LSB (bit width-1 = left pixel, bit 0 = right pixel), so bit 2 is
// the left column here. 3 px wide centers on the 8 px ball with a pixel to
// spare, and the farthest glyph pixel (dx=1, dy=2) sits at 1+4=5 < 4^2, so
// every set pixel lands inside the ball.
constexpr uint16_t kDigitRows[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7},  // 0
    {0x2, 0x6, 0x2, 0x2, 0x7},  // 1
    {0x7, 0x1, 0x2, 0x4, 0x7},  // 2
    {0x7, 0x1, 0x3, 0x1, 0x7},  // 3
    {0x5, 0x5, 0x7, 0x1, 0x1},  // 4
    {0x7, 0x4, 0x3, 0x1, 0x7},  // 5
    {0x7, 0x4, 0x7, 0x5, 0x7},  // 6
    {0x7, 0x1, 0x1, 0x2, 0x2},  // 7
    {0x7, 0x5, 0x7, 0x5, 0x7},  // 8
    {0x7, 0x5, 0x7, 0x1, 0x7},  // 9
};

constexpr pr32::graphics::Sprite kDigitGlyphs[10] = {
    {kDigitRows[0], 3, 5}, {kDigitRows[1], 3, 5}, {kDigitRows[2], 3, 5}, {kDigitRows[3], 3, 5},
    {kDigitRows[4], 3, 5}, {kDigitRows[5], 3, 5}, {kDigitRows[6], 3, 5}, {kDigitRows[7], 3, 5},
    {kDigitRows[8], 3, 5}, {kDigitRows[9], 3, 5},
};

// Micro-font covering '0'..'9' for ball numbers; flash-resident like any
// other const asset, zero RAM cost.
constexpr pr32::graphics::Font kDigitFont = {kDigitGlyphs, '0', '9', 3, 5, 4, 6, nullptr, 0, 0, 0};

}  // namespace

void PoolScene::startGameFromMenu() {
    game_.startGame();
    audio_.playSfx(PoolSfx::UiConfirm);
    snapshotAudioBaseline();
}

void PoolScene::setMusicEnabled(bool enabled) {
    audio_.setMusicMuted(!enabled);
    audio_.playSfx(PoolSfx::UiConfirm);
}

void PoolScene::setSfxEnabled(bool enabled) {
    audio_.setSfxMuted(!enabled);
    audio_.playSfx(PoolSfx::UiConfirm);
}

void PoolScene::playMenuTick() {
    audio_.playSfx(PoolSfx::AimTick);
}

void PoolScene::updateMenuAudio(unsigned long dtMs) {
    audio_.update(dtMs);
}

void PoolScene::init() {
    Scene::init();  // resetState() + physicsScheduler.init() — always call base first
    // init() is idempotent by Scene contract: re-running it deals a fresh
    // stage-1 game instead of stacking state on the previous one.
    accUnits_ = 0;
    paused_ = false;
    // Every entry into this scene (boot or START GAME) deals a fresh run
    // from stage 1: engine.setScene() always runs init() first.
    tableError_ = game_.newGame(1);
    audio_.reset();
    audio_.stopMusic();
    audioKey_ = 0;
    snapshotAudioBaseline();
}

void PoolScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);
    if (tableError_ != pool::TableError::None) {
        return;
    }
    handleInput();
    if (!paused_) {
        stepSimulation(deltaTime);
    }
    // A pause held through the final shot must not freeze the GameOver
    // screen (or mute its jingle): losing/winning always releases it.
    if (game_.state() == pool::State::GameOver && paused_) {
        paused_ = false;
        audio_.setMusicPaused(false);
    }
    audio_.update(deltaTime);
    if (!paused_) {
        pollAudio();
        syncMusic();
    }
}

void PoolScene::handleInput() {
    auto& input = engine.getInputManager();
    const pool::State state = game_.state();
    // B pauses anywhere a shot can be live; Menu and GameOver confirm with A,
    // so B needs no second meaning there.
    if (input.isButtonPressed(kBtnB) &&
        (state == pool::State::Aiming || state == pool::State::Shooting ||
         state == pool::State::BallsMoving)) {
        paused_ = !paused_;
        audio_.playSfx(PoolSfx::PauseToggle);
        audio_.setMusicPaused(paused_);
    }
    if (paused_) {
        return;
    }
    switch (state) {
        case pool::State::Menu:
            // The menu lives in PoolMenuScene; reaching Menu here only
            // happens on a direct boot into this scene, so A just starts.
            if (input.isButtonPressed(kBtnA)) {
                startGameFromMenu();
            }
            break;
        case pool::State::Aiming:
            // Aim tracks the held level for smooth sweeps; power steps on the
            // press edge so one tap is exactly one meter level. Both tick
            // softly, like the original's cursor chirp (cooldown-gated).
            if (input.isButtonDown(kBtnLeft)) {
                game_.aimLeft();
                audio_.playSfx(PoolSfx::AimTick);
            }
            if (input.isButtonDown(kBtnRight)) {
                game_.aimRight();
                audio_.playSfx(PoolSfx::AimTick);
            }
            if (input.isButtonPressed(kBtnUp)) {
                game_.powerUp();
                audio_.playSfx(PoolSfx::UiConfirm);
            }
            if (input.isButtonPressed(kBtnDown)) {
                game_.powerDown();
                audio_.playSfx(PoolSfx::UiConfirm);
            }
            if (input.isButtonPressed(kBtnA)) {
                if (game_.shoot()) {
                    audio_.playSfx(PoolSfx::CueStrike);
                }
            }
            break;
        case pool::State::GameOver:
            // Won means the last stage fell: restart the run from stage 1. A
            // loss retries the same stage instead; a broken reload surfaces
            // the red screen instead of a partial table.
            if (input.isButtonPressed(kBtnA)) {
                const uint8_t stage = game_.won() ? 1 : game_.stage();
                const pool::TableError err = game_.newGame(stage);
                if (err == pool::TableError::None) {
                    game_.startGame();
                    audio_.playSfx(PoolSfx::UiConfirm);
                    snapshotAudioBaseline();
                } else {
                    tableError_ = err;
                }
            }
            break;
        default:
            break;  // Live shot states run hands-off.
    }
}

void PoolScene::stepSimulation(unsigned long deltaTime) {
    // Exact 60 Hz pacing in integer units: ms x 60 piles up 1000 units per
    // frame with no drift, so the sim neither races nor drags the display.
    accUnits_ += deltaTime * 60;
    uint8_t steps = 0;
    while (accUnits_ >= 1000 && steps < kMaxStepsPerTick) {
        game_.stepFrame();
        accUnits_ -= 1000;
        ++steps;
    }
    if (steps == kMaxStepsPerTick) {
        accUnits_ = 0;
    }
}

void PoolScene::pollAudio() {
    if (game_.world().drainCushionHit()) {
        audio_.playSfx(PoolSfx::CushionThud);
    }
    if (game_.world().drainBallHit()) {
        audio_.playSfx(PoolSfx::BallClick);
    }
    // A 1->0 edge on the active mask is a pocket: the cue scratches, a
    // target drops. Stage advances re-deal every ball (0->1 edges), so they
    // never read as pockets here.
    uint8_t mask = 0;
    bool cuePocketed = false;
    bool targetPocketed = false;
    for (uint8_t i = 0; i < game_.world().ballCount(); ++i) {
        const pool::Ball& ball = game_.world().ball(i);
        if (ball.active) {
            mask |= static_cast<uint8_t>(1u << i);
        } else if ((prevActiveMask_ & static_cast<uint8_t>(1u << i)) != 0) {
            if (ball.number == 0) {
                cuePocketed = true;
            } else {
                targetPocketed = true;
            }
        }
    }
    prevActiveMask_ = mask;
    if (cuePocketed) {
        audio_.playSfx(PoolSfx::Scratch);
    } else if (targetPocketed) {
        audio_.playSfx(PoolSfx::PocketDrop);
    }
    // A fouled shot scores nothing and may drop the total; the scratch
    // already covers the cue case, so only buzz otherwise.
    if (game_.score() < prevScore_ && !cuePocketed) {
        audio_.playSfx(PoolSfx::Foul);
    }
    prevScore_ = game_.score();
}

void PoolScene::syncMusic() {
    const pool::State state = game_.state();
    if (state == pool::State::EvaluateShot || state == pool::State::NextTurn) {
        return;  // Transient single-frame states keep whatever is playing.
    }
    uint8_t want = 0;
    if (state == pool::State::Aiming || state == pool::State::Shooting ||
        state == pool::State::BallsMoving) {
        want = game_.stage();
    } else if (state == pool::State::GameOver) {
        want = game_.won() ? 11 : 12;
    }
    if (want == audioKey_) {
        return;
    }
    // Clearing a stage into the next one earns the arpeggio; the win fanfare
    // covers the final clear on its own.
    if (want >= 1 && want <= 10 && audioKey_ >= 1 && audioKey_ <= 10 &&
        want == static_cast<uint8_t>(audioKey_ + 1)) {
        audio_.playSfx(PoolSfx::StageClear);
    }
    audioKey_ = want;
    if (want >= 1 && want <= 10) {
        audio_.playStageMusic(want);
    } else if (want == 11) {
        audio_.playWinJingle();
    } else if (want == 12) {
        audio_.playLoseJingle();
    } else {
        audio_.stopMusic();
    }
}

void PoolScene::snapshotAudioBaseline() {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < game_.world().ballCount(); ++i) {
        if (game_.world().ball(i).active) {
            mask |= static_cast<uint8_t>(1u << i);
        }
    }
    prevActiveMask_ = mask;
    prevScore_ = game_.score();
}

void PoolScene::draw(pr32::graphics::Renderer& renderer) {
    if (tableError_ != pool::TableError::None) {
        // A solid color is the only way to surface a bad table instead of
        // drawing from a partially-written Table or crashing.
        renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(),
                                     pr32::graphics::Color::Red);
        Scene::draw(renderer);
        return;
    }

    drawTable(renderer);
    drawBalls(renderer);
    drawAim(renderer);
    drawHud(renderer);

    Scene::draw(renderer);
}

void PoolScene::drawTable(pr32::graphics::Renderer& renderer) const {
    const pool::Table& table = game_.table();
    // Rail color across the whole table area; the felt fill below overpaints
    // every row inside the border, leaving the rail visible only at the
    // pocket notches and the outer margin.
    renderer.drawFilledRectangle(0, kHudHeight, renderer.getLogicalWidth(), renderer.getLogicalHeight() - kHudHeight,
                                 pr32::graphics::Color::DarkRed);

    const pool::Polyline& border = pool::tableForStage(game_.stage()).border;
    int16_t xs[kMaxRowSpans];
    for (int16_t y = kHudHeight; y < renderer.getLogicalHeight(); ++y) {
        const uint8_t count = pool::rowSpans(border, y, xs, kMaxRowSpans);
        for (uint8_t i = 0; static_cast<uint8_t>(i + 1) < count; i = static_cast<uint8_t>(i + 2)) {
            renderer.drawFilledRectangle(xs[i], y, xs[i + 1] - xs[i], 1, pr32::graphics::Color::Navy);
        }
    }

    // Stage 2+ obstacle blocks read as solid rail so they never pass for felt.
    const pool::TableDef& def = pool::tableForStage(game_.stage());
    for (uint8_t o = 0; o < def.obstacleCount; ++o) {
        for (int16_t y = kHudHeight; y < renderer.getLogicalHeight(); ++y) {
            const uint8_t count = pool::rowSpans(def.obstacles[o], y, xs, kMaxRowSpans);
            for (uint8_t i = 0; static_cast<uint8_t>(i + 1) < count; i = static_cast<uint8_t>(i + 2)) {
                renderer.drawFilledRectangle(xs[i], y, xs[i + 1] - xs[i], 1,
                                             pr32::graphics::Color::DarkRed);
            }
        }
    }

    for (uint8_t s = 0; s < table.segmentCount; ++s) {
        const pool::Segment& seg = table.segments[s];
        const int x1 = static_cast<int>(pool::toPixel(seg.ax));
        const int y1 = static_cast<int>(pool::toPixel(seg.ay));
        const int x2 = static_cast<int>(pool::toPixel(seg.ax + static_cast<int32_t>(seg.ex) * pool::kPxScale));
        const int y2 = static_cast<int>(pool::toPixel(seg.ay + static_cast<int32_t>(seg.ey) * pool::kPxScale));
        renderer.drawLine(x1, y1, x2, y2, pr32::graphics::Color::Blue);
    }

    for (uint8_t p = 0; p < table.pocketCount; ++p) {
        const pool::Pocket& pocket = table.pockets[p];
        renderer.drawFilledCircle(static_cast<int>(pool::toPixel(pocket.x)),
                                  static_cast<int>(pool::toPixel(pocket.y)), 5, pr32::graphics::Color::Black);
    }
}

void PoolScene::drawBalls(pr32::graphics::Renderer& renderer) const {
    const pool::World& world = game_.world();
    const uint8_t expected = game_.nextExpected();
    const int radius = static_cast<int>(pool::toPixel(pool::kBallRadiusRaw));
    for (uint8_t i = 0; i < world.ballCount(); ++i) {
        const pool::Ball& ball = world.ball(i);
        if (!ball.active) {
            continue;
        }
        const int x = static_cast<int>(pool::toPixel(ball.x));
        const int y = static_cast<int>(pool::toPixel(ball.y));
        pr32::graphics::Color color = pr32::graphics::Color::Gray;
        if (ball.number == 0) {
            color = pr32::graphics::Color::White;
        } else if (ball.number <= 6) {
            color = kTargetColors[ball.number - 1];
        }
        renderer.drawFilledCircle(x, y, radius, color);
        renderer.drawCircle(x, y, radius, pr32::graphics::Color::Black);
        // Ring the ball the rules want next, so "in order" needs no HUD text.
        if (ball.number != 0 && ball.number == expected) {
            renderer.drawCircle(x, y, radius + 2, pr32::graphics::Color::White);
        }
        // NES-style number inside the ball: the 3x5 glyph centers on the ball
        // center with one pixel of margin on every side.
        if (ball.number >= 1 && ball.number <= 9) {
            const char label[2] = {static_cast<char>('0' + ball.number), '\0'};
            renderer.drawText(label, static_cast<int16_t>(x - 1), static_cast<int16_t>(y - 2),
                              pr32::graphics::Color::Black, 1, &kDigitFont);
        }
    }
}

void PoolScene::drawAim(pr32::graphics::Renderer& renderer) const {
    if (game_.state() != pool::State::Aiming) {
        return;
    }
    const pool::Ball& cue = game_.world().ball(0);
    if (!cue.active) {
        return;
    }
    // Unit direction from the fixed-point angle table (screen-clockwise, y
    // down, matching Trig.h), scaled once into raw units per axis.
    const int32_t cos = pool::cosQ14(game_.angle());
    const int32_t sin = pool::sinQ14(game_.angle());
    const int32_t gapRaw = pool::kBallRadiusRaw + 2 * pool::kPxScale;
    const int32_t lenRaw = kAimGuidePx * pool::kPxScale;
    const int sx = static_cast<int>(pool::toPixel(cue.x + static_cast<int32_t>(pool::divRound(
                                                               static_cast<int64_t>(cos) * gapRaw, pool::kQ14One))));
    const int sy = static_cast<int>(pool::toPixel(cue.y + static_cast<int32_t>(pool::divRound(
                                                               static_cast<int64_t>(sin) * gapRaw, pool::kQ14One))));
    const int ex = static_cast<int>(pool::toPixel(cue.x + static_cast<int32_t>(pool::divRound(
                                                               static_cast<int64_t>(cos) * lenRaw, pool::kQ14One))));
    const int ey = static_cast<int>(pool::toPixel(cue.y + static_cast<int32_t>(pool::divRound(
                                                               static_cast<int64_t>(sin) * lenRaw, pool::kQ14One))));
    renderer.drawLine(sx, sy, ex, ey, pr32::graphics::Color::White);
}

void PoolScene::drawHud(pr32::graphics::Renderer& renderer) const {
    using Color = pr32::graphics::Color;
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), kHudHeight, Color::Black);

    char line[32];
    std::snprintf(line, sizeof(line), "STAGE %d SCORE %d", static_cast<int>(game_.stage()),
                  static_cast<int>(game_.score()));
    renderer.drawText(line, 4, 3, Color::White, 1);
    std::snprintf(line, sizeof(line), "SHOTS %d BALL %d POW %d", static_cast<int>(game_.shotsLeft()),
                  static_cast<int>(game_.nextExpected()), static_cast<int>(game_.power()));
    renderer.drawText(line, 4, 13, Color::White, 1);

    // Power meter: 10 cells, filled up to the current level.
    for (uint8_t i = 0; i < pool::kMaxPower; ++i) {
        const Color cell = i < game_.power() ? Color::Green : Color::Gray;
        renderer.drawFilledRectangle(4 + i * 7, 24, 5, 8, cell);
    }
    renderer.drawText("A SHOOT B PAUSE", 82, 24, Color::Gray, 1);

    if (paused_) {
        renderer.drawTextCentered("PAUSED", 110, Color::Yellow, 2);
        return;
    }
    if (game_.state() == pool::State::Menu) {
        renderer.drawTextCentered("POOL", 100, Color::White, 2);
        renderer.drawTextCentered("PRESS A TO START", 130, Color::Gray, 1);
    } else if (game_.state() == pool::State::GameOver) {
        if (game_.won()) {
            renderer.drawTextCentered("YOU WIN!", 100, Color::Yellow, 2);
        } else {
            renderer.drawTextCentered("GAME OVER", 100, Color::LightRed, 2);
        }
        renderer.drawTextCentered("PRESS A", 130, Color::Gray, 1);
    }
}

}  // namespace pool
