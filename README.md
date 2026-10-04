| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-H21 | ESP32-H4 | ESP32-P4 | ESP32-S2 | ESP32-S3 | Linux |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | --------- | -------- | -------- | -------- | -------- | ----- |

# Bruce Banner

Controls stepper motors. e-lit with paper.

## Arduino component for multiplexing:

See: Multiple 28BYJ-48 5-Volt Steppers with SN74HC595 and ULN2003A - Driver/Tutorial
https://forum.arduino.cc/t/multiple-28byj-48-5-volt-steppers-with-sn74hc595-and-uln2003a-driver-tutorial/363674


## LLM reqsponse

**LLM question about doing the above. Note the bit about demultiplexer. Check that out?**

To control multiple 28BYJ-48 stepper motors using a 74HC595 shift register and ULN2003 drivers, you chain the shift registers to expand the output pins while using one ULN2003 module per motor. Each 74HC595 provides 8 outputs, allowing it to control two stepper motors (4 pins per motor) or four motors if you use a demultiplexer approach, though the standard method chains them to drive multiple ULN2003 boards.

Wiring Configuration

Shift Register Chain: Connect the Q7' (pin 9) of the first 74HC595 to the DS (pin 14) of the next. Chain all SH_CP (pin 11) to the Arduino CLOCK pin, all ST_CP (pin 12) to the LATCH pin, and the first DS (pin 14) to the DATA pin. 
Power: Connect all VCC (pin 16) pins to 5V and all GND (pin 8) pins to GND.  Tie /MR (pin 10) to 5V and OE (pin 13) to GND to enable outputs.
Driver Connection: Connect the first 4 outputs (Q0–Q3) of a 74HC595 to the IN1–IN4 inputs of one ULN2003 driver board. Connect the next 4 outputs (Q4–Q7) to the inputs of a second ULN2003 board.
Motor Connection: Plug the 28BYJ-48 motors directly into the respective ULN2003 modules.  Ensure the ULN2003 VCC pins are connected to a 5V power supply (external or Arduino, depending on current draw) and that GND is common. 
Code Implementation
Use a custom library like UnipolarSteppersOn74595 or manually shift bits. The standard Stepper.h library does not natively support shift registers for multiple motors simultaneously without modification.

#include <UnipolarSteppersOn74595.h>

#define STEPPER_COUNT 2
#define LATCH_PIN 8
#define CLOCK_PIN 9
#define DATA_PIN 10

void setup() {
  Stepper.begin(STEPPER_COUNT, LATCH_PIN, CLOCK_PIN, DATA_PIN);
}

void loop() {
  // Start motor 1 forward
  Stepper.start(0, 1); 
  // Start motor 2 backward
  Stepper.start(1, -1);
}

Important Considerations

Current Limits: The 74HC595 has a limited current output per pin (typically 6-25mA). Ensure the ULN2003 inputs are driven correctly; the ULN2003 itself can handle up to 500mA per channel, which is sufficient for the 28BYJ-48. 
Simultaneous Control: To move multiple motors at different speeds or directions simultaneously, you must call the step-update function (e.g., Stepper.run() or manual bit-shifting) frequently in the loop, similar to the AccelStepper library pattern. 
Alternative ICs: For PCB space savings, consider using MIC5842 ICs, which combine the shift register and Darlington array functionality in a single chip.

## Troubleshooting

* Program upload failure

    * Hardware connection is not correct: run `idf.py -p PORT monitor`, and reboot your board to see if there are any output logs.
    * The baud rate for downloading is too high: lower your baud rate in the `menuconfig` menu, and try again.

## Technical support and feedback

