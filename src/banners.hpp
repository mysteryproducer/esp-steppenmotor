#pragma once

#include <cstdint>
#include <vector>
#include <math.h>

#include "config.hpp"
#include "motion.hpp"
#include "settings.hpp"

namespace banners {

// Moves each set of banners into a random order, homing them first when their positions are unknown.
class BannerController {
public:
    BannerController(Motion& motion, const Settings& settings);

    // Homes every set whose positions are unknown, or every set if `force` is true. A non-negative
    // `only_set` limits this to that set.
    void home(bool force, int only_set = -1);

    // Moves every homed set into a new random order.
    void shuffle();

    // Moves one banner by `steps` half-steps; positive is away from home.
    void jog(int set, int banner, int32_t steps);

    // Forgets a set's positions, so it is homed before it next moves.
    void invalidate(int set);

    bool homed(int set) const { return sets_[set].homed; }
    const std::vector<int>& order(int set) const { return sets_[set].order; }
    // Debounced reading of a set's home sensor; false if the set has none.
    bool sensor_active(int set) const { return sensor_active(sets_[set]); }

private:
    struct SetState {
        const config::Set* cfg;
        int index;
        bool homed = false;
        std::vector<int> order{};  // order[slot] = banner index; empty straight after homing
    };

    const BannerSettings& tuning(const SetState& set, int banner) const { return settings_.banner(set.index, banner); }
    void sync_directions();
    void home_against_stops(bool force, int only_set);
    void home_with_sensors(bool force, int only_set);
    bool clear_sensor(const SetState& set);
    bool sensor_active(const SetState& set) const;
    int32_t search_distance(const SetState& set, int banner) const;
    std::vector<int32_t> slot_positions(const SetState& set) const;

    Motion& motion_;
    const Settings& settings_;
    std::vector<SetState> sets_;
};


class Motor {
    public:
        static constexpr uint8_t HALF_STEPS[8] = {0b0001, 0b0011, 0b0010, 0b0110, 0b0100, 0b1100, 0b1000, 0b1001};
        static const double SHEAVE_DIAMETER_MM = 26;
        static const double HALF_STEPS_PER_REV = 4075.7728; // 64 half-steps per motor revolution x the 63.684:1 gearbox (it is not exactly the advertised 64:1).

        Motor(double diameter=SHEAVE_DIAMETER_MM, bool reversed=false, int home_offset=0)
            : diameter_mm(diameter), reversed(reversed), home_offset(home_offset) {
                steps_per_mm = 1.0 / (HALF_STEPS_PER_REV / (std::numbers::pi * diameter_mm));
            }
        Motor()
        virtual void move(int position_mm);
        virtual void release();

    protected:
        uint8_t phase = 0;  // index into the half-step sequence, kept while released

    private:
        double diameter_mm;
        double steps_per_mm; // half-steps per millimeter
        bool reversed;
        int home_offset;    // in half-steps
};

class Banner {
    public:
        static const int BANNER_WIDTH_MM = 200;
        Banner(Motor *motor,int width=BANNER_WIDTH_MM) : motor(motor), width(width) {}
        void moveTo(int position_mm);
        bool isInMotion();

    private:
        Motor *motor;
        int position_mm;
        uint64_t lastHomed;
        int width;
};

struct HomingSensor {
    uint8_t pin;
    bool activeLevel;
    uint32_t position_mm;
};

enum BannerSetState {
    UNHOMED,
    HOMING,
    HOMED,
    IN_POSITION,
    IN_MOTION
};
class BannerSet {
    public:
        BannerSet(std::vector<Banner> banners,std::vector<HomingSensor> sensors) : banners(banners), sensors(sensors) {}
        void home();
        void shuffle();
        Banner *getBanner(int index) { return &banners[index]; }
        BannerSetState getState() const { return state; }
    private:
        std::vector<Banner> banners;
        std::vector<HomingSensor> sensors;
        BannerSetState state = UNHOMED;
};

class MultiplexedMotor : Motor {
    //represents a motor connected to a 74HC595 shift register that can control 2 motors. All 745s are chained together on the same SPI bus.
    public:
        MultiplexedMotor(int csPin, bool highPins=false) : csPin(csPin), shiftLeft(highPins?4:0) {}

    private:
        int csPin;
        uint8_t shiftLeft = 0;
};

}