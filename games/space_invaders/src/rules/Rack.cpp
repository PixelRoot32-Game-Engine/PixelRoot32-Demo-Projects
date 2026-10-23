#include "rules/Rack.h"

namespace spaceinvaders::rules {

void Rack::spawn(int16_t originX, int16_t originY) {
    (void)originX;
    (void)originY;
    // RED: implemented in T1.3
}

RackTick Rack::tick() {
    return RackTick{};  // RED: implemented in T1.3
}

void Rack::hold(uint16_t ticks) {
    (void)ticks;
    // RED: implemented in T1.3
}

void Rack::kill(int row, int col) {
    (void)row;
    (void)col;
    // RED: implemented in T1.3
}

bool Rack::anyAlive() const {
    return false;  // RED: implemented in T1.3
}

int Rack::aliveCount() const {
    return 0;  // RED: implemented in T1.3
}

int Rack::animationFrame() const {
    return 0;  // RED: implemented in T1.3
}

RackCell& Rack::cell(int row, int col) {
    return cells[row][col];
}

const RackCell& Rack::cell(int row, int col) const {
    return cells[row][col];
}

int16_t Rack::lowestAlienBottom() const {
    return 0;  // RED: implemented in T1.3
}

void Rack::shiftDown(int16_t dy) {
    (void)dy;
    // RED: implemented in T1.3
}

int Rack::direction() const {
    return marchDirection;
}

}  // namespace spaceinvaders::rules
