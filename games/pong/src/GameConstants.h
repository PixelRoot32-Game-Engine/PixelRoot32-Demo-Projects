#pragma once
#include <cstdint>

namespace pong {

    constexpr uint8_t BTN_UP = 0;
    constexpr uint8_t BTN_DOWN = 1;
    constexpr uint8_t BTN_START = 4; 

    constexpr int PADDLE_WIDTH = 10;
    constexpr int PADDLE_HEIGHT = 50;
    constexpr int BALL_RADIUS = 6;
    constexpr float BALL_SPEED = 120.0f;
    constexpr int SCORE_TO_WIN = 5;

    constexpr float AI_TARGET_OFFSET = 6.0f;
    constexpr float AI_MAX_SPEED = 90.0f;
    constexpr float AI_MOVEMENT_THRESHOLD = 0.5f;
    
    constexpr int PONG_PLAY_AREA_HEIGHT = 160;

    constexpr bool STRESS_TEST_MODE = false;
    constexpr int STRESS_BASE_BALL_COUNT = 24;
    
    // CCD Test Mode - Ultra fast ball to test Continuous Collision Detection
    constexpr bool CCD_TEST_MODE = false;
    constexpr float CCD_TEST_BALL_SPEED = 600.0f;  // 5x normal speed (120px/s)

}
