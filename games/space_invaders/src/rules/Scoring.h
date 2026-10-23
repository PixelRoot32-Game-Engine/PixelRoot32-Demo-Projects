#pragma once

#include <cstdint>

#include "rules/RulesConstants.h"

namespace spaceinvaders::rules {

/**
 * @class Scoring
 * @brief Score, lives, wave counter, session high score and the extra ship (S12).
 *
 * A run starts with three ships and one award at 1500 points (F14). The score
 * is four digits, so it clamps rather than overflowing into the HUD. The high
 * score is session state: reset() starts a new run but keeps it.
 */
class Scoring {
public:
    /**
     * @brief Starts a new run: score 0, three ships, wave 1, no extra ship.
     *
     * The high score is deliberately preserved, because it belongs to the
     * session rather than to the run.
     */
    void reset();

    /**
     * @brief Adds points to the current score.
     * @param points Points to add; the total clamps at 9999.
     */
    void add(uint16_t points);

    /**
     * @brief Current score.
     * @return Score in points, 0..9999.
     */
    uint16_t score() const;

    /**
     * @brief High byte of the current score.
     *
     * The enemy reload ladder is indexed by this value, so it is part of the
     * rules surface rather than a HUD helper.
     *
     * @return score() >> 8.
     */
    uint8_t highByte() const;

    /**
     * @brief Ships remaining.
     * @return Lives, 0..StartLives.
     */
    uint8_t lives() const;

    /**
     * @brief Spends one ship.
     *
     * A run ends when the count reaches zero; further calls cannot drive it
     * negative.
     */
    void loseLife();

    /**
     * @brief Claims the 1500-point extra ship if it is due.
     *
     * Returns true on exactly the first call made once the score is at least
     * ExtraShipScore; every later call returns false for the rest of the run.
     *
     * @return true when a ship was just awarded.
     */
    bool consumeExtraShip();

    /**
     * @brief Reports whether the extra ship has been awarded this run.
     * @return true after the first successful consumeExtraShip().
     */
    bool extraShipAwarded() const;

    /**
     * @brief Current wave, 1-based.
     * @return Wave number.
     */
    uint16_t wave() const;

    /**
     * @brief Advances to the next wave after the rack is cleared.
     *
     * The caller owns the formation offset and the shield rebuild; this only
     * moves the counter.
     */
    void nextWave();

    /**
     * @brief Best score seen this session.
     * @return High score in points.
     */
    uint16_t highScore() const;

    /**
     * @brief Raises the high score to the current score when it is higher.
     */
    void commitHighScore();

private:
    uint16_t scorePoints = 0;
    uint16_t highScorePoints = 0;
    uint8_t shipLives = StartLives;
    uint16_t waveNumber = 1;
    bool extraShipGiven = false;
};

}  // namespace spaceinvaders::rules
