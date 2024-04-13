#include <M5Unified.h>
#include "UNIT_SCALES.h"    // https://github.com/m5stack/M5Unit-Miniscale

M5GFX display;
M5Canvas canvas(&display);
UNIT_SCALES scales;

uint16_t dw, dh;

void setup() {
    M5.begin();
    display.begin();
    Serial.begin(115200);
    Serial.println("System Start!");
    canvas.setRotation(3);
    canvas.setColorDepth(8);  // mono color
    canvas.setFont(&fonts::efontCN_12);
    dh = display.width();
    dw = display.height();
    canvas.createSprite(dh, dw);
    canvas.setTextSize(2);
    while (!scales.begin(&Wire, 32, 33, DEVICE_DEFAULT_ADDR)) {
        Serial.println("scales connect error");
        M5.Lcd.print("scales connect error");
        delay(1000);
    }
    scales.setLEDColor(0x001000);
}

void loop() {
    float weight = scales.getWeight();
    float gap    = scales.getGapValue();
    int adc      = scales.getRawADC();

    canvas.fillSprite(BLACK);
    canvas.setTextColor(YELLOW);
    canvas.setTextSize(2);
    canvas.drawString("M5Stack Scales", 10, 5);

    canvas.setTextColor(WHITE);
    canvas.setCursor(10, 35);
    canvas.setTextSize(2);
    canvas.printf("Weight:");

    canvas.setTextColor(GREEN);
    canvas.setCursor(50, 60);
    canvas.setTextSize(4);
    //canvas.printf("%.1f g", weight);
    char buff[16];
    sprintf(buff, "%.1f g", weight);
    canvas.drawRightString(buff, dw-20, 60);

    canvas.pushSprite(0, 0);

    M5.update();
    if (M5.BtnA.wasPressed()) {
        scales.setOffset();
    }
    if (M5.BtnB.wasPressed()) {
        delay(500);
        esp_restart();
    }
    delay(10);
}
