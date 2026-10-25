#include "banners.hpp"

#include <array>
#include <cinttypes>
#include <cmath>
#include <numeric>
#include <string>
#include <utility>

#include "platform.hpp"

namespace banners {

const char* const TAG = "banner controller";

// Uniform in [0, bound), without modulo bias.
uint32_t random_below(uint32_t bound) {
    const uint32_t limit = UINT32_MAX - UINT32_MAX % bound;
    uint32_t value;
    do {
        value = platform::random_u32();
    } while (value >= limit);
    return value % bound;
}

// A uniformly random order that differs from `previous`.
std::vector<int> random_order(size_t count, const std::vector<int>& previous) {
    std::vector<int> order(count);
    do {
        std::iota(order.begin(), order.end(), 0);
        for (size_t i = count; i > 1; --i) {
            std::swap(order[i - 1], order[random_below(i)]);
        }
    } while (count > 1 && order == previous);
    return order;
}

}  // namespace

BannerController::BannerController(Motion& motion, const Settings& settings) : motion_(motion), settings_(settings) {
    for (int index = 0; index < static_cast<int>(std::size(config::SETS)); ++index) {
        const config::Set& cfg = config::SETS[index];
        sets_.push_back({.cfg = &cfg, .index = index});
        if (cfg.home_mode == config::HomeMode::Sensor) {
//            platform::configure_input_pullup(cfg.home_pin);
            configurePin(platform::GpioPinConfig{.pin = cfg.home_pin, .mode = platform::GpioPinMode::Input, .pullup = true, .level = false});
        }
    }
}

void BannerController::home(bool force, int only_set) {
    sync_directions();
    for (SetState& set : sets_) {
        if (set.cfg->home_mode == config::HomeMode::None && !set.homed && (only_set < 0 || only_set == set.index)) {
            LOG_WARN(TAG, "set %s: not homing, assuming banners are in slot order", set.cfg->name);
            set.order.resize(set.cfg->banners.size());
            std::iota(set.order.begin(), set.order.end(), 0);
            const std::vector<int32_t> positions = slot_positions(set);
            for (size_t i = 0; i < positions.size(); ++i) {
                motion_.set_position(set.cfg->banners[i].motor, positions[i]);
            }
            set.homed = true;
        }
    }
    home_against_stops(force, only_set);
    home_with_sensors(force, only_set);
}

void BannerController::home_against_stops(bool force, int only_set) {
    std::vector<Move> moves;
    std::vector<SetState*> homing;
    for (SetState& set : sets_) {
        if (set.cfg->home_mode != config::HomeMode::HardStop || (only_set >= 0 && only_set != set.index) ||
            (set.homed && !force)) {
            continue;
        }
        homing.push_back(&set);
        for (int i = 0; i < static_cast<int>(set.cfg->banners.size()); ++i) {
            const int motor = set.cfg->banners[i].motor;
            const int32_t position = motion_.position(motor);
            const int32_t distance = set.homed ? position + static_cast<int32_t>(settings_.home_overdrive_steps)
                                               : search_distance(set, i);
            moves.push_back({
                .motor = motor,
                .target = position - distance,
                .step_interval_us = settings_.home_step_interval_us,
            });
        }
    }
    if (homing.empty()) {
        return;
    }

    LOG_INFO(TAG, "homing against end stops");
    motion_.run(moves);
    for (SetState* set : homing) {
        for (const config::Banner& banner : set->cfg->banners) {
            motion_.set_position(banner.motor, 0);
        }
        set->homed = true;
        set->order.clear();
    }
}

