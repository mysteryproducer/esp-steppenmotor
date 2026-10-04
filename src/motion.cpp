#include "motion.hpp"

#include <algorithm>
#include <cstdlib>
#include <deque>

#include "platform.hpp"

namespace {

// Half-step coil patterns, bit 0 = IN1.
constexpr uint8_t HALF_STEP[8] = {0b0001, 0b0011, 0b0010, 0b0110, 0b0100, 0b1100, 0b1000, 0b1001};

struct Active {
    Move* move;
    int direction;
    uint32_t steps_total;
    uint32_t steps_taken = 0;
    int64_t next_us;        // when this motor next needs attention
    int input_samples = 0;  // consecutive readings of the stop input at its stop level
    bool holding = false;   // finished moving; coils stay on until next_us, then it is released
    bool done = false;
};

// Ramps linearly between the start interval and the cruising interval at both ends of a move.
int64_t step_interval(const Active& active, const Settings& settings) {
    const int64_t cruise = active.move->step_interval_us;
    const int64_t start = std::max<int64_t>(settings.start_step_interval_us, cruise);
    if (settings.ramp_steps == 0) {
        return cruise;
    }
    const uint32_t from_end = std::min(active.steps_taken, active.steps_total - active.steps_taken);
    const int64_t ramp = std::min(from_end, settings.ramp_steps);
    return start - (start - cruise) * ramp / settings.ramp_steps;
}

}  // namespace

Motion::Motion(CoilOutputs& outputs, const Settings& settings)
    : outputs_(outputs), settings_(settings), motors_(config::MOTOR_COUNT) {}

void Motion::run(std::span<Move> moves) {
    std::deque<Move*> queued;
    for (Move& move : moves) {
        move.stopped_by_input = false;
        if (move.target != motors_[move.motor].position) {
            queued.push_back(&move);
        }
    }
    if (queued.empty()) {
        return;
    }

    outputs_.set_enabled(true);

    const int64_t settle_us = int64_t{settings_.settle_ms} * 1000;
    const size_t max_active = std::max<size_t>(settings_.max_moving_motors, 1);
    std::vector<Active> active;
    while (!queued.empty() || !active.empty()) {
        const int64_t now = platform::now_us();
        bool changed = false;

        // Energise waiting motors on the phase they were released on, so their rotors don't jump.
        while (!queued.empty() && active.size() < max_active) {
            Move* move = queued.front();
            queued.pop_front();
            const Motor& motor = motors_[move->motor];
            const int32_t distance = move->target - motor.position;
            active.push_back({
                .move = move,
                .direction = distance > 0 ? 1 : -1,
                .steps_total = static_cast<uint32_t>(std::abs(distance)),
                .next_us = now + settle_us,
            });
            outputs_.set(move->motor, HALF_STEP[motor.phase]);
            changed = true;
        }

        for (Active& a : active) {
            if (now < a.next_us) {
                continue;
            }
            Move& move = *a.move;

            if (a.holding) {
                outputs_.set(move.motor, 0);
                a.done = true;
                changed = true;
                continue;
            }

            if (move.stop_pin != config::NO_PIN && platform::read_input(move.stop_pin) == move.stop_level) {
                // Stand still while confirming, so noise can't shift where the motor stops.
                if (++a.input_samples >= move.stop_samples) {
                    move.stopped_by_input = true;
                    a.holding = true;
                    a.next_us = now + settle_us;
                } else {
                    a.next_us = now + move.step_interval_us;
                }
                continue;
            }
            a.input_samples = 0;

            step(move.motor, a.direction);
            changed = true;
            if (++a.steps_taken == a.steps_total) {
                a.holding = true;
                a.next_us = now + settle_us;
            } else {
                // Keep to the schedule, but never bunch up steps after running late.
                const int64_t interval = step_interval(a, settings_);
                a.next_us = std::max(a.next_us + interval, now + interval / 2);
            }
        }

        if (changed) {
            outputs_.flush();
        }
        std::erase_if(active, [](const Active& a) { return a.done; });
        if (active.empty()) {
            continue;
        }

        int64_t next_us = active.front().next_us;
        for (const Active& a : active) {
            next_us = std::min(next_us, a.next_us);
        }
        platform::sleep_until_us(next_us);
    }
}

void Motion::release_all() {
    for (int motor = 0; motor < config::MOTOR_COUNT; ++motor) {
        outputs_.set(motor, 0);
    }
    outputs_.flush();
    outputs_.set_enabled(false);
}

void Motion::step(int motor_index, int direction) {
    Motor& motor = motors_[motor_index];
    motor.position += direction;
    motor.phase = (motor.phase + (motor.reversed ? -direction : direction)) & 7;
    outputs_.set(motor_index, HALF_STEP[motor.phase]);
}
