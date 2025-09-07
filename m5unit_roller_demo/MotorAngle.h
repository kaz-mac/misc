/*
  MotorAngle.h
  M5Stack Unit-Roller485 ポジションモード用 モーターの角度とスピード制御クラス

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT

  参考ページ
  https://docs.m5stack.com/ja/unit/Unit-Roller485
  https://docs.m5stack.com/ja/guide/motor_ctl/roller485/roller485
  https://github.com/m5stack/M5Unit-Roller

  TODO:
  ・start()で移動中に位置を変更したときに減速（台形制御）せずにスムーズに動かしたい
  ・台形制御は角度指定ではなく時間ベースで計算すべき
*/

// デバッグに便利なマクロ定義 ESP32-S3 M5.Log対応ver --------
#define sp(x) M5.Log.printf("%s\n", String(x).c_str())
#define spn(x) M5.Log.printf("%s", String(x).c_str())  //【非推薦】改行しないとシリアルには出力されないので注意
#define spp(k,v) M5.Log.printf("%s=%s\n", k, String(v).c_str())
#define spf(fmt, ...) M5.Log.printf(fmt, __VA_ARGS__)
#define array_length(x) (sizeof(x) / sizeof(x[0]))


class MotorAngle {
private:
  UnitRollerI2C* _Roller;   // RollerI2Cインスタンスへのポインタ
  TaskHandle_t _task_handle = NULL;   // FreeRTOSタスクハンドル
  bool _abort = false;      // 中断フラグ

public:
  int _target_pos;          // 目標の角度 (1/100度単位)
  int _speed;               // スピード　角度/s (1/100度単位)
  int _init_loops = 0;      // 初期ポジションが何回転分ずれているか
  int _minimum_deg = 100;   // 変化させる最小角度 (1/100度単位)  
  uint32_t accel_angle = 15*100;  // 加速するまでの角度
  uint32_t stop_angle = 10*100;  // 減速するまでの角度
  bool _finish = true;      // 移動完了

  // コンストラクタ
  MotorAngle(UnitRollerI2C* rolleri2c) {
    _Roller = rolleri2c;
  }

  // 初期設定
  void init() {
    output(0);
    _Roller->setMode(ROLLER_MODE_POSITION);
    _init_loops = _Roller->getPosReadback() / 36000 * 36000;
  }

  // 終了
  void end() {
    stop();
    output(0);
  }

  // 最小角度を設定
  void set_minimum_deg(int minimum_deg) {
    _minimum_deg = minimum_deg;
  }
  
  // 静的タスク関数
  static void motorTask(void* parameter) {
    MotorAngle* motor = static_cast<MotorAngle*>(parameter);
    motor->runTask();
  }

  // タスク実行関数
  void runTask() {
    int start_pos = _Roller->getPosReadback();
    spf("start_pos: %d, _target_pos: %d\n", start_pos, _target_pos);
    int pos = start_pos;
    int dir = (start_pos < _target_pos) ? 1 : -1;
    int total_angle = (_target_pos - start_pos) * dir;
    spf("total_angle: %d\n", total_angle);
    int moved_angle = 0;
    while (true) {
      if (_abort) break;
      // 台形制御
      int speed = _speed;
      if (moved_angle < accel_angle) {
        speed = map(moved_angle, 0, accel_angle, _speed*0.2f, _speed);
      } else if (moved_angle > (total_angle-stop_angle)) {
        speed = map(moved_angle, (total_angle-stop_angle), total_angle, _speed, _speed*0.2f);
      }
      if (abs(pos - _target_pos) < _minimum_deg) break;
      pos += _minimum_deg * dir;
      moved_angle += _minimum_deg;
      uint32_t step_time_us = (_minimum_deg * 1000000) / speed;
      move(pos, step_time_us);
      yield();
    }
    // タスクを削除
    sp("Motor task deleted");
    _task_handle = NULL;
    _finish = true;
    _abort = false;
    vTaskDelete(NULL);
  }

  // モーターの角度を変更し、指定時間まで待機
  void move(int pos, uint32_t time_us) {
    // spf("move: %d, %d us\n", pos, time_us);
    uint32_t sta_us = micros();
    _Roller->setPos(pos);
    output(1);
    int32_t remain_us = time_us - (micros()-sta_us);
    while (true) {
      if (_abort) break;
      if (remain_us <= 0) break;
      uint32_t delay_us = remain_us > 100 ? 100 : remain_us;
      remain_us -= delay_us;
      delayMicroseconds(delay_us);
      yield();
      if (remain_us <= 0) break;
    }
  }

  // スタート
  void start(int pos, int speed) {
    if (_task_handle != NULL || !_finish) {
      sp("Motor task is already running, stopped");
      stop();
    }
    sp("starting...");
    // 初期設定
    _target_pos = pos;
    _speed = speed;
    _finish = false;
    _abort = false;
    // タスクの開始
    xTaskCreate(
      motorTask,           // タスク関数
      "MotorAngleTask",    // タスク名
      4096,                // スタックサイズ
      this,                // パラメータ
      1,                   // 優先度
      &_task_handle        // タスクハンドル
    );
    sp("Motor task created");
  }

  // ストップ
  void stop() {
    _abort = true;
    sp("Motor task stopping...");
    while (_task_handle != NULL || !_finish) {
      delay(10);
    }
    sp("Motor task stopped");
  }

  // モーター出力のオンオフ
  void output(uint8_t onoff) {
    if (onoff && _Roller->getOutputStatus()) return;
    _Roller->setOutput(onoff ? 1 : 0);
  }

  // 目的の角度に到達したか？
  bool is_finish() {
    return _finish;
  }

};