void BannerController::home_with_sensors(bool force, int only_set) {
    struct Homing {
        SetState* set;
        bool ok;
    };
    std::vector<Homing> homing;
    for (SetState& set : sets_) {
        if (set.cfg->home_mode != config::HomeMode::Sensor || (only_set >= 0 && only_set != set.index) ||
            (set.homed && !force)) {
            continue;
        }
        LOG_INFO(TAG, "set %s: homing", set.cfg->name);
        homing.push_back({&set, clear_sensor(set)});
    }

    // A set's banners share its sensor so they home one at a time, but separate sets home side by side.
    for (int i = 0;; ++i) {
        std::vector<Move> seeks;
        std::vector<Homing*> seeking;
        for (Homing& h : homing) {
            if (!h.ok || i >= static_cast<int>(h.set->cfg->banners.size())) {
                continue;
            }
            const int motor = h.set->cfg->banners[i].motor;
            seeks.push_back({
                .motor = motor,
                .target = motion_.position(motor) - search_distance(*h.set, i),
                .step_interval_us = settings_.home_step_interval_us,
                .stop_pin = h.set->cfg->home_pin,
                .stop_level = h.set->cfg->home_active_level,
                .stop_samples = static_cast<int>(settings_.home_debounce_samples),
            });
            seeking.push_back(&h);
        }
        if (seeks.empty()) {
            break;
        }
        motion_.run(seeks);

        // Zero each banner where it tripped the sensor, then send it to slot 0 to free the sensor again.
        std::vector<Move> backoffs;
        for (size_t k = 0; k < seeks.size(); ++k) {
            Homing& h = *seeking[k];
            const int motor = h.set->cfg->banners[i].motor;
            if (!seeks[k].stopped_by_input) {
                LOG_ERROR(TAG, "set %s banner %d: home sensor not found", h.set->cfg->name, i);
                h.ok = false;
                continue;
            }
            if (h.set->homed) {
                LOG_INFO(TAG, "set %s banner %d: position was out by %" PRId32 " half-steps", h.set->cfg->name, i,
                         motion_.position(motor));
            }
            motion_.set_position(motor, 0);
            backoffs.push_back({
                .motor = motor,
                .target = tuning(*h.set, i).home_offset,
                .step_interval_us = settings_.step_interval_us,
            });
        }
        motion_.run(backoffs);

        for (Homing* h : seeking) {
            if (h->ok && sensor_active(*h->set)) {
                LOG_ERROR(TAG, "set %s banner %d: still on the home sensor at slot 0; increase its offset",
                          h->set->cfg->name, i);
                h->ok = false;
            }
        }
    }

    for (Homing& h : homing) {
        h.set->homed = h.ok;
        h.set->order.clear();
        if (h.ok) {
            LOG_INFO(TAG, "set %s: homed", h.set->cfg->name);
        }
    }
}

bool BannerController::sensor_active(const SetState& set) const {
    if (set.cfg->home_mode != config::HomeMode::Sensor) {
        return false;
    }
    // Majority vote, to ride out noise picked up from the motor wiring.
    int active = 0;
    for (int i = 0; i < 5; ++i) {
        active += platform::read_input(set.cfg->home_pin) == set.cfg->home_active_level;
        platform::busy_wait_us(1000);
    }
    return active >= 3;
}

void BannerController::sync_directions() {
    for (const SetState& set : sets_) {
        for (int i = 0; i < static_cast<int>(set.cfg->banners.size()); ++i) {
            motion_.set_reversed(set.cfg->banners[i].motor, tuning(set, i).reversed);
        }
    }
}

// How far a banner may need to travel to reach home: from its furthest slot, plus a margin.
int32_t BannerController::search_distance(const SetState& set, int banner_index) const {
    const BannerSettings& banner = tuning(set, banner_index);
    float others_width = -banner.width;
    for (int i = 0; i < static_cast<int>(set.cfg->banners.size()); ++i) {
        others_width += tuning(set, i).width;
    }
    return static_cast<int32_t>(std::lround((banner.home_offset + others_width * banner.steps_per_unit) *
                                            settings_.home_search_margin));
}

// Each banner's target position (indexed by banner) for the set's current order.
std::vector<int32_t> BannerController::slot_positions(const SetState& set) const {
    std::vector<int32_t> positions(set.cfg->banners.size());
    float edge = 0;  // track units from slot 0 to the current slot
    for (int b : set.order) {
        const BannerSettings& banner = tuning(set, b);
        positions[b] = banner.home_offset + static_cast<int32_t>(std::lround(edge * banner.steps_per_unit));
        edge += banner.width;
    }
    return positions;
}

void BannerController::shuffle() {
    sync_directions();
    std::vector<Move> moves;
    for (SetState& set : sets_) {
        if (!set.homed) {
            LOG_WARN(TAG, "set %s: position unknown, leaving it alone", set.cfg->name);
            continue;
        }
        set.order = random_order(set.cfg->banners.size(), set.order);

        std::string order_text;
        for (int b : set.order) {
            order_text += ' ' + std::to_string(b);
        }
        LOG_INFO(TAG, "set %s: new order%s", set.cfg->name, order_text.c_str());

        const std::vector<int32_t> positions = slot_positions(set);
        for (size_t i = 0; i < positions.size(); ++i) {
            moves.push_back({
                .motor = set.cfg->banners[i].motor,
                .target = positions[i],
                .step_interval_us = settings_.step_interval_us,
            });
        }
    }
    motion_.run(moves);
}

void BannerController::jog(int set, int banner, int32_t steps) {
    sync_directions();
    const int motor = sets_[set].cfg->banners[banner].motor;
    Move move = {
        .motor = motor,
        .target = motion_.position(motor) + steps,
        .step_interval_us = settings_.step_interval_us,
    };
    motion_.run(std::span(&move, 1));
}

void BannerController::invalidate(int set) {
    sets_[set].homed = false;
    sets_[set].order.clear();
}
