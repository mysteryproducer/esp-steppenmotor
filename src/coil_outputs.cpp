#include "coil_outputs.hpp"

#include <algorithm>
#include <iterator>
#include <vector>

#include "config.hpp"
#include "platform.hpp"

static_assert(config::OUTPUT_MODE != config::OutputMode::DirectGpio ||
                  config::MOTOR_COUNT <= std::ssize(config::DIRECT_GPIO_PINS),
              "DIRECT_GPIO_PINS needs an entry for every motor");

namespace {

class ShiftRegisterOutputs final : public CoilOutputs {
public:
    ShiftRegisterOutputs() : staged_((config::MOTOR_COUNT + 1) / 2), buffer_(staged_.size()) {
        // Stay disabled until the registers hold a known state.
        if (config::SR_ENABLE_PIN != config::NO_PIN) {
            platform::configure_output(config::SR_ENABLE_PIN, true);
        }
        platform::configure_output(config::SR_LATCH_PIN, false);
        platform::spi_begin(config::SR_DATA_PIN, config::SR_CLOCK_PIN, buffer_.size());
        flush();
    }

    void set(int motor, uint8_t coils) override {
        uint8_t& chip = staged_[motor / 2];
        const int shift = (motor % 2) * 4;
        chip = static_cast<uint8_t>((chip & ~(0x0F << shift)) | ((coils & 0x0F) << shift));
    }

    void flush() override {
        // Every byte is pushed along the chain by the ones after it, so the furthest chip's goes first.
        std::reverse_copy(staged_.begin(), staged_.end(), buffer_.begin());
        platform::spi_write(buffer_.data(), buffer_.size());
        platform::write_output(config::SR_LATCH_PIN, true);
        platform::write_output(config::SR_LATCH_PIN, false);
    }

    void set_enabled(bool enabled) override {
        if (config::SR_ENABLE_PIN != config::NO_PIN) {
            platform::write_output(config::SR_ENABLE_PIN, !enabled);
        }
    }

private:
    std::vector<uint8_t> staged_;  // one byte per chip, chip 0 first
    std::vector<uint8_t> buffer_;
};

class DirectGpioOutputs final : public CoilOutputs {
public:
    DirectGpioOutputs() : staged_(config::MOTOR_COUNT, 0), written_(config::MOTOR_COUNT, 0) {
        for (int motor = 0; motor < config::MOTOR_COUNT; ++motor) {
            for (int pin : config::DIRECT_GPIO_PINS[motor]) {
                platform::configure_output(pin, false);
            }
        }
    }

    void set(int motor, uint8_t coils) override { staged_[motor] = coils; }

    void flush() override {
        for (size_t motor = 0; motor < staged_.size(); ++motor) {
            if (staged_[motor] == written_[motor]) {
                continue;
            }
            for (int coil = 0; coil < 4; ++coil) {
                platform::write_output(config::DIRECT_GPIO_PINS[motor][coil], (staged_[motor] >> coil) & 1);
            }
            written_[motor] = staged_[motor];
        }
    }

    // Motors are released by staging an all-off pattern, so there is nothing else to switch.
    void set_enabled(bool) override {}

private:
    std::vector<uint8_t> staged_;
    std::vector<uint8_t> written_;
};

}  // namespace

std::unique_ptr<CoilOutputs> make_coil_outputs() {
    if (config::OUTPUT_MODE == config::OutputMode::DirectGpio) {
        return std::make_unique<DirectGpioOutputs>();
    }
    return std::make_unique<ShiftRegisterOutputs>();
}
