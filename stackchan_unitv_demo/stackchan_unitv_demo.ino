/*
  stackchan_unitv_demo.ino
  スタックチャン UnitVデモ

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#include <M5Unified.h>

// GPIO設定
#define GPIO_SERVO_X 27   // サーボ X軸  
#define GPIO_SERVO_Y 19   // サーボ Y軸 
#define GPIO_UART_RX 32   // UnitV UART RX (Core2 Port A)
#define GPIO_UART_TX 33   // UnitV UART TX (Core2 Port A)

// アバター関連
#include <Avatar.h>   // M5Stack_Avatar
using namespace m5avatar;
Avatar avatar;

// サーボ関連
#include <ServoEasing.hpp>      
#include "ServoChan.h"
using namespace servo_chan;
ServoChan servo;

// サーボの最大可動範囲の設定（extend_servo_adjust()で調べる）
const float ANGLEX_L = 45;      // X軸 左
const float ANGLEX_R = 135;     // X軸 右
const float ANGLEY_U = 60;      // Y軸 上
const float ANGLEY_D = 105;     // Y軸 下

// サーボのスピード定義
#define SERVO_SPEED_SLOW 20     // 低速
#define SERVO_SPEED_MEDIUM 80   // 中速
#define SERVO_SPEED_FAST 120    // 高速
#define SERVO_SPEED_VFAST 240   // 超高速

// ツール類
#include "tools.h"    // 作業用のプログラム

// 設定
const int UNITV_CAM_WIDTH = 320;    // UnitVのカメラの横幅 QVGA 320px
const int UNITV_CAM_HEIGHT = 240;   // UnitVのカメラの縦幅 QVGA 240px

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spp(k,v) Serial.println(String(k)+"="+String(v))
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#define array_length(x) (sizeof(x) / sizeof(x[0]))

// UnitVデータ構造体
struct UnitVData {
  int hit, x, y, w, h, pixel, cx, cy, fps;
};

// ====================================================================================

// UnitVから最新のデータを取得する
UnitVData get_unitv_latest_data() {
  UnitVData data = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  String line_last = "";
  
  // Serial2からバッファ内のすべてのデータを読み取り、最後の行のみを保持
  while (Serial2.available() > 0) {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) line_last = line;
    delay(5);
  }
  
  // 最新データをパース
  if (line_last.length() > 0) {
    int val[9];
    int index = 0;
    int pos = 0;
    for (int i = 0; i <= line_last.length() && index < 9; i++) {
      if (i == line_last.length() || line_last.charAt(i) == ',') {
        if (i > pos) {
          String val_str = line_last.substring(pos, i);
          val[index++] = val_str.toInt();
        } else {
          val[index++] = 0;
        }
        pos = i + 1;
      }
    }
    data = {
      val[0], val[1], val[2], val[3], val[4], val[5], val[6], val[7], val[8]
    };
  }
  
  return data;
}

// BEEP音を鳴らす
void beep(unsigned long duration=100) {
  M5.Speaker.tone(2000, duration);
  delay(duration);
}

// ====================================================================================

// 初期化
void setup() {
  auto cfg = M5.config();
  cfg.external_spk = true;    /// use external speaker (SPK HAT / ATOMIC SPK)
  M5.begin(cfg); 
  Serial.begin(115200);
  delay(1000);
  sp("Start");

  // UnitV用 Serial2の設定
  Serial2.begin(115200, SERIAL_8N1, GPIO_UART_RX, GPIO_UART_TX);

  // ディスプレイの設定
  M5.Lcd.init();
  M5.Lcd.setRotation(0);    // 縦向きで表示 (240x320)
  M5.Lcd.setColorDepth(16);
  M5.Lcd.fillScreen(TFT_BLACK);

  // サーボの設定
  //servo.connectGpioXY(ServoType::PortA_Direct); // Port.A直結 GPIO番号自動設定
  servo.connectGpioXY(ServoType::Custom, GPIO_SERVO_X, GPIO_SERVO_Y); // GPIO設定
  servo.setServoAngle(ANGLEX_L, ANGLEX_R, ANGLEY_D, ANGLEY_U); // サーボの可動範囲を設定する
  servo.setSpeedDefault(SERVO_SPEED_FAST);  // 移動速度
  servo.headPosition(0, 0);   // 頭をホームポジションに戻す

  // Aボタンを押しながら起動したら、サーボ調整ツールを実行する
  if (M5.BtnA.isPressed()) { 
    beep();
    extend_servo_adjust();
  }

  // Bボタンを押しながら起動したら、サーボデモを実行する
  if (M5.BtnB.isPressed()) { 
    beep();
    extend_servo_demo();
  }

  // Cボタンを押しながら起動したら、シリアルポートの出力を表示する（デバッグ用）
  if (M5.BtnC.isPressed()) { 
    beep();
    extend_serial2_dump();
  }

  // アバターの初期化（縦表示なので調整する）
  avatar.setScale(1.0);
  avatar.setPosition(30, -40);
  avatar.init();
}

// ====================================================================================

// メインループ
void loop() {
  M5.update();

  // UnitVで検出した物体をサーボで追跡する
  static uint32_t tm = millis();
  if (tm < millis()) {
    UnitVData unitv = get_unitv_latest_data();
    if (unitv.hit == 1 && unitv.pixel > 200) {
      spf("UnitV Data: hit=%d, x=%d, y=%d, w=%d, h=%d, pixel=%d, cx=%d, cy=%d, fps=%d\n", unitv.hit, unitv.x, unitv.y, unitv.w, unitv.h, unitv.pixel, unitv.cx, unitv.cy, unitv.fps);
      float fx = -map(unitv.cx, 0, UNITV_CAM_WIDTH, -1000, 1000) / 1000.0;
      float fy = -map(unitv.cy, 0, UNITV_CAM_HEIGHT, -1000, 1000) / 1000.0;
      spf("Target: %.2f, %.2f\n", fx, fy);
      servo.headPosition(fx, fy); // 頭の向きを変更
    }
    tm = millis() + 50; // 次回は50ms経過後に実行する
  }

  delay(10);
}

// ====================================================================================


