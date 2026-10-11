#include "app.hpp"

#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include "banners.hpp"
#include "coil_outputs.hpp"
#include "config.hpp"
#include "console.hpp"
#include "motion.hpp"
#include "platform.hpp"
#include "settings.hpp"

namespace {

const char* const TAG = "app";

// Keeps the console available until `seconds` have passed or it is paused. Always polls at least once.
void wait_with_console(Console& console, uint32_t seconds) {
    const int64_t end_us = platform::now_us() + int64_t{seconds} * 1'000'000;
    do {
        console.poll(100);
    } while (!console.paused() && platform::now_us() < end_us);
}

}  // namespace

void run_banners() {
    platform::console_begin();

    Settings settings = Settings::defaults();
    if (settings.load()) {
        LOG_INFO(TAG, "using saved settings");
    }

    const auto outputs = make_coil_outputs();
    Motion motion(*outputs, settings);
    BannerController banners(motion, settings);
    Console console(settings, motion, banners);

    printf("\nBanner shuffler. Press Enter within %" PRIu32 " s for the console.\n",
           config::CONSOLE_BOOT_WINDOW_SECONDS);
    wait_with_console(console, config::CONSOLE_BOOT_WINDOW_SECONDS);

    for (uint32_t cycle = 0;; ++cycle) {
        while (console.paused()) {
            console.poll(1000);
        }

        // Low-power sleep keeps RAM, so positions survive it and only power-up needs a full home.
        const bool rehome = settings.rehome_every_n_cycles > 0 && cycle > 0 &&
                            cycle % settings.rehome_every_n_cycles == 0;
        banners.home(rehome);
        banners.shuffle();
        motion.release_all();  // unloaded, the gearbox holds the banners where they are

        if (settings.sleep_seconds == 0 || platform::console_host_attached()) {
            // Low-power sleep would drop the USB connection, so stay awake and keep the console open.
            wait_with_console(console, settings.sleep_seconds);
        } else {
            LOG_INFO(TAG, "sleeping for %" PRIu32 " s", settings.sleep_seconds);
            platform::low_power_sleep(settings.sleep_seconds);
        }
    }
}
