#include "banners.hpp"
#include "platform.hpp"
#include "JVal.h"

namespace banners {

    Multiplexer::Multiplexer(JVal *settings) {
        JErr err;
        JVal *json_csPin = JVal_get(settings, &err, "cs");
        csPin = (json_csPin==NULL || err.isError())?0:json_csPin->getInt(&err);
        JVal *json_sdaPin = JVal_get(settings, &err, "sda");
        sdaPin = (json_sdaPin==NULL || err.isError())?0:json_sdaPin->getInt(&err);
        JVal *json_sckPin = JVal_get(settings, &err, "sck");
        sckPin = (json_sckPin==NULL || err.isError())?0:json_sckPin->getInt(&err);

        JVal *motor_array = JVal_get(settings, &err, "motors");
        if (motor_array != NULL && !err.isError() && motor_array->getType() == JVType::JVType_Array) {
            int motor_count = motor_array->getLength(&err);
            for (int i = 0; i < motor_count; ++i) {
                JVal *motor_val = JVal_get(motor_array, &err, "A", i);
                if (motor_val != NULL && !err.isError() && motor_val->getType() == JVType::JVType_Object) {
                    motors.push_back(new Motor(motor_val));
                }
            }
        }
        configurePins();
    }

    void Multiplexer::configurePins() {
        platform::GpioPinConfig config = { 
            .pin = 0, 
            .mode = platform::GpioPinMode::Output, 
            .pullup = false, 
            .level = false 
        };
        config.pin = csPin;
        platform::configurePin(config);
        config.pin = sdaPin;
        platform::configurePin(config);
        config.pin = sckPin;
        platform::configurePin(config);
    }

    std::vector<Motor*> Multiplexer::getMotors() {
        return motors;
    }

}