/* 
 * Euro Track Simurator 2用 BLE接続 H-Shifter 
 * M5Stack ATOM Lite + EXT.I/O 2ユニット使用
 */
#include <M5Unified.h>

// BLE関連
#include <BleGamepad.h>
BleGamepad bleGamepad("ETS2 HShifter", "Kaz", 100);

// EXT.I/O 2ユニット関連(STM32F030)
#include "M5_EXTIO2.h"  // https://github.com/m5stack/M5Unit-EXTIO2/
M5_EXTIO2 extio;

// RGB LED関連
#include <FastLED.h>
#define LED_NUM 1
CRGB leds[LED_NUM];

// 仮想ボタンライブラリ
#include "AnyButton.h"
#define SHIFT_NUM 7
AnyButton btnShift[SHIFT_NUM];
AnyButton btnPush, btnAtom; 

// 設定 機種
#define ATOMLite
//#define ATOMS3Lite

// 設定 GPIOポート
#ifdef ATOMLite
  #define GPIO_M5BUTTON  39
  #define GPIO_M5LED 27
  #define GPIO_M5SDA 26
  #define GPIO_M5SCL 32
#endif
#ifdef ATOMS3Lite
  #define GPIO_M5BUTTON  35
  #define GPIO_M5LED 41
  #define GPIO_M5SDA 2
  #define GPIO_M5SCL 1
#endif

// 設定 I/Oユニット ボタン対応
#define IO_SHIFTER_BTN 1 // 1～7
#define IO_SHIFTER_SW  0

// 設定 BLE Gamepad ボタン対応
#define PAD_SHIFTER_BTN    BUTTON_1   // IO=1～7
#define PAD_SHIFTER_SW_OFF BUTTON_8   // IO=0
#define PAD_SHIFTER_SW_ON  BUTTON_9   // IO=0
#define PAD_M5BUTTON       BUTTON_10

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#define array_length(x) (sizeof(x) / sizeof(x[0]))

// LEDの色を変更する
void led(CRGB color) {
  leds[0] = color;
  FastLED.show();
}

int redd_io(int io) {
  return (extio.getDigitalInput(io)) ? 1 : 0;
}

// 初期化
void setup() {
  M5.begin();
  Serial.begin(115200);
  delay(200);
  Serial.println("System Start!");

  // LEDの設定
  FastLED.addLeds<WS2811, GPIO_M5LED, GRB>(leds, LED_NUM);
  FastLED.setBrightness(20);
  led(CRGB::Red);

  // GPIO設定
  pinMode(GPIO_M5BUTTON, INPUT_PULLDOWN);

  // EXT.I/O 2ユニット設定
  while (!extio.begin(&Wire, GPIO_M5SDA, GPIO_M5SCL, 0x45)) {
    delay(100);
  }
  extio.setAllPinMode(DIGITAL_INPUT_MODE);
  led(CRGB::Yellow);

  // 仮想ボタン設定
  btnAtom.configButton(AnyButton::TypePush, AnyButton::ModeDirect, AnyButton::SpanEver);
  btnPush.configButton(AnyButton::TypePush, AnyButton::ModeSelect, AnyButton::SpanEver);
  for (int i=0; i<SHIFT_NUM; i++) {
    btnShift[i].configButton(AnyButton::TypePush, AnyButton::ModeDirect, AnyButton::SpanEver);
  }

  // BLE GamePadの設定
  bleGamepad.begin();

  sp("Wainting for BLE");
  delay(1000);
}

// ループ
void loop() {
  M5.update();
  static bool connlast = false;
  bool update = false;

  // BLEの接続状態を取得する
  bool connected = bleGamepad.isConnected();
  if (connected != connlast) {
    if (connected) {
      sp("BLE connected!");
      bleGamepad.setAxes(16384,16384,16384,16384,16384,16384,16384,16384);
      led(CRGB::Green);
    } else {
      sp("BLE disconnected!");
      led(CRGB::Black);
    }
    update = true;
  }
  connlast = connected;

  // BLE未接続時はLEDを点滅させる
  if (! connected) {
    static uint32_t tmble = 0;
    static bool bleled = false;
    if (tmble < millis()) {
      sp("BLE waiting...");
      bleled = !bleled;
      led(bleled ? CRGB::Blue : CRGB::Black);
      tmble = millis() + 1000;
    }
  }

  // ボタン操作
  int state;
  if (connected) {

    // レンジスイッチ（プッシュボタンでトグル動作）
    btnPush.loadState( redd_io(IO_SHIFTER_SW) );
    if ((state = btnPush.getStateChanged()) != -1) {
      if (state == 1) {
        bleGamepad.release(PAD_SHIFTER_SW_OFF);
        bleGamepad.press(PAD_SHIFTER_SW_ON);
        sp("Range SW1: A");
      } else if (state == 2) {
        bleGamepad.release(PAD_SHIFTER_SW_ON);
        bleGamepad.press(PAD_SHIFTER_SW_OFF);
        sp("Range SW1: B");
      }
      update = true;
    }

    // シフトのポジション（7個のリードスイッチでON/FF）
    int positionSet = -1;
    int positionRelease = -1;
    for (int i=0; i<SHIFT_NUM; i++) {
      btnShift[i].loadState( redd_io(IO_SHIFTER_BTN + i) );
      if ((state = btnShift[i].getStateChanged()) != -1) {
        if (state == 2) positionSet = i;
        else if (state == 1) positionRelease = i;
        update = true;
      }
    }
    if (positionRelease != -1) {
      bleGamepad.release(PAD_SHIFTER_BTN + positionRelease);
      sp("Position release: "+String(positionRelease));
    }
    if (positionSet != -1) {
      bleGamepad.press(PAD_SHIFTER_BTN + positionSet);
      sp("Position press: "+String(positionSet));
    }

    // ATOM Lite本体のボタン（長押し2秒）シフトの状態の強制リセット
    if (M5.BtnA.pressedFor(2000)) {
      while (M5.BtnA.isPressed()) {
        M5.update();
        delay(5);
      }
      for (int i=0; i<SHIFT_NUM; i++) {
        bleGamepad.release(PAD_SHIFTER_BTN + i);
      }
      sp("All Position release!");
    }

    // ATOM Lite本体のボタン（短押し）おまけ。別にライブラリを使う必要もない
    btnAtom.loadState( (M5.BtnA.isPressed()==1) );
    if ((state = btnAtom.getStateChanged()) != -1) {
      if (state == 2) {
        bleGamepad.press(PAD_M5BUTTON);
        sp("Atom: press");
      } else if (state == 1) {
        bleGamepad.release(PAD_M5BUTTON);
        sp("Atom: release");
      }
      update = true;
    }
  }

  // ディスプレイの更新
  if (update) {
    //
  }

  delay(10);
}
