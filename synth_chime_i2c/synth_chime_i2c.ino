/*
  synth_chime_i2c.ino
  M5Stack Unit-Syhth (UART接続)を I2C to UART変換IC SC16IS750 経由でI2C接続するサンプル

  Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT

  使用外部ユニット
    M5Stack Unit-Syhth (https://github.com/m5stack/M5Unit-Synth)
    I2C-Uart変換基板 SC16IS750 (https://www.switch-science.com/products/6027)

  使用ライブラリ
    SC16IS7X0   変数SerialConfigが競合するので別の名前にリネームして使う
*/
#include <M5Unified.h>

// 設定
#define USE_I2CUART_CONVERT     // I2C to UART変換を使う。UART接続の場合はコメントする
const byte MASTER_VOLUME = 72;  // 音量
const byte TONE_CODE = 7;       // 7=Harpsichord ハープシコード

// I2C to UART変換 SC16IS750
#ifdef USE_I2CUART_CONVERT
#define SC16IS750_ADDR 0x9A           // I2C to UART変換基板のI2Cアドレス
#define SC16IS750_XTAL_FREQ 7372800UL // I2C to UART基板のクリスタルの周波数 (https://www.switch-science.com/products/6027)
#define UNIT_SYNTH_BAUD_I2C 30720     // Unit-Syhthのボーレートは本来は31250だが出せないので30720にしている
#include <SC16IS7X0.h>  // このライブラリはSerialConfigをSCSerialConfigに直接書き換えている（ESP32と競合するため）
SC16IS7X0 sc16is750(SC16IS750_XTAL_FREQ);
#include "SC16IS7X0Serial.h"  // SC16IS7X0のラッパークラス
SC16IS7X0Serial i2cuart(sc16is750);
const bool i2cconv = true;
#endif
#ifndef USE_I2CUART_CONVERT
const bool i2cconv = false;
#endif

// M5Stack Unit-Syhth
#include <M5UnitSynth.h>
M5UnitSynth synth;
#include "SynthUtil.h"

// 楽譜データ
uint16_t tempo = 160;   // 当初は80で作成したつもりなのだが…
const uint8_t melody[][6] PROGMEM = {
  { NOTE_FS5,8, 0,0,        0,0 },
  { NOTE_D5,8,  0,0,        0,0 },
  { NOTE_A4,8,  NOTE_FS4,8, 0,0 },
  { NOTE_D5,8,  0,0,        0,0 },
  { NOTE_E5,8,  NOTE_A4,8,  0,0 },
  { NOTE_A5,8,  0,0,        0,0 },
  { REST,8,     0,0,        0,0 },
  { NOTE_E4,8,  0,0,        0,0 },
  { NOTE_E5,8,  NOTE_A4,8,  0,0 },
  { NOTE_FS5,8, 0,0,        0,0 },
  { NOTE_E5,8,  NOTE_A4,8,  0,0 },
  { NOTE_A4,8,  0,0,        0,0 },
  { NOTE_D5,4,  NOTE_FS4,4, 0,0 },
  { REST,4,     0,0,        0,0 }
};

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spp(k,v) Serial.println(String(k)+"="+String(v))
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)


// 初期化
void setup() {
  M5.begin();
  delay(500);
  sp("Unit Synth Demo");

  // I2C to UART変換IC SC16IS750の初期化
#ifdef USE_I2CUART_CONVERT
  if (sc16is750.begin_I2C(SC16IS750_ADDR >> 1)) {
    sp("i2cuart initialize successful");
  } else {
    sp("i2cuart initialize failed");
    while(1);
  }
  sc16is750.begin_UART(UNIT_SYNTH_BAUD_I2C);  // UNIT-Synthのボーレート 30720（本当は31250を出したい）
#endif

  // Unit-Synth 初期化
#ifdef USE_I2CUART_CONVERT
  synth.begin(&i2cuart, UNIT_SYNTH_BAUD, 13, 14);   // for I2C（GPIO番号は適当なもの。ダミー）
#endif
#ifndef USE_I2CUART_CONVERT
  //synth.begin(&Serial2, UNIT_SYNTH_BAUD, 22, 21);   // for BASIC RX=22, TX=21
  synth.begin(&Serial2, UNIT_SYNTH_BAUD, 33, 32);   // for Core2 RX=33, TX=32
  //synth.begin(&Serial2, UNIT_SYNTH_BAUD, 21, 25);   // for Atom Matrix extend GPIO RX=22, TX=21
  //synth.begin(&Serial2, UNIT_SYNTH_BAUD, 32, 26);   // for Atom Matrix Grove Port
#endif
  synth.reset();

  // Unit-Synth 音色の設定（0～2チャンネル使用）
  synth.setMasterVolume(MASTER_VOLUME);
  for (int ch=0; ch<3; ch++) {
    synth.setInstrument(0, ch, TONE_CODE);
  }

  sp("ready");
}

// メイン
void loop() {
  M5.update();

  // Aボタンで再生
  if (M5.BtnA.wasPressed()) {
    sp("Play start");
    SynthUtil music(&synth);
    size_t len = sizeof(melody) / sizeof(melody[0]);
    music.setMusic(melody, len, tempo);  // 楽譜データをセットする
    if (i2cconv) {
      // playBackground()は再生中も別の処理ができる
      music.playBackground(); // 再生開始 
      while (music.isPlaying()) {  // 再生中なら待機する
        delay(10);
      }
    } else {
      // play()では再生が終わるまで次の処理はできない
      music.play(); // 再生開始
    }
    sp("Play end");
  }

  delay(10);
}
