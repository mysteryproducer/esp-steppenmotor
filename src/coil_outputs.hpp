#pragma once

#include <cstdint>
#include <memory>

// Drives the IN1-IN4 inputs of every ULN2003 board.
class CoilOutputs {
public:
    virtual ~CoilOutputs() = default;

    // Stages a motor's coil pattern (bit 0 = IN1 ... bit 3 = IN4). Takes effect on flush().
    virtual void set(int motor, uint8_t coils) = 0;
    virtual void flush() = 0;

    // While disabled, every coil is off whatever pattern is staged.
    virtual void set_enabled(bool enabled) = 0;
};

// Creates the outputs selected by config::OUTPUT_MODE, disabled and with every coil off.
std::unique_ptr<CoilOutputs> make_coil_outputs();
