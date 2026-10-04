#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "config.hpp"

// The part of the configuration that the serial console can change and save. Defaults come from config.hpp.

struct BannerSettings {
    float width;           // track units
    float steps_per_unit;  // half-steps per track unit
    int32_t home_offset;   // half-steps from home to slot 0
    bool reversed;
};

struct Settings {
    uint32_t step_interval_us;
    uint32_t start_step_interval_us;
    uint32_t ramp_steps;
    uint32_t settle_ms;
    uint32_t max_moving_motors;
    uint32_t sleep_seconds;
    uint32_t home_step_interval_us;
    float home_search_margin;
    uint32_t home_debounce_samples;
    uint32_t home_clear_steps;
    uint32_t home_overdrive_steps;
    uint32_t rehome_every_n_cycles;
    std::array<BannerSettings, config::BANNER_COUNT> banners;  // every set's banners, one set after another

    static Settings defaults();

    // Replaces these values with the saved ones; returns false, changing nothing, if none are saved.
    bool load();
    bool save() const;

    BannerSettings& banner(int set, int index) { return banners[config::first_banner(set) + index]; }
    const BannerSettings& banner(int set, int index) const { return banners[config::first_banner(set) + index]; }
};

// A whole-system setting that the console can show and change. Exactly one of `integer` and `real` is set.
struct SettingField {
    const char* name;
    uint32_t Settings::*integer;
    float Settings::*real;
    double min;
    double max;
    const char* description;
};

std::span<const SettingField> setting_fields();
