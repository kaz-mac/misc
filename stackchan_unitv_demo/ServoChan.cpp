/*
  ServoChan.cpp
  ズンダチャン　スタックチャン風にサーボを扱うクラス

  ServoEasing使用（参考 https://github.com/ArminJo/ServoEasing）

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#include "ServoChan.h"
namespace servo_chan {

// コンストラクタ
ServoChan::ServoChan() {
  _servoType = ServoType::None;
}

// サーボのGPIO設定を行う
void ServoChan::connectGpioXY(ServoType type, int16_t portax, int16_t portay) {
  _servoType = type;

  // Core2想定 Port.A に直接サーボを接続する場合
  if (type == ServoType::PortA_Direct) {
    // GPIOポートの自動設定（未指定の場合）
    servo_pin_x = (portax < 0) ? M5.Ex_I2C.getSCL() : portax;   // Core2=33
    servo_pin_y = (portay < 0) ? M5.Ex_I2C.getSDA() : portay;   // Core2=32
    Serial.printf("Servo GPIO Auto Setting: X=%d Y=%d\n", servo_pin_x, servo_pin_y);
    M5.Ex_I2C.release();  // Port.Aの外部I2Cを開放する
  } else if (type == ServoType::Custom) {
    servo_pin_x = portax;
    servo_pin_y = portay;
    Serial.printf("Servo GPIO Manual Setting: X=%d Y=%d\n", servo_pin_x, servo_pin_y);
  }

  // GPIOを割り当てる
  if (servo_x.attach(servo_pin_x, 90, DEFAULT_MICROSECONDS_FOR_0_DEGREE, DEFAULT_MICROSECONDS_FOR_180_DEGREE)) {
    Serial.println("Error attaching servo x");
  }
  if (servo_y.attach(servo_pin_y, 90, DEFAULT_MICROSECONDS_FOR_0_DEGREE, DEFAULT_MICROSECONDS_FOR_180_DEGREE)) {
    Serial.println("Error attaching servo y");
  }

  // デフォルト設定
  servo_x.setEasingType(EASE_QUADRATIC_IN_OUT);
  servo_y.setEasingType(EASE_QUADRATIC_IN_OUT);
  setSpeedForAllServos(60);
}

// サーボの可動範囲を設定する
void ServoChan::setServoAngle(float xl, float xr, float yd, float yu) {
  homeAngleX = (xl + xr) / 2;
  leftAngle  = xl;
  rightAngle = xr;
  homeAngleY = (yd + yu) / 2;
  upAngle    = yu;
  downAngle  = yd;
}

// デフォルトのスピードを設定する
void ServoChan::setSpeedDefault(float speed) {
    //setSpeedForAllServos(speed);
    speedX = speed;
    speedY = speed;
}

// X,Yの角度に移動する
void ServoChan::moveAngleXY(float ax, float ay, bool await) {
  bool opt = await ? DO_NOT_START_UPDATE_BY_INTERRUPT : START_UPDATE_BY_INTERRUPT;
  servo_x.startEaseTo(ax, speedX, opt);
  servo_y.startEaseTo(ay, speedY, opt);
  spf("Servo x=%f y=%f\n", ax, ay);
  if (await) {
    synchronizeAllServosStartAndWaitForAllServosToStop(); // 長ぇーｗ
  }
}

// 頭の向きを移動する(範囲-1.0～1.0)
void ServoChan::headPosition(float px, float py, bool await) {
  float ax, ay;
  if (abs(px) > 1.0 || abs(py) > 1.0) return;
  int dirX = dir(leftAngle, rightAngle);
  int dirY = dir(downAngle, upAngle);
  //spf("headPosition x=%f y=%f\n", px, py);
  if (px < 0) {
    ax = homeAngleX + abs(homeAngleX - leftAngle) * px * dirX;
  } else {
    ax = homeAngleX + abs(homeAngleX - rightAngle) * px * dirX;
  }
  if (py < 0) {
    ay = homeAngleY + abs(homeAngleX - downAngle) * py * dirY;
  } else {
    ay = homeAngleY + abs(homeAngleX - upAngle) * py * dirY;
  }
  moveAngleXY(ax, ay, await);
}

// 向きを区別する
int ServoChan::dir(float pos1, float pos2) {
  return (pos1 < pos2) ? 1 : -1;
} 

} //namespace