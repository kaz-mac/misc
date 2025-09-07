/*
  m5unit_roller_demo.ino
  M5Stack Unit-Roller485 ポジションモード用 モーターの角度とスピード制御のデモ

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT

  使用デバイス
  ・M5Stack Core2
  ・Roller485 Unit with BLDC Motor (STM32) (https://shop.m5stack.com/products/roller485-unit-with-bldc-motor-stm32)
  ・Extension Port Module for Core2 (https://shop.m5stack.com/products/extension-port-module-for-core2)
  ・GPIO Unit-Angle (https://shop.m5stack.com/products/angle-unit)
  ・Fader Unit with B10K Potentiometer (SK6812) (https://shop.m5stack.com/products/fader-unit-with-b10k-potentiometer-sk6812)
*/

#include "unit_rolleri2c.hpp"
#include <M5Unified.h>
UnitRollerI2C RollerI2C;  // Create a UNIT_ROLLERI2C object

// デバッグに便利なマクロ定義 ESP32-S3 M5.Log対応ver --------
#define sp(x) M5.Log.printf("%s\n", String(x).c_str())
#define spn(x) M5.Log.printf("%s", String(x).c_str())  //【非推薦】改行しないとシリアルには出力されないので注意
#define spp(k,v) M5.Log.printf("%s=%s\n", k, String(v).c_str())
#define spf(fmt, ...) M5.Log.printf(fmt, __VA_ARGS__)
#define array_length(x) (sizeof(x) / sizeof(x[0]))

// GPIO設定
#define GPIO_ANGLE_OUT 36   // Port B: 回転角ユニットAnalog out
#define GPIO_FADE_OUT 13    // Port C: フェーダーユニットAnalog out
#define GPIO_FADE_LED 14    // Port C: フェーダーユニットLED

// MotorAngleクラスのインスタンス
#include "MotorAngle.h"
MotorAngle* motorAngle = nullptr;

// FastLED
#include "FastLED.h"
#define NUM_LEDS   14
CRGB leds[NUM_LEDS], leds2[NUM_LEDS];

// セットアップ
void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;   // ESP32-S3ではこの行を書かないと従来のSeria.printが出力されない
  M5.begin(cfg);
  
  // ディスプレイの設定
  M5.Display.setTextSize(2);
  M5.Display.setTextScroll(true);
  M5.Display.setTextWrap(true, true);

  // ボタンの設定
  M5.setTouchButtonHeight(32);  // タッチパネルの反応エリアを広くする

  // ログの設定
  //M5.setLogDisplayIndex(0); // ログをディスプレイにも出力
  M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_DEBUG);   // ログレベル（シリアル）
  // M5.Log.setLogLevel(m5::log_target_display, ESP_LOG_DEBUG);  // ログレベル（ディスプレイ）
  M5.Log.setEnableColor(m5::log_target_serial, false);
  delay(1000);
  sp("Start!");

  // FastLEDの設定
  FastLED.addLeds<NEOPIXEL, GPIO_FADE_LED>(leds, NUM_LEDS);
  FastLED.setBrightness(64);
  FastLED.show();
  
  // Unit-Roller485の設定、MotorAngleクラスのインスタンスを作成
  int pinSda = M5.getPin(m5::pin_name_t::port_a_sda);  // Port A: Unit-Roller485
  int pinScl = M5.getPin(m5::pin_name_t::port_a_scl);
  spf("I2C Pin SDA=%d SCL=%d\n", pinSda, pinScl);
  RollerI2C.begin(&Wire, 0x64, pinSda, pinScl, 400000);
  RollerI2C.setOutput(0);
  RollerI2C.resetStalledProtect();
  motorAngle = new MotorAngle(&RollerI2C);
  motorAngle->init();

  // 入力デバイスの設定
  pinMode(GPIO_ANGLE_OUT, INPUT);
  pinMode(GPIO_FADE_OUT, INPUT);
}

// メインループ
void loop() {
  M5.update();

  // 入力デバイスの値を取得（平均値を求める）
  uint32_t rawadc_angles = 0;
  uint32_t rawadc_fades = 0;
  for (int i=0; i<5; i++) {
    rawadc_angles += 4095 - analogRead(GPIO_ANGLE_OUT);
    rawadc_fades += 4095 - analogRead(GPIO_FADE_OUT);
  }
  uint16_t rawadc_angle = rawadc_angles / 5;
  uint16_t rawadc_fade = rawadc_fades / 5;
  // spf("rawadc_angle=%d rawadc_fade=%d\n", rawadc_angle, rawadc_fade);

  // LEDを光らせる
  uint8_t angle_pos = map(rawadc_angle, 0, 4095, 0, 6);
  uint8_t fade_pos = map(rawadc_fade, 0, 4095, 0, 6);
  uint8_t fade_color = map(rawadc_fade, 0, 4095, 0, 255);
  for (int i=0; i<14; i++) {
    leds[i] = CRGB(255-fade_color, fade_color, 0);
  }
  leds[fade_pos] = CRGB::Blue;
  leds[13-fade_pos] = CRGB::Blue;
  FastLED.show();

  // 角度と速度を計算
  int32_t angle = map(rawadc_angle, 0, 4095, -360*100, 360*100);
  angle = round(angle / 4500.0) * 4500;  // 45度単位で動かす
  uint32_t speed = map(rawadc_fade, 0, 4095, 360*0.5*100, 360*5*100);
  
  // 値が変化しないまま0.25秒経ったらモーターを動かす
  static uint32_t stable_time = 0;
  static uint32_t last_angle = -9999;
  static bool change = false;
  if (last_angle != angle) {
    stable_time = millis();
    change = true;
  }
  if (millis() - stable_time > 250) {
    if (motorAngle->is_finish() && change) {
      // LEDを点滅する
      memcpy(leds2, leds, sizeof(leds));
      for (int j=0; j<2; j++) {
        for (int i=0; i<14; i++) leds[i] = CRGB::Red;
        FastLED.show();
        delay(100);
        for (int i=0; i<14; i++) leds[i] = CRGB::Black;
        FastLED.show();
        delay(100);
      }
      memcpy(leds, leds2, sizeof(leds2));
      FastLED.show();
      // モーターを動かす
      spf("Commnad: angle=%d speed=%d\n", angle, speed);
      motorAngle->set_minimum_deg(speed / 360);
      motorAngle->start(angle, speed);
      change = false;
    }
  }
  last_angle = angle;

  delay(100);
}
