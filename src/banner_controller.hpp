#pragma once

#include <cstdint>
#include <vector>

#include "config.hpp"
#include "motion.hpp"
#include "settings.hpp"

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
