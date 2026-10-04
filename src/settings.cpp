#include "settings.hpp"

#include "platform.hpp"

namespace {

// Bump when a field's meaning changes; changes in size are detected without it.
constexpr uint32_t SETTINGS_VERSION = 1;
constexpr uint32_t MAGIC = 0x53524E42;  // "BNRS"
constexpr uint32_t LAYOUT = SETTINGS_VERSION << 24 | config::BANNER_COUNT << 16 | sizeof(Settings);

struct Stored {
    uint32_t magic;
    uint32_t layout;
    Settings settings;
    uint32_t checksum;
};

// FNV-1a
uint32_t checksum(const Settings& settings) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&settings);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof settings; ++i) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

constexpr SettingField FIELDS[] = {
    {"step_us", &Settings::step_interval_us, nullptr, 500, 100000, "cruising time per half-step (us)"},
    {"start_us", &Settings::start_step_interval_us, nullptr, 500, 100000, "time per half-step as a move starts and ends (us)"},
    {"ramp_steps", &Settings::ramp_steps, nullptr, 0, 10000, "half-steps spent speeding up and slowing down"},
    {"settle_ms", &Settings::settle_ms, nullptr, 0, 1000, "coils held on before and after each move (ms)"},
    {"max_moving", &Settings::max_moving_motors, nullptr, 1, 64, "motors allowed to move at once"},
    {"sleep_s", &Settings::sleep_seconds, nullptr, 0, 86400, "pause between shuffles (s); 0 shuffles continuously"},
    {"home_us", &Settings::home_step_interval_us, nullptr, 500, 100000, "time per half-step while homing (us)"},
    {"home_margin", nullptr, &Settings::home_search_margin, 1, 5, "homing gives up after this multiple of the longest travel"},
    {"home_debounce", &Settings::home_debounce_samples, nullptr, 1, 50, "sensor readings needed to accept a trigger"},
    {"home_clear", &Settings::home_clear_steps, nullptr, 0, 100000, "half-steps to move banners off a sensor found active at power-up"},
    {"home_overdrive", &Settings::home_overdrive_steps, nullptr, 0, 100000, "half-steps past a known end stop when re-homing"},
    {"rehome_every", &Settings::rehome_every_n_cycles, nullptr, 0, 100000, "re-home after this many shuffles (0 = only at power-up)"},
};

}  // namespace

Settings Settings::defaults() {
    Settings settings = {
        .step_interval_us = config::STEP_INTERVAL_US,
        .start_step_interval_us = config::START_STEP_INTERVAL_US,
        .ramp_steps = config::RAMP_STEPS,
        .settle_ms = config::SETTLE_MS,
        .max_moving_motors = config::MAX_MOVING_MOTORS,
        .sleep_seconds = config::SLEEP_SECONDS,
        .home_step_interval_us = config::HOME_STEP_INTERVAL_US,
        .home_search_margin = config::HOME_SEARCH_MARGIN,
        .home_debounce_samples = config::HOME_DEBOUNCE_SAMPLES,
        .home_clear_steps = config::HOME_CLEAR_STEPS,
        .home_overdrive_steps = config::HOME_OVERDRIVE_STEPS,
        .rehome_every_n_cycles = config::REHOME_EVERY_N_CYCLES,
        .banners = {},
    };
    for (int set = 0; set < static_cast<int>(std::size(config::SETS)); ++set) {
        const auto& banners = config::SETS[set].banners;
        for (int i = 0; i < static_cast<int>(banners.size()); ++i) {
            const config::Banner& banner = banners[i];
            settings.banner(set, i) = {banner.width, banner.steps_per_unit, banner.home_offset, banner.reversed};
        }
    }
    return settings;
}

bool Settings::load() {
    Stored stored;
    if (!platform::storage_load(reinterpret_cast<uint8_t*>(&stored), sizeof stored) || stored.magic != MAGIC ||
        stored.layout != LAYOUT || stored.checksum != checksum(stored.settings)) {
        return false;
    }
    *this = stored.settings;
    return true;
}

bool Settings::save() const {
    Stored stored{};  // zeroed, so padding bytes are the same when the checksum is verified
    stored.magic = MAGIC;
    stored.layout = LAYOUT;
    stored.settings = *this;
    stored.checksum = checksum(stored.settings);
    return platform::storage_save(reinterpret_cast<const uint8_t*>(&stored), sizeof stored);
}

std::span<const SettingField> setting_fields() {
    return FIELDS;
}
