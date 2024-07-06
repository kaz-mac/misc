/*
  SC16IS7X0Serial.h
  簡易ラッパー: SC16IS7X0クラスをラッピングしてM5UnitSynthクラスで使うやつ

  Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT

  SC16IS7X0Serialの使用例　こんな感じでI2Cポートを経由してUARTに送受信できる
    SC16IS7X0 sc16is750(SC16IS750_XTAL_FREQ);
    SC16IS7X0Serial i2cuart(sc16is750);
    i2cuart.write(0x41);            // 1バイト送信  
    uint8_t buff[] = {0x0D, 0x0A};
    i2cuart.write(buff, 2);         // 複数バイト送信
    while(i2cuart.available()==0);  // 受信データがあるか
    if (i2cuart.read()==0x55) {}    // 読み込む
*/
#pragma once

#include <SC16IS7X0.h>  // このライブラリはSerialConfigをSCSerialConfigに直接書き換えている（ESP32と競合するため）

class SC16IS7X0Serial : public HardwareSerial {
public:
  SC16IS7X0Serial(SC16IS7X0& sc16is7x0)
    : HardwareSerial(1), _sc16is7x0(sc16is7x0) {}

  size_t write(uint8_t byte) override {
    return _sc16is7x0.write(byte);
  }

  size_t write(const uint8_t *buffer, size_t size) override {
    return _sc16is7x0.write(buffer, size);
  }

  int read() override {
    return _sc16is7x0.read();
  }

  int available() override {
    return _sc16is7x0.available();
  }

  void flush() override {
    _sc16is7x0.flush();
  }

  int peek() override {
    return _sc16is7x0.peek();
  }

private:
  SC16IS7X0& _sc16is7x0;
};
