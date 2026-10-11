#include "banners.hpp"

namespace banners {

    Motor::release() {
        
    }

    Motor::move(int position_mm) {
        int target_steps = static_cast<int>(std::lround(position_mm * steps_per_mm));
        if (reversed) {
            target_steps = -target_steps;
        }
        target_steps += home_offset;
    }

}