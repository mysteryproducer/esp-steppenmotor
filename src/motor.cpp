#include "banners.hpp"
#include "platform.hpp"
#include "JVal.h"
#include <cmath>

namespace banners {

    Motor::Motor(JVal *json) {
        JErr err;
        JVal *json_diameter = JVal_get(json, &err, "diameter_mm");
        diameter_mm = (json_diameter==NULL || err.isError())?SHEAVE_DIAMETER_MM:json_diameter->getInt(&err);
        JVal *json_reversed = JVal_get(json, &err, "reversed");
        reversed = (json_reversed==NULL || err.isError())?false:json_reversed->getBoolean(&err);
        steps_per_mm = 1.0 / (HALF_STEPS_PER_REV / (std::numbers::pi * diameter_mm));

        JVal *pins_array = JVal_get(json, &err, "pins");
        if (pins_array != NULL && !err.isError() && pins_array->getType() == JVType::JVType_Array) {
            for (int i = 0; i < 4; ++i) {
                JVal *pin_val = JVal_get(pins_array, &err, "A", i);
                if (pin_val != NULL && !err.isError() && pin_val->getType() == JVType::JVType_Int) {
                    pins[i] = static_cast<uint8_t>(pin_val->getInt(&err));
                }
            }
        }
        configurePins();
    }

    void Motor::configurePins() {
        platform::GpioPinConfig config = { 
            .pin = 0, 
            .mode = platform::GpioPinMode::Output, 
            .pullup = false, 
            .level = false 
        };
        for (int i = 0; i < 4; ++i) {
            config.pin = pins[i] + i;
            platform::configurePin(config);
        }
    }

    void Motor::release() {
        for (int i = 0; i < 4; ++i) {
            platform::write_output(pins[i], 0);
        }
    }

    void Motor::write_pins() {
        for (int i = 0; i < 4; ++i) {
            platform::write_output(pins[i], (HALF_STEPS[phase] >> i) & 1);
        }
    }

    void Motor::move(int position_mm) {
        start_position = position;
        start_time = platform::now_us();
        int target_steps = static_cast<int>(std::lround(position_mm * steps_per_mm));
        if (reversed) {
            target_steps = -target_steps;
        }
        target_position += home_offset;
        // distance under acceleration/deceleration is 0.5 * a * at^2
        // a = MAX_SPEED / at
        // => distance = 0.5 * MAX_SPEED * at
        // => cruise distance = total distance - MAX_SPEED * at
        // => total time = 2 * at + cruise distance / MAX_SPEED
        // => total time = 2 * at + (total distance - MAX_SPEED * at) / MAX_SPEED
        // => total time = 2 * at + total distance / MAX_SPEED - at
        // => total time = at + total distance / MAX_SPEED
        travel_time_us = ACCELERATION_TIME + static_cast<uint32_t>(std::abs(target_position - start_position) / MAX_SPEED);
    }

    uint8_t Motor::next(long time_us) {
        //todo: this is AI code. Fix it.
        position = getAbsolutePosition(time_us);
        long distance = target_position - position;
        if (distance == 0) {
            return phase;
        }
        int step_direction = (distance > 0) ? 1 : -1;
        phase = (phase + step_direction + PHASES) % PHASES;
        write_pins();
        return phase;
    }

    int Motor::getAbsolutePosition(long time_us) {
        long elapsed_time = time_us - start_time;
        if (elapsed_time < 0) {
            return start_position;
        }
        if (elapsed_time >= travel_time_us) {
            return target_position;
        }
        //3 legs: acceleration, cruise, deceleration
        long travelled = start_position;
        long leg_time = std::min(elapsed_time, ACCELERATION_TIME);
        travelled += 0.5 * leg_time * leg_time * ACCELERATION;
        if (elapsed_time < ACCELERATION_TIME) {
            return travelled;
        }
        leg_time = std::min(elapsed_time - ACCELERATION_TIME, travel_time_us - 2 * ACCELERATION_TIME);
        travelled += leg_time * MAX_SPEED;
        if (elapsed_time < travel_time_us - ACCELERATION_TIME) {
            return travelled;
        }
        long decel_time = travel_time_us - elapsed_time;
        return target_position - 0.5 * decel_time * decel_time * ACCELERATION;
    } 



/*
def position(t, D, v_max, a):
    """
    t     : current time [s]
    D     : total distance [m]
    v_max : maximum velocity [m/s]
    a     : acceleration magnitude [m/s²]  (decel = -a)
    """
    t_acc = v_max / a
    d_acc = v_max * v_max / (2 * a)   # distance during accel (or decel)

    # --- Triangular case: can't reach v_max in distance D ---
    if 2 * d_acc > D:
        t_peak = (D / a) ** 0.5       # time to peak (= accel time = decel time)
        if t >= 2 * t_peak:
            return D
        if t < t_peak:
            return 0.5 * a * t * t
        left = 2 * t_peak - t
        return D - 0.5 * a * left * left

    # --- Trapezoidal case: reaches v_max ---
    t_cruise = (D - 2 * d_acc) / v_max
    t_total  = 2 * t_acc + t_cruise

    if t >= t_total:
        return D
    if t < t_acc:
        return 0.5 * a * t * t
    if t < t_acc + t_cruise:
        return d_acc + v_max * (t - t_acc)

    # Deceleration: count back from the end
    left = t_total - t
    return D - 0.5 * a * left * left
    */

}
