/*
  SyhthUtil.cpp
  M5Stack Unit-Syhthで演奏を行うクラス

  Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#include "SynthUtil.h"

DriveContextSYU::DriveContextSYU(SynthUtil *ssuu) : ssuu{ssuu} {}
SynthUtil *DriveContextSYU::getSynthUtil() { return ssuu; }

// 再生する音楽を登録する
void SynthUtil::setMusic(const uint8_t (*melodyAry)[6], size_t noteNum, uint16_t tempo) {
  _melody = melodyAry;
  _melodyLen = noteNum;
  _tempo = tempo;
}

// 音楽を再生する
void SynthUtil::play() {
  uint16_t wholenote = (60000 * 4) / _tempo;
  uint8_t tone, divider, maxDivider;
  uint16_t noteDuration = 0;
  uint32_t tm;

  //const int yoin = 3;
  for (int note=0; note<_melodyLen; note++) {
    maxDivider = 0;
    for (int ch=0; ch<3; ch++) {
      tone = pgm_read_byte(&_melody[note][ch*2]);
      divider = pgm_read_byte(&_melody[note][ch*2+1]);
      if (maxDivider < divider) maxDivider = divider;
      if (tone > 0) {
        _synth->setNoteOn(ch, tone, 127);
      }
      if (ch == 2) {
        noteDuration = wholenote / maxDivider;
        delay(noteDuration);
        tm = millis() + noteDuration;
        while (millis() < tm) {
          if (abort) break;
          delay(10);
        }
      }
      // 音を止める処理が面倒なので、自然に音が止まる音色だけ使う運用とする
      // if (note >= yoin) {
      //   for (int ch=0; ch<3; ch++) {
      //     int8_t mutetone = melody[note-yoin][ch*2];
      //     if (mutetone > 0 && tone != mutetone) synth.setNoteOff(ch, mutetone, 127);
      //   }
      // }
    }
  }
  // 全ての音を止める
  for (int ch=0; ch<3; ch++) {
    _synth->setAllNotesOff(ch);
  }
  busy = false;
}

// タスク処理：音楽を再生する
void taskPlayMusic(void *args) {
  DriveContextSYU *ctx = reinterpret_cast<DriveContextSYU *>(args);
  SynthUtil *ssuu = ctx->getSynthUtil();
  ssuu->play();
  vTaskDelete(NULL);
}

// 音楽を再生するタスクを実行する（UARTだと_synth->setNoteOnのところでpanicになる場合がある）
void SynthUtil::playBackground() {
  // タスク実行中なら中断させる
  if (busy) {
    abort = true;
    uint32_t tm = millis() + 5000;
    while (busy) {
      if (tm < millis()) break;
      delay(5);
    }
    if (busy) return;
  }

  // 新しいタスクの開始
  busy = true;   // 実行中フラグをオンにする
  abort = false;
  DriveContextSYU *ctx = new DriveContextSYU(this);

  // タスクを作成する
  xTaskCreateUniversal(
    taskPlayMusic,  // Function to implement the task
    "taskPlayMusic",// Name of the task
    2048,           // Stack size in words
    ctx,            // Task input parameter
    8,             // Priority of the task
    NULL,           // Task handle.
    CONFIG_ARDUINO_RUNNING_CORE);
}

// バックグラウンドで再生中か？
bool SynthUtil::isPlaying() {
  return busy;
}
