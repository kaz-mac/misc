/*
  tools.h
  特別な用途のプログラムはメインに書くと長くなるからこっちに書く

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#pragma once
// #include "function.h"

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spp(k,v) Serial.println(String(k)+"="+String(v))
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#define array_length(x) (sizeof(x) / sizeof(x[0]))

// メインプログラムのグローバル関数を使うための記述
#include "ServoChan.h"
using namespace servo_chan;
extern ServoChan servo;

// メインプログラムの関数の前方宣言
void beep(unsigned long duration);


// ====================================================================================

// サーボ調整ツール
void extend_servo_adjust() {
  String title[] = { "-", "X", "Y" };
  String btncap[] = { "", "Mode", "" };
  uint16_t ww = M5.Lcd.width();
  uint16_t hh = M5.Lcd.height();
  uint16_t x, y;
  float ax=90, ay=90;
  int i, mode = 0;
  bool view = true;

  sp("Entering Servo Adjust mode.");
  servo.moveAngleXY(ax, ay);

  M5.Lcd.fillScreen(TFT_WHITE);
  M5.Lcd.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setClipRect(2,2, ww-4, hh-4);  //描画範囲

  // ループ
  for (;;) {
    M5.update();

    // モード変更
    if (M5.BtnB.wasPressed()) {
      beep(50);
      mode ++;
      if (mode >= 3) mode = 0;
      view = true;
    } else if (M5.BtnB.pressedFor(1000)) { // 長押しで抜ける
      beep(500);
      break;
    }

    // UP/DOWN
    if (M5.BtnA.wasPressed()) {
      beep(50);
      if (mode == 1) ax += (ax < 180) ? 5 : 0;
      else if (mode == 2) ay += (ay < 180) ? 5 : 0;
      view = true;
    } else if (M5.BtnC.wasPressed()) {
      beep(50);
      if (mode == 1) ax -= (ax > 0) ? 5 : 0;
      else if (mode == 2) ay -= (ay > 0) ? 5 : 0;
      view = true;
    }
    if (view) {
      servo.moveAngleXY(ax, ay);
    }

    // 表示
    if (view) {
      M5.Lcd.setCursor(0, 2);
      M5.Lcd.println("Servo Adjust Mode");
      M5.Lcd.println("Adjust : "+title[mode]);
      M5.Lcd.printf("X = %.1f\n", ax);
      M5.Lcd.printf("Y = %.1f\n", ay);
      if (mode == 0) {
        btncap[0] = "";
        btncap[2] = "";
      } else if (mode == 1 || mode == 2) {
        btncap[0] = "Up";
        btncap[2] = "Down";
      }
      for (i=0; i<3; i++) {
        M5.Lcd.setCursor(30+(ww/3)*i, 220);
        M5.Lcd.print(btncap[i]);
      }
      view = false;
    }
    delay(10);
  }

  // 元に戻して抜ける
  servo.moveAngleXY(90, 90, true);
  M5.Lcd.clearClipRect();  //描画範囲解除
  M5.Lcd.fillScreen(TFT_WHITE);
} 

// ====================================================================================

// サーボデモツール
void extend_servo_demo() {
  sp("Start Debug mode");
  servo.setSpeedDefault(SERVO_SPEED_SLOW);  // 低速
  delay(1000);
  servo.headPosition(0, 0);   // 頭をホームポジションに戻す
  delay(1000);
  float fx, fy;
  while (true) {
    fy = 0;
    for (float fx=-1.0; fx<=1.0; fx += 0.25) {
      spf("X-inc: fx=%f fy=%f\n", fx, fy);
      servo.headPosition(fx, 0);
      delay(500);
    }
    for (float fx=1.0; fx>=-1.0; fx -= 0.25) {
      spf("X-dec: fx=%f fy=%f\n", fx, fy);
      servo.headPosition(fx, 0);
      delay(500);
    }
    fx = 0;
    servo.headPosition(fx, fy);
    delay(1000);
    for (float fy=-1.0; fy<=1.0; fy += 0.25) {
      spf("Y-inc: fx=%f fy=%f\n", fx, fy);
      servo.headPosition(fx, fy);
      delay(500);
    }
    for (float fy=1.0; fy>=-1.0; fy -= 0.25) {
      spf("Y-dec: fx=%f fy=%f\n", fx, fy);
      servo.headPosition(fx, fy);
      delay(500);
    }
    fy = 0;
    servo.headPosition(fx, fy);
    delay(1000);
  }
}

// ====================================================================================

// Serial2をディスプレイにダンプする
void extend_serial2_dump() {
  M5.Lcd.println("Start Serial2 Debug");
  sp("Start Serial2 Debug");
  // serial2の結果を読み取る
  int line = 0;
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextFont(2);
  while (true) {
    if (Serial2.available() > 0) {
      String text = Serial2.readStringUntil('\n');
      Serial.println(text);
      if (line++ % 20 == 0) {
        M5.Lcd.fillScreen(TFT_BLACK);
        M5.Lcd.setCursor(0, 0);
      }
      M5.Lcd.println(text);
    }
    delay(10);
  }
}

// ====================================================================================

// LCDにテキストを表示
// void lcdtext(LovyanGFX* dst, String text, int x=-1, int y=-1, int size=-1) {
//   static int oldsize;
//   if (x != -1 && y != -1) dst.setCursor(x, y);
//   if (size != -1) dst.setTextSize(size);
//   dst.print(text);
//   oldsize = size;
// }

// ====================================================================================


