/* 
 * Euro Track Simurator 2用 BLE接続ゲームパッド
 * ウィンカーとシフト（前後のみ）のボタンを付ける
 */
#include <M5AtomS3.h>
#include <BleGamepad.h>
BleGamepad bleGamepad("ETS2 Gamepad", "Kaz", 100);

// 設定 ボタン対応
#define GPIO_WINKER_LEFT  8
#define GPIO_WINKER_RIGHT 7
#define GPIO_GEAR_FORWARD 6
#define GPIO_GEAR_BACK    5
#define BTN_WINKER_LEFT   BUTTON_16
#define BTN_WINKER_RIGHT  BUTTON_15
#define BTN_GEAR_FORWARD  BUTTON_14
#define BTN_GEAR_BACK     BUTTON_13
#define BTN_TRAILER_SW    BUTTON_12

// デバッグに便利なマクロ定義 --------
#define sp(x) USBSerial.println(x)
#define spn(x) USBSerial.print(x)
#define spf(fmt, ...) USBSerial.printf(fmt, __VA_ARGS__)
#define lp(x) M5.Lcd.println(x)
#define lpn(x) M5.Lcd.print(x)
#define lpf(fmt, ...) M5.Lcd.printf(fmt, __VA_ARGS__)
#define array_length(x) (sizeof(x) / sizeof(x[0]))

// 関数定義
void lcdtext(String text, int size=-1, int x=-1, int y=-1);

// 初期化
void setup() {
  M5.begin(true, true, false, false);  // Init M5AtomS3
  M5.Lcd.setRotation(2);

  // GPIO設定
  pinMode(GPIO_WINKER_LEFT, INPUT_PULLDOWN);
  pinMode(GPIO_WINKER_RIGHT, INPUT_PULLDOWN);
  pinMode(GPIO_GEAR_FORWARD, INPUT_PULLDOWN);
  pinMode(GPIO_GEAR_BACK, INPUT_PULLDOWN);

  // BLE GamePadの設定（configを使うとなぜか反応なくなる。なぜ？）
  BleGamepadConfiguration config;
  // config.setButtonCount(16);
  // config.setHatSwitchCount(0);
  // config.setWhichAxes(true,true,false,false,false,false,false,false);
  //bleGamepad.begin(&config);
  bleGamepad.begin();

  sp("Start!!");
  M5.Lcd.print("START!!");
  delay(500);
  M5.Lcd.clear();
}

// ループ
void loop() {
  M5.update();
  bool update = false;
  static bool connlast = false;
  static int winker_last = 0;
  static int gear_last = 0;
  static bool trailer_last = false;
  static unsigned long autowinkeroff = 0;

  // BLEの接続状態
  bool connected = bleGamepad.isConnected();
  if (connected != connlast) {
    if (connected) {
      sp("BLE connected!");
      bleGamepad.setAxes(16384,16384,16384,16384,16384,16384,16384,16384);
    } else sp("BLE disconnected!");
    update = true;
  }
  connlast = connected;

  // ボタン操作
  if (connected) {
    // ウィンカーレバー
    if (digitalRead(GPIO_WINKER_LEFT) && winker_last != 1) {
      bleGamepad.press(GPIO_WINKER_LEFT);
      bleGamepad.release(GPIO_WINKER_RIGHT);
      sp("Winker: LEFT");
      winker_last = 1;
      autowinkeroff = millis() + 250;
      update = true;
    } else if (digitalRead(GPIO_WINKER_RIGHT) && winker_last != 2) {
      bleGamepad.release(GPIO_WINKER_LEFT);
      bleGamepad.press(GPIO_WINKER_RIGHT);
      sp("Winker: RIGHT");
      winker_last = 2;
      autowinkeroff = millis() + 250;
      update = true;
    } else if (!digitalRead(GPIO_WINKER_LEFT) && !digitalRead(GPIO_WINKER_RIGHT) && winker_last != 0) {
      if (winker_last == 1) bleGamepad.press(GPIO_WINKER_LEFT);
      else bleGamepad.press(GPIO_WINKER_RIGHT);
      sp("Winker: OFF");
      winker_last = 0;
      autowinkeroff = millis() + 250;
      update = true;
    } else if (autowinkeroff > 0 && autowinkeroff < millis()) {
      bleGamepad.release(GPIO_WINKER_LEFT);
      bleGamepad.release(GPIO_WINKER_RIGHT);
    }

    // シフトレバー
    if (digitalRead(GPIO_GEAR_FORWARD) && gear_last != 1) {
      bleGamepad.press(BTN_GEAR_FORWARD);
      bleGamepad.release(BTN_GEAR_BACK);
      sp("Gear: FORWARD");
      gear_last = 1;
      update = true;
    } else if (digitalRead(GPIO_GEAR_BACK) && gear_last != 2) {
      bleGamepad.release(BTN_GEAR_FORWARD);
      bleGamepad.press(BTN_GEAR_BACK);
      sp("Gear: BACK");
      gear_last = 2;
      update = true;
    } else if (!digitalRead(GPIO_GEAR_FORWARD) && !digitalRead(GPIO_GEAR_BACK) && gear_last != 0) {
      bleGamepad.release(BTN_GEAR_FORWARD);
      bleGamepad.release(BTN_GEAR_BACK);
      sp("Gear: NEUTRAL");
      gear_last = 0;
      update = true;
    }

    // トレーラーの連結（ボタン短押し）
    if (M5.Btn.isPressed() && !trailer_last) {
      bleGamepad.press(BTN_TRAILER_SW);
      sp("Trailer: press");
      trailer_last = true;
      update = true;
    } else if (!M5.Btn.isPressed() && trailer_last) {
      bleGamepad.release(BTN_TRAILER_SW);
      sp("Trailer: release");
      trailer_last = false;
      update = true;
    }
  }

  // 状態の強制リセット（ボタン長押し2秒）
  if (M5.Btn.pressedFor(2000)) {
    winker_last = 0;
    gear_last = 0;
    trailer_last = 0;
    autowinkeroff = 0;
    update = true;
    sp("RESET!!!");
  }

  // ディスプレイの更新
  if (update) {
    String str;
    M5.Lcd.clear();
    str = (connected) ? "Connected" : "none";
    lcdtext(str+"\n\n", 2, 0, 0);
    str = (winker_last==1) ? "<--" : ((winker_last==2) ? "-->" : "");
    lcdtext("W: "+str+"\n");
    str = (gear_last==1) ? "^^^" : ((gear_last==2) ? "vvv" : "");
    lcdtext("G: "+str+"\n");
    str = (trailer_last) ? "*" : "-";
    lcdtext("T: "+str+"\n");
  }

  delay(10);
}

// LCDにテキストを表示
void lcdtext(String text, int size, int x, int y) {
  static int oldsize;
  if (x != -1 && y != -1) M5.Lcd.setCursor(x, y);
  if (size != -1) M5.Lcd.setTextSize(size);
  M5.Lcd.print(text);
  oldsize = size;
}
