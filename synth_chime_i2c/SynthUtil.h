/*
  SyhthUtil.h
  M5Stack Unit-Syhthで演奏を行うクラス
  （3和音可、ただし同時発音のみ）

  Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#pragma once
#include <M5UnitSynth.h>

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spp(k,v) Serial.println(String(k)+"="+String(v))
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)


class SynthUtil {
private:
  M5UnitSynth* _synth;
  const uint8_t (*_melody)[6];
  size_t _melodyLen;
  uint16_t _tempo = 120;

public:
  bool busy = false;  // 実行中フラグ　再生中はtrue
  bool abort = false; // 中断させたいときにtrue

  SynthUtil(M5UnitSynth* synthPointer) : _synth(synthPointer) {};
  ~SynthUtil() {};

  void setMusic(const uint8_t (*melodyAry)[6], size_t noteNum, uint16_t tempo);  // 再生する音楽を登録する
  void play(); // 音楽を再生する
  void playBackground();  // 音楽を再生するタスクを実行する（バックグラウンド再生）
  bool isPlaying();  // バックグラウンドで再生中か？
};

// from M5Stack-Avatar (https://github.com/meganetaaan/m5stack-avatar)
class DriveContextSYU {
 private:
  SynthUtil *ssuu;
 public:
  DriveContextSYU() = delete;
  explicit DriveContextSYU(SynthUtil *ssuu);
  ~DriveContextSYU() = default;
  DriveContextSYU(const DriveContextSYU &other) = delete;
  DriveContextSYU &operator=(const DriveContextSYU &other) = delete;
  SynthUtil *getSynthUtil();
};

