#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "banner_controller.hpp"
#include "motion.hpp"
#include "settings.hpp"

// Line-based serial commands for calibrating and testing. Type "help" for the list.
class Console {
public:
    Console(Settings& settings, Motion& motion, BannerController& banners);

    // Runs any complete command lines, waiting up to timeout_ms for input to arrive.
    void poll(uint32_t timeout_ms);

    // Set as soon as anything is typed, and cleared by "run" or a long spell without input.
    // The shuffle cycle waits while the console is paused.
    bool paused() const { return paused_; }

private:
    using Args = std::vector<std::string>;

    void execute(const std::string& line);
    void show() const;
    void set_setting(const Args& args);
    void set_banner(const Args& args);
    void jog(const Args& args);
    void home(const Args& args);
    // Switches to `next`, re-homing any set whose motor directions change.
    void apply(const Settings& next);

    Settings& settings_;
    Motion& motion_;
    BannerController& banners_;
    std::string line_;
    bool last_was_cr_ = false;
    bool paused_ = false;
    int64_t last_input_us_ = 0;
};
