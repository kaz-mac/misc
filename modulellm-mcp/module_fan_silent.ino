// Module Fanのスピードをボタンで調整できるようにしたもの
// オリジナル https://github.com/m5stack/M5Module-Fan

/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*
 * @Hardwares: Basic v2.7+ Module Fan
 * @Dependent Library:
 * M5GFX@^0.2.3: https://github.com/m5stack/M5GFX
 * M5Unified@^0.2.2: https://github.com/m5stack/M5Unified
 * M5Module-Fan: https://github.com/m5stack/M5Module-Fan
 */

#include <M5Unified.h>
#include <m5_module_fan.hpp>

M5ModuleFan moduleFan;
uint8_t deviceAddr = MODULE_FAN_BASE_ADDR;
uint8_t dutyCycle  = 30;
void setup()
{
    M5.begin();
    Serial.begin(115200);
    while (!moduleFan.begin(&Wire1, deviceAddr, 21, 22, 400000)) {
        Serial.printf("Module FAN Init faile\r\n");
        delay(500);
    }
    // Set the fan to rotate at 80% duty cycle
    moduleFan.setPWMDutyCycle(dutyCycle);
}

void loop() {
    static bool show = true;
    M5.update();

    if (M5.BtnA.wasPressed()) {
        dutyCycle -= 10;
        show = true;
    } else if (M5.BtnC.wasPressed()) {
        dutyCycle += 10;
        show = true;
    }
    if (dutyCycle < 0) dutyCycle = 0;
    else if (dutyCycle > 100) dutyCycle = 100;
    moduleFan.setPWMDutyCycle(dutyCycle);
    
    if (M5.BtnB.wasPressed()) show = true;
    if (show) {
        M5.Display.clear();
        M5.Display.setCursor(0, 0);
        M5.Display.setFont(&fonts::Font4);
        M5.Display.setTextSize(1);
        M5.Display.printf("Setting: %d\n", dutyCycle);
        M5.Display.printf("Work Status: %d\n", moduleFan.getStatus());
        M5.Display.printf("PWM Frequency: %d\n", moduleFan.getPWMFrequency());
        M5.Display.printf("PWM Duty Cycle: %d\n", moduleFan.getPWMDutyCycle());
        M5.Display.printf("RPM: %d\n", moduleFan.getRPM());
        M5.Display.printf("Signal Frequency: %d\n", moduleFan.getSignalFrequency());
        M5.Display.printf("Firmware Version: %d\n", moduleFan.getFirmwareVersion());
        M5.Display.printf("I2C Address: 0x%02X\n", moduleFan.getI2CAddress());
        
        Serial.printf("\r\n");
        Serial.printf(" {\r\n");
        Serial.printf("    Setting          : %d\r\n", dutyCycle);
        Serial.printf("    Work Status      : %d\r\n", moduleFan.getStatus());
        Serial.printf("    PWM  Frequency   : %d\r\n", moduleFan.getPWMFrequency());
        Serial.printf("    PWM  Duty Cycle  : %d\r\n", moduleFan.getPWMDutyCycle());
        Serial.printf("    RPM              : %d\r\n", moduleFan.getRPM());
        Serial.printf("    Signal Frequency : %d\r\n", moduleFan.getSignalFrequency());
        Serial.printf("    Firmware Version : %d\r\n", moduleFan.getFirmwareVersion());
        Serial.printf("         I2C Addrres : 0x%02X\r\n", moduleFan.getI2CAddress());
        Serial.printf("                             }\r\n");
        show = false;
    }
    delay(10);
}