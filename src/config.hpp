#pragma once

// Hardware configuration, plus the defaults for everything the serial console can change. Console changes
// are kept separately (see settings.hpp); the console's "defaults" command reverts to the values here.

#include <algorithm>
#include <cstdint>
#include <numbers>
#include <span>

#if defined(BANNERS_STM32)
#include BANNERS_BOARD_HEADER  // the board chosen by the stm32 build, e.g. stm32/boards/weact_c552ve.hpp
#endif

namespace config {

inline constexpr int NO_PIN = -1;

// ---------------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------------
//
// Coils are driven through a chain of 74HC595s, one per two motors. Motor m is wired to chip m / 2: Q0-Q3
// for even m and Q4-Q7 for odd m, to IN1-IN4 of its ULN2003 board. Chip 0's SER is fed by the
// microcontroller; each chip's Q7' feeds the next chip's SER.
//
// Fit a 10k pull-up from /OE to 3V3: the registers power up with random contents, and the pull-up keeps
// every coil off until the firmware has cleared them. Set SR_ENABLE_PIN to NO_PIN if /OE is tied to GND.

#if defined(ESP_PLATFORM)

inline constexpr int SR_DATA_PIN = 7;    // 74HC595 SER   (pin 14), driven as SPI MOSI
inline constexpr int SR_CLOCK_PIN = 6;   // 74HC595 SRCLK (pin 11), driven as SPI SCLK
inline constexpr int SR_LATCH_PIN = 5;   // 74HC595 RCLK  (pin 12)
inline constexpr int SR_ENABLE_PIN = 4;  // 74HC595 /OE   (pin 13)
inline constexpr int HOME_PIN_A = 10;
inline constexpr int HOME_PIN_B = 1;
inline constexpr int DIRECT_GPIO_PINS[][4] = {{4, 5, 6, 7}};

#elif defined(PICO_RP2350)

// SR_CLOCK_PIN and SR_DATA_PIN must be the SCK and TX pins of one SPI block: GPIO n belongs to
// SPI((n / 8) % 2), and is SCK when n % 4 == 2 or TX when n % 4 == 3. On the WeAct RP2350B Core board,
// GP23 is the user button and GP25 the LED.
inline constexpr int SR_DATA_PIN = 19;   // 74HC595 SER   (pin 14), SPI0 TX
inline constexpr int SR_CLOCK_PIN = 18;  // 74HC595 SRCLK (pin 11), SPI0 SCK
inline constexpr int SR_LATCH_PIN = 17;  // 74HC595 RCLK  (pin 12)
inline constexpr int SR_ENABLE_PIN = 16; // 74HC595 /OE   (pin 13)
inline constexpr int HOME_PIN_A = 14;
inline constexpr int HOME_PIN_B = 15;
inline constexpr int DIRECT_GPIO_PINS[][4] = {{2, 3, 4, 5}};

static_assert(SR_CLOCK_PIN % 4 == 2 && SR_DATA_PIN % 4 == 3 && SR_CLOCK_PIN / 8 % 2 == SR_DATA_PIN / 8 % 2,
              "SR_CLOCK_PIN and SR_DATA_PIN must be the SCK and TX pins of the same SPI block");

#elif defined(BANNERS_STM32)

// Pins come from the board header, where they are written as port and index (PA7, PC13 and so on).
using board::DIRECT_GPIO_PINS;
using board::HOME_PIN_A;
using board::HOME_PIN_B;
using board::SR_CLOCK_PIN;
using board::SR_DATA_PIN;
using board::SR_ENABLE_PIN;
using board::SR_LATCH_PIN;

#else
#error "Unsupported target"
#endif

enum class OutputMode {
    ShiftRegister,  // the 74HC595 chain described above
    DirectGpio,     // four GPIOs per motor from DIRECT_GPIO_PINS, for bench-testing a motor or two
};
// Define BANNERS_DIRECT_GPIO at build time to drive the coils straight from pins instead.
#if defined(BANNERS_DIRECT_GPIO)
inline constexpr OutputMode OUTPUT_MODE = OutputMode::DirectGpio;
#else
inline constexpr OutputMode OUTPUT_MODE = OutputMode::ShiftRegister;
#endif

// ---------------------------------------------------------------------------
// Motion defaults (intervals are per half-step)
// ---------------------------------------------------------------------------

inline constexpr uint32_t STEP_INTERVAL_US = 1200;        // cruising speed; below ~1000 us steps get skipped
inline constexpr uint32_t START_STEP_INTERVAL_US = 3000;  // speed at the start and end of every move
inline constexpr uint32_t RAMP_STEPS = 200;               // half-steps spent ramping between the two
// How long a motor's coils stay on before its first step (so the rotor lines up with the phase it was
// released on) and after its last step.
inline constexpr uint32_t SETTLE_MS = 20;
// Each moving 5 V 28BYJ-48 draws up to ~200 mA, so this sets the peak current from the motor supply.
inline constexpr uint32_t MAX_MOVING_MOTORS = 4;

inline constexpr uint32_t SLEEP_SECONDS = 300;  // pause between shuffles

// ---------------------------------------------------------------------------
// Homing
// ---------------------------------------------------------------------------

enum class HomeMode {
    // Each banner trips a sensor just beyond its slot-0 position, in overtravel so that no banner rests
    // on it in normal use. All sensors in a set share one input, so a set's banners home one at a time.
    Sensor,
    // No sensor: drive every banner past slot 0 until it stalls against an end stop.
    HardStop,
    // No homing: at power-up banner 0 must be in slot 0, banner 1 in slot 1, and so on.
    None,
};

inline constexpr uint32_t HOME_STEP_INTERVAL_US = 1500;
// Homing gives up after this multiple of a banner's longest possible distance from home.
inline constexpr float HOME_SEARCH_MARGIN = 1.2f;
// Consecutive sensor readings, one per step interval, needed to accept a trigger.
inline constexpr uint32_t HOME_DEBOUNCE_SAMPLES = 3;
// Sensor mode: if a set's sensor is already triggered at power-up (power was lost mid-homing), banners
// are moved this far away from home, one at a time, until it releases.
inline constexpr uint32_t HOME_CLEAR_STEPS = 400;
// HardStop mode: how far past the expected stop to drive when re-homing banners whose position is known.
inline constexpr uint32_t HOME_OVERDRIVE_STEPS = 200;
// Re-home every N shuffles to correct lost steps (0 = only at power-up). Positions survive low-power
// sleep, so this is only needed if steps are being missed.
inline constexpr uint32_t REHOME_EVERY_N_CYCLES = 12;

// ---------------------------------------------------------------------------
// Serial console
// ---------------------------------------------------------------------------

inline constexpr uint32_t CONSOLE_BOOT_WINDOW_SECONDS = 5;  // time to press Enter after power-up
// A paused shuffle cycle resumes by itself after this long without console input.
inline constexpr uint32_t CONSOLE_IDLE_RESUME_MINUTES = 15;

// ---------------------------------------------------------------------------
// Banners
// ---------------------------------------------------------------------------
//
// Distances along the track are in "track units" of your choosing, measured from slot 0:
//  - millimetres, with steps_per_unit = half_steps_per_mm(pulley diameter), or
//  - half-steps, measured directly, with steps_per_unit = 1 (only if every pulley in the set matches).
// A banner's position is the total width of the banners before it in the current order.

struct Banner {
    int motor;             // coil output index (see Pins)
    float width;           // track units, including any gap to the next banner
    float steps_per_unit;  // half-steps this banner's pulley takes to move it one track unit
    int32_t home_offset;   // half-steps from the home sensor (or end stop) to this banner's slot-0 position
    bool reversed;         // set if positive steps move this banner towards home
};

struct Set {
    const char* name;
    std::span<const Banner> banners;
    HomeMode home_mode;
    int home_pin;           // HomeMode::Sensor only; the internal pull-up is enabled
    int home_active_level;  // 1: normally-closed switches in series; 0: open-drain sensors in parallel
};

// 64 half-steps per motor revolution x the 63.684:1 gearbox (it is not exactly the advertised 64:1).
inline constexpr double HALF_STEPS_PER_REV = 4075.7728;

constexpr float half_steps_per_mm(double pulley_diameter_mm) {
    return static_cast<float>(HALF_STEPS_PER_REV / (std::numbers::pi * pulley_diameter_mm));
}

// Placeholder mechanics: replace with your measurements.
inline constexpr float BANNER_WIDTH_MM = 100.0f;
inline constexpr float STEPS_PER_MM = half_steps_per_mm(12.0);
inline constexpr int32_t HOME_OFFSET = 300;

inline constexpr Banner SET_A_BANNERS[] = {
    // motor  width           steps_per_unit  home_offset  reversed
    {0,      BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
    {1,      BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
    {2,      BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
    {3,      BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
    {4,      BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
    {5,      BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
    {6,      BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
};

inline constexpr Banner SET_B_BANNERS[] = {
    // motor  width           steps_per_unit  home_offset  reversed
    {7,      BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
    {8,      BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
    {9,      BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
    {10,     BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
    {11,     BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
    {12,     BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
    {13,     BANNER_WIDTH_MM, STEPS_PER_MM,   HOME_OFFSET, false},
};

inline constexpr Set SETS[] = {
    // name banners        home_mode         home_pin     home_active_level
    {"A",   SET_A_BANNERS, HomeMode::Sensor, HOME_PIN_A,  1},
    {"B",   SET_B_BANNERS, HomeMode::Sensor, HOME_PIN_B,  1},
};

// ---------------------------------------------------------------------------
// Derived values: no need to edit
// ---------------------------------------------------------------------------

inline constexpr int MOTOR_COUNT = [] {
    int count = 0;
    for (const Set& set : SETS) {
        for (const Banner& banner : set.banners) {
            count = std::max(count, banner.motor + 1);
        }
    }
    return count;
}();

inline constexpr int BANNER_COUNT = [] {
    int count = 0;
    for (const Set& set : SETS) {
        count += static_cast<int>(set.banners.size());
    }
    return count;
}();

// Index of a set's first banner when every set's banners are listed one after another.
constexpr int first_banner(int set) {
    int index = 0;
    for (int i = 0; i < set; ++i) {
        index += static_cast<int>(SETS[i].banners.size());
    }
    return index;
}

}  // namespace config
