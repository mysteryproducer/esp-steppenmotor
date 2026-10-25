#include "banners.hpp"

#include <array>
#include <cinttypes>
#include <cmath>
#include <numeric>
#include <string>
#include <utility>

#include "platform.hpp"

namespace banners {

const char* const TAG = "banner sets";

// A banner left on the sensor (e.g. power was lost mid-homing) would hide every other banner's trigger,
// so step banners away from home one at a time until the sensor releases.
bool BannerSet::clear_sensor(const HomingSensor& sensor) {
    if (!sensor_active(set)) {
        return true;
    }
    LOG_WARN(TAG, "set %s: home sensor already active, stepping banners off it", set.cfg->name);
    for (const config::Banner& banner : set.cfg->banners) {
        Move move = {
            .motor = banner.motor,
            .target = motion_.position(banner.motor) + static_cast<int32_t>(settings_.home_clear_steps),
            .step_interval_us = settings_.home_step_interval_us,
        };
        motion_.run(std::span(&move, 1));
        if (!sensor_active(set)) {
            return true;
        }
    }
    LOG_ERROR(TAG, "set %s: home sensor stuck active; check its wiring", set.cfg->name);
    return false;
}

}