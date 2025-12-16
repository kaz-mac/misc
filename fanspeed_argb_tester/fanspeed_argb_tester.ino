/*
  FAN & ARGB LEDコントローラー（テスト用） for M5Stack Din Meter

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#include <M5DinMeter.h>   // https://github.com/m5stack/M5DinMeter/
#include <Arduino.h>

// 設定 GPIOポート
#define GPIO_PWM_PIN   15  // Port A 白
#define GPIO_RPM_PIN   13  // Port A 黄
#define GPIO_LED_PIN    1  // Port B 白
#define GPIO_POWER_HOLD_PIN 46   // GPIOピン H=稼働時、L=スリープ時
#define GPIO_BTN_A     42   // GPIOピン ボタンA (wake)

// PWMの設定
#define PWM_FREQ 25000        // PWM周波数
#define PWM_BIT 8             // PWMの分解能bit
#define ADC_RESOLUTIUON 4095  // ADCの分解能(0-4095)

// RGB LED関連
#include <FastLED.h>
const size_t LED_NUM = 40;    // LEDの数
CRGB leds[LED_NUM];

// 定義
struct XYWHaddress { int16_t x; int16_t y; uint16_t w; uint16_t h; };

// グローバル変数
static m5::touch_state_t prev_state = m5::touch_state_t::none;  // タッチパネルの前回状態
static long prev_pos = 0;  // エンコーダーの前回位置
M5Canvas canvas(&DinMeter.Display);

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spp(k,v) Serial.println(String(k)+"="+String(v))
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#define array_length(x) (sizeof(x) / sizeof(x[0]))


// 内蔵ブザーを鳴らす
#define BEEP_VSHORT  0
#define BEEP_SHORT   1
#define BEEP_LONG    2
#define BEEP_DOUBLE  3
#define BEEP_ERROR   4
void beep(int8_t pattern=BEEP_SHORT, bool foreGround=true) {  // default: BEEP_SHORT, true
  if (pattern == BEEP_VSHORT) {
    DinMeter.Speaker.tone(4000, 10);
    if (foreGround) delay(10);
  } else if (pattern == BEEP_SHORT) {
    DinMeter.Speaker.tone(4000, 50);
    if (foreGround) delay(50);
  } else if (pattern == BEEP_LONG) {
    DinMeter.Speaker.tone(4000, 300);
    if (foreGround) delay(400);
  } else if (pattern == BEEP_DOUBLE) {
    DinMeter.Speaker.tone(4000, 50);
    delay(100);
    DinMeter.Speaker.tone(4000, 50);
    if (foreGround) delay(50);
  } else if (pattern == BEEP_ERROR) {
    for (int i=0; i<5; i++) {
      DinMeter.Speaker.tone(4000, 50);
      delay(100);
      if (i < 1 || foreGround) delay(50);
    }
  }
}

// ファンのスピードを変更する(0%-100%)
void setFanDuty(int ratio) {
  int pwm_duty = pow(2, PWM_BIT) * (ratio / 100.0);
  spf("ratio=%d%% duty=%d\n", ratio, pwm_duty);
  ledcWrite(GPIO_PWM_PIN, pwm_duty);
}

// ファンの回転数をカウントする
volatile uint32_t pulseCount = 0;
void IRAM_ATTR tachISR() {
  pulseCount++;
}

// 32bitカラーからRGB565に変換
uint16_t rgb565(uint32_t color) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// ボタンと値を描画
void drawButton(M5Canvas* canvas, int x, int y, int w, int h, String label, String text, String text2, bool selected, bool enterd) {
  // 枠
  uint16_t color = rgb565( selected ? 0xEEAFAF : 0xAFEEEE );
  canvas->fillRect(x, y, w, h, color);
  // ラベル
  canvas->setTextColor(TFT_BLACK);
  canvas->setTextDatum(TC_DATUM);
  canvas->drawString(label, x+w/2, y+3, &fonts::FreeSansBold12pt7b);
  // 値1
  color = rgb565( selected ? 0xFFFF00 : 0xFFFFFF );
  canvas->setTextDatum(TL_DATUM);
  int ty = y + ((text2 == "") ? 33 : 24);
  if (enterd) {
    canvas->setTextColor(TFT_RED);
    canvas->drawString(text, x+10, ty, &fonts::FreeSansBold12pt7b);
  } else {
    canvas->setTextColor(TFT_BLACK);
    canvas->drawString(text, x+10, ty, &fonts::FreeSans12pt7b);
  }
  // 値2
  if (text2 != "") {
    canvas->setTextColor(TFT_BLACK);
    canvas->drawString(text2, x+10, ty+22, &fonts::FreeSans9pt7b);
  }
}

// セットアップ
void setup() {
  // M5DinMeterの初期化
  auto cfg = M5.config();
  DinMeter.begin(cfg, true);  // true=Encoder enable

  // M5DinMeterでバッテリー駆動時に電源ONを継続するための設定
  gpio_reset_pin((gpio_num_t)GPIO_POWER_HOLD_PIN);
  pinMode(GPIO_POWER_HOLD_PIN, OUTPUT);
  digitalWrite(GPIO_POWER_HOLD_PIN, HIGH);

  // シリアル
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  sp("\n\nSystem Start!");
  
  // ディスプレイの設定
  DinMeter.Display.init();
  DinMeter.Display.setRotation(1);
  DinMeter.Display.setColorDepth(16);
  DinMeter.Display.setTextSize(1);
  DinMeter.Display.setBrightness(127);
  DinMeter.Display.fillScreen(TFT_BLACK);

  // キャンバスの設定
  canvas.setColorDepth(16);
  canvas.createSprite(240, 135);  // M5DinMeter 
  canvas.fillSprite(TFT_BLACK);

  // FAN スピード PWMの設定
  pinMode(GPIO_PWM_PIN, OUTPUT);  // FAN PWM Pin
  // ledcSetup(PWM_CH, PWM_FREQ, PWM_BIT);  // pwm ch0 setting     : for ESP32 Arduino Core before v3
  // ledcAttachPin(GPIO_PWM_PIN, PWM_CH);   // pwm ch0 attach GPIO : for ESP32 Arduino Core before v3
  ledcAttach(GPIO_PWM_PIN, PWM_FREQ, PWM_BIT); // for ESP32 Arduino Core after v3
  setFanDuty(0);

  // FAN 回転数取得の設定
  pinMode(GPIO_RPM_PIN, INPUT_PULLUP);  // FAN Sense Pin
  attachInterrupt(digitalPinToInterrupt(GPIO_RPM_PIN), tachISR, FALLING);
  
  // FastLEDの設定
  pinMode(GPIO_LED_PIN, OUTPUT);  // ARGB LED Data Pin 
  FastLED.addLeds<WS2811, GPIO_LED_PIN, GRB>(leds, LED_NUM);
  FastLED.setBrightness(128);

  // エンコーダーの設定
  prev_pos = DinMeter.Encoder.readAndReset();
}

// ループ
void loop() {
  static int menu = 0;
  static bool entermenu = false;
  static bool refresh = true;
  static uint8_t fan_ratio = 0;
  static uint8_t led_speed = 32;
  static uint8_t led_brightness = 63;
  static uint16_t led_index = 0;
  static bool onoff = false;
  bool set_fan_duty = false;
  bool set_led = false;

  M5.update();

  // ボタン操作
  if (M5.BtnA.pressedFor(2000)) {
    // 長押しでリセット
    beep(BEEP_ERROR);
    while (!M5.BtnA.wasReleased()) {
      M5.update();
      delay(10);
    }
    M5.BtnA.wasReleased();  // dummy
    menu = 0;
    entermenu = false;
    onoff = false;
    fan_ratio = 0;
    led_speed = 32;
    led_brightness = 63;
    led_index = 0;
    refresh = true;
    set_fan_duty = true;
  } else if (M5.BtnA.wasReleased()) {
    // ボタンを押したらメニューに入る/出る
    if (menu == 0) {  // [0]出力のON/OFF
      onoff = !onoff;
      set_fan_duty = true;
      beep(onoff ? BEEP_DOUBLE : BEEP_LONG, false);
    } else {
      entermenu = !entermenu;
      beep(BEEP_SHORT);
    }
    refresh = true;
  }

  // エンコーダー状態取得
  long enc_pos = DinMeter.Encoder.read() / 2;
  if (enc_pos != prev_pos) {
    int delta = prev_pos - enc_pos;
    prev_pos = enc_pos;
    spf("enc_pos=%d delta=%d\n", enc_pos, delta);
    beep(BEEP_VSHORT, false);
    // メニュー操作
    if (entermenu) {
      // メニュー選択時
      if (menu == 1) { // [1] ファンのスピード
        fan_ratio = constrain(fan_ratio + delta*5, 0, 100);
        set_fan_duty = true;
      } else if (menu == 2) { // [2] LEDのスピード
        int step = led_speed < 16 ? 2 : (led_speed > 32 ? 8 : 4);
        led_speed = constrain(led_speed + delta*step, 0, 255);
        set_led = true;
      } else if (menu == 3) { // [3] LEDの明るさ
        if (delta > 0) led_brightness = led_brightness << 1 | 0x01;
        else if (delta < 0) led_brightness = led_brightness >> 1;
        set_led = true;
      }
    } else {
      // メニュー非選択時
      menu += delta;
      if (menu < 0) menu = 3;
      if (menu > 3) menu = 0;
    }
    refresh = true;
  }

  // 1秒ごとにファンの回転数を計算
  static uint32_t rpm = 0;
  static uint32_t tm = millis();
  if (millis() - tm > 1000) {
    tm = millis();
    rpm = (pulseCount * 60) / 2;
    pulseCount = 0;
    refresh = true;
  }

  // LCD描画
  if (refresh) {
    canvas.fillSprite(TFT_BLACK);
    drawButton(&canvas, 0,0, 119,66, 
      "Power", String(onoff ? "ON" : "OFF"), "", 
      menu==0, (menu==0 && entermenu));
    drawButton(&canvas, 120,0, 119,66, 
      "Fan", String(fan_ratio)+"%", String(rpm)+" rpm", 
      menu==1, (menu==1 && entermenu));
    drawButton(&canvas, 0,67, 119,66, 
      "LED", String(led_speed), "", 
      menu==2, (menu==2 && entermenu));
    drawButton(&canvas, 120,67, 119,66, 
      "Bright", String(led_brightness), "", 
      menu==3, (menu==3 && entermenu));
    canvas.pushSprite(0,0);
    refresh = false;
  }

  // ファンのスピードを反映
  if (set_fan_duty) {
    setFanDuty(onoff ? fan_ratio : 0);
  }

  // LEDを反映
  if (onoff) {
    // uint8_t hue = beat8(led_speed, 0); 
    // fill_rainbow(leds, LED_NUM, hue, 3);    
    led_index += led_speed * 8;
    fill_rainbow(leds, LED_NUM, led_index >> 8, 3);
    FastLED.setBrightness(led_brightness);
    FastLED.show();
  } else {
    FastLED.clear();
    FastLED.show();
  }

  delay(10);
}
