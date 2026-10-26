#pragma once

#include <cstdint>
#include <vector>
#include <math.h>

#include "JVal.h"

namespace banners {

class BannerSet;

// Moves each set of banners into a random order, homing them first when their positions are unknown.
class BannerController {
public:
    BannerController(const JVal& settings);

    // Homes every set whose positions are unknown, or every set if `force` is true. A non-negative
    // `only_set` limits this to that set.
    void home(bool force, int only_set = -1);

    // Moves every homed set into a new random order.
    void shuffle();

    // Moves one banner by `steps` half-steps; positive is away from home.
    void jog(int set, int banner, int32_t steps);

    // Forgets a set's positions, so it is homed before it next moves.
    void invalidate(int set);

private:
    std::vector<BannerSet> sets;
    int banner_spacing_mm = 20;
};
 
class SignalDestination {
    public:
        virtual void sendNextSignal(long time_us) = 0;
        virtual void configurePins() = 0;
        virtual ~SignalDestination() = default;
};

class Motor : SignalDestination {
    public:
        static const uint8_t PHASES = 8;
        static constexpr uint8_t HALF_STEPS[8] = {0b0001, 0b0011, 0b0010, 0b0110, 0b0100, 0b1100, 0b1000, 0b1001};
        static const int SHEAVE_DIAMETER_MM = 26;
        static constexpr float HALF_STEPS_PER_REV = 4075.7728; // 64 half-steps per motor revolution x the 63.684:1 gearbox (it is not exactly the advertised 64:1).
        static constexpr float MAX_SPEED = .0001; // half-steps per microsecond
        static const long ACCELERATION_TIME = 1000000; // microseconds
        static constexpr float ACCELERATION = static_cast<float>(MAX_SPEED) / ACCELERATION_TIME; // half-steps per microsecond^2

        Motor(float diameter=SHEAVE_DIAMETER_MM, bool reversed=false, int home_offset=0)
            : diameter_mm(diameter), reversed(reversed), home_offset(home_offset) {
                steps_per_mm = 1.0 / (HALF_STEPS_PER_REV / (std::numbers::pi * diameter_mm));
            }
        Motor(JVal *settings);
        void move(int position_mm);
        void release();
        void sendNextSignal(long time_us) override;
        void configurePins() override;
        long time_to_next(long time_us);

    protected:
        uint8_t phase = 0;  // index into the half-step sequence, kept while released
        long position = -1; // in half-steps, relative to home
        int getAbsolutePosition(long time_us);

    private:
        uint8_t pins[4];

        float diameter_mm;
        float steps_per_mm; // half-steps per millimeter
        bool reversed;
        int32_t home_offset;    // in half-steps
        
        int32_t start_position;  // in half-steps, relative to home
        int32_t start_time;
        int32_t target_position; // in half-steps
        int32_t travel_time_us; // time to travel from start_position to target_position

        void write_pins();
};

class Multiplexer : SignalDestination {
    //represents a motor connected to a 74HC595 shift register that can control 2 motors. All 745s are chained together on the same SPI bus.
    public:
        Multiplexer(int csPin, bool highPins=false) : csPin(csPin), shiftLeft(highPins?4:0) {}
        Multiplexer(JVal *settings);
        void sendNextSignal(long time_us) override;
        void configurePins() override;
        std::vector<Motor*> getMotors();

    private:
        int csPin;
        int sdaPin;
        int sckPin;
        
        std::vector<Motor*> motors;
        
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
    uint32_t position_mm; //from left of track
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

}