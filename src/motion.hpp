#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "coil_outputs.hpp"
#include "config.hpp"
#include "settings.hpp"

// One motor movement for Motion::run().
struct Move {
    int motor;
    int32_t target;             // absolute position, in half-steps
    uint32_t step_interval_us;  // cruising speed
    // If set, the move ends early once this input has read stop_level for stop_samples consecutive steps.
    int stop_pin = config::NO_PIN;
    int stop_level = 1;
    int stop_samples = 1;

    bool stopped_by_input = false;  // set by Motion::run()
};

// Drives 28BYJ-48 motors in half-steps and keeps track of their positions.
class Motion {
public:
    Motion(CoilOutputs& outputs, const Settings& settings);

    int32_t position(int motor) const { return motors_[motor].position; }
    void set_position(int motor, int32_t position) { motors_[motor].position = position; }
    void set_reversed(int motor, bool reversed) { motors_[motor].reversed = reversed; }

    // Runs the moves, up to Settings::max_moving_motors at a time, and returns once all have finished.
    // Each motor is released as soon as its own move is done.
    void run(std::span<Move> moves);

    // Turns every coil off.
    void release_all();

private:
    struct Motor {
        int32_t position = 0;
        uint8_t phase = 0;  // index into the half-step sequence, kept while released
        bool reversed = false;
    };

    void step(int motor, int direction);

    CoilOutputs& outputs_;
    const Settings& settings_;
    std::vector<Motor> motors_;
};
