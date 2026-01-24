/*
  水冷ラジエーター用 温度によるファンの自動制御とポンプの手動設定 ver.2.0
  for M5Stack Din Meter

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#include <M5DinMeter.h>   // https://github.com/m5stack/M5DinMeter/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <FFat.h>

// 設定 WiFi
const char* WIFI_SSID = "****";
const char* WIFI_PASS = "****";
const char* MDNS_NAME = "gamingfan";   // mDNSに登録するホスト名
WiFiClient client;
WebServer server(80);

// 設定 GPIOポート
#define GPIO_FANRPM_PIN      2  // FANのout : Port B 黄
#define GPIO_FANPWM_PIN      1  // FANのPWM : Port B 白
#define GPIO_THERMISTOR_PIN 13  // サーミスタ : Port A 黄
#define GPIO_PUMPPWM_PIN    15  // ポンプのPWM : Port A 白
#define GPIO_POWER_HOLD_PIN 46  // DinMeter 電源 H=稼働時、L=スリープ時
#define GPIO_BTN_A          42  // DinMeter ボタンA (wake)

// PWMの設定
#define PWM_FREQ 25000        // PWM周波数
#define PWM_BIT 8             // PWMの分解能bit
#define ADC_RESOLUTIUON 4095  // ADCの分解能(0-4095)

// その他
#define EMARGENCY_TEMP 50     // 強制的にファンをフル回転にする温度
#define TEMP_HISTERESIS 0.8   // 温度判定にヒステリシスを持たせる範囲
#define START_PUMP_TIME 20    // 電源投入後、ポンプをフル回転させる時間
#define START_PUMP_SPEED 100  // 電源投入後、ポンプをフル回転させるスピード

// 設定 サーミスタの特性
#define THERMISTOR_R25  10000  // 25℃のときの抵抗値
#define THERMISTOR_B    3435   // B定数
#define THERMISTOR_RREF 4700   // リファレンス側の抵抗値（配線: GND - 抵抗 - GPIO - サーミスタ - 5.5V）

// 内部電圧の計算用 M5DinMeterの5Vと3.3Vの電圧値（実測値）
#define M5ATOMS3_3V3 3.37   // (未使用)
#define M5ATOMS3_5V0 5.50   // 12V入力時のGroveポート電圧実測値

// グローバル変数
static long prev_pos = 0;  // エンコーダーの前回位置
M5Canvas canvas(&DinMeter.Display);
double temp = 0;
uint32_t rpm = 0;
int fan = 100;
int pump = 100;
bool manualmode = false;
uint8_t conf[10] = {  // 温度設定 温度(以上になったら), 回転数(%)
  45, 100, 
  40,  70, 
  35,  50, 
  30,  20, 
   0,  10 
};

// ファイルに保存するデータ
const char* save_filename = "/conf.txt";
struct SaveData {
  uint8_t _check;
  int fan;
  int pump;
  bool manualmode;
  uint8_t conf[10];
};
SaveData fsdata;

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spp(k,v) Serial.println(String(k)+"="+String(v))
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#define array_length(x) (sizeof(x) / sizeof(x[0]))

// --------------------------------------------------------------------------------

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
  spf("FAN ratio=%d%% duty=%d\n", ratio, pwm_duty);
  ledcWrite(GPIO_FANPWM_PIN, pwm_duty);
}

// ポンプのスピードを変更する(0%-100%)
void setPumpDuty(int ratio) {
  int pwm_duty = pow(2, PWM_BIT) * (ratio / 100.0);
  spf("PUMP ratio=%d%% duty=%d\n", ratio, pwm_duty);
  ledcWrite(GPIO_PUMPPWM_PIN, pwm_duty);
}

// 温度を測定する NTCサーミスタ
// 配線: GND - 抵抗(4.7kΩ) - GPIO - サーミスタ - 5.5V
double measureTemperature(int count=10) {
  double tempsum = 0;
  for (int i=0; i<count; i++) {
    double adcmv = analogReadMilliVolts(GPIO_THERMISTOR_PIN); // 電圧測定
    // サーミスタの抵抗値計算: R_thermistor = R_ref × (V_supply - V_GPIO) / V_GPIO
    double rt = (M5ATOMS3_5V0 * 1000 - adcmv) / adcmv * THERMISTOR_RREF;
    tempsum += 1.0 / (1.0 / (25.0+273.15) + 1.0 / THERMISTOR_B * log(rt / THERMISTOR_R25)) - 273.15;
    if (i==0) spf("adc=%.0f Rt=%.1f temp=%.1f\n", adcmv, rt, tempsum);
    delay(10);
  }
  return tempsum / count;
}

// ファイルにデータを保存する
void fileSave() {
  fsdata._check = 123;  // 未初期化データの読み込み防止用
  fsdata.fan = fan;
  fsdata.pump = pump;
  fsdata.manualmode = manualmode;
  memcpy(fsdata.conf, conf, sizeof(conf));
  
  File file = FFat.open(save_filename, "w");
  if (!file) {
    sp("File save failed");
    return;
  }
  file.write((uint8_t*)&fsdata, sizeof(fsdata));
  file.close();
  sp("File saved");
}

// ファイルからデータを読み出す
void fileLoad() {
  if (!FFat.exists(save_filename)) {
    sp("File not found");
    return;
  }
  
  File file = FFat.open(save_filename, "r");
  if (!file) {
    sp("File load failed");
    return;
  }
  
  file.read((uint8_t*)&fsdata, sizeof(fsdata));
  file.close();
  
  if (fsdata._check == 123) {
    fan = fsdata.fan;
    pump = fsdata.pump;
    manualmode = fsdata.manualmode;
    memcpy(conf, fsdata.conf, sizeof(fsdata.conf));
    sp("File loaded");
  } else {
    sp("File data invalid");
  }
}

// ファンの回転数をカウントする
volatile uint32_t pulseCount = 0;
volatile uint32_t lastPulseTime = 0;
void IRAM_ATTR tachISR() {
  uint32_t now = micros();
  if (now - lastPulseTime > 500) {
    pulseCount++;
    lastPulseTime = now;
  }
}

// 32bitカラーからRGB565に変換
uint16_t rgb565(uint32_t color) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// ボタンと値を描画
void drawMenu(M5Canvas* canvas, int x, int y, int w, int h, String label, String text, String text2, bool selected, bool enterd) {
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

// --------------------------------------------------------------------------------

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

  // FatFSの初期化（未フォーマットの場合は自動フォーマット）
  if (!FFat.begin(true)) {
    sp("FFat initialization failed");
  }

  // FAN スピード PWMの設定
  pinMode(GPIO_FANPWM_PIN, OUTPUT);  // FAN PWM Pin
  ledcAttach(GPIO_FANPWM_PIN, PWM_FREQ, PWM_BIT);
  setFanDuty(fan);

  // ポンプ スピード PWMの設定
  pinMode(GPIO_PUMPPWM_PIN, OUTPUT);  // FAN PWM Pin
  ledcAttach(GPIO_PUMPPWM_PIN, PWM_FREQ, PWM_BIT);
  setPumpDuty(pump);

  // FAN 回転数取得の設定
  pinMode(GPIO_FANRPM_PIN, INPUT_PULLUP);  // FAN Sense Pin
  attachInterrupt(digitalPinToInterrupt(GPIO_FANRPM_PIN), tachISR, FALLING);
  
  // エンコーダーの設定
  prev_pos = DinMeter.Encoder.readAndReset();

  // WiFi接続
  DinMeter.Display.println("Wifi connecting...");
  spn("Wifi connecting.");
  for (int j=0; j<10; j++) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);  //  Wi-Fi APに接続
    for (int i=0; i<30; i++) {
      if (WiFi.status() == WL_CONNECTED) break;
      spn(".");
      delay(500);
    }
    if (WiFi.status() == WL_CONNECTED) {
      spn("connected! ");
      sp(WiFi.localIP());
      break;
    } else {
      sp("failed");
      WiFi.disconnect();
    }
    delay(2000);
  }

  // Webサーバーの設定
  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin(MDNS_NAME)) {  // http://XXXX.local/ でアクセスできるようにする
      sp("mDNS registerd!");
    } else {
      sp("mDNS error!");
      delay(2000);
    }
    server.on("/", []() { server.send(200, "text/plain", "OK"); });
    server.on("/conf", handleConf);
    server.on("/set", handleSet);
    server.onNotFound( []() { server.send(404, "text/plain", "Not Found\n\n"); });
    server.begin();
  }

  // 電源投入後20秒間はポンプを100%で動かす
  DinMeter.Display.println("Pump fullspeed on start");
  setPumpDuty(START_PUMP_SPEED);
  while (millis() < START_PUMP_TIME*1000) delay(10);

  // ファイルからデータを読み出す
  fileLoad();
}

// --------------------------------------------------------------------------------

// ループ
void loop() {
  static int menu = 0;
  static bool entermenu = false;
  static bool refresh = true;
  static bool init = true;
  M5.update();
  server.handleClient();

  // 1秒に1回行う処理
  refresh = false;
  static unsigned long nexttime = 0;
  if (nexttime < millis()) {
    nexttime = millis() + 1000;
    refresh = true;

    // 温度測定
    temp = measureTemperature();

    // (自動モード) 自動ファンコントロール
    if (!manualmode) {
      for (int i=0; i<array_length(conf); i+=2) {
        if ((fan == conf[i+1] && temp >= (conf[i]-TEMP_HISTERESIS)) || (temp >= conf[i])) {
          fan = conf[i+1];
          break;
        }
      }
    }
  }

  // ボタンを押したらメニューに入る/出る
  if (M5.BtnA.wasReleased()) {
    if (menu == 0) {  // [0] マニュアルモードのON/OFF
      manualmode = !manualmode;
      beep(manualmode ? BEEP_DOUBLE : BEEP_LONG, false);
    } else {
      entermenu = !entermenu;
      beep(BEEP_SHORT);
    }
    refresh = true;
    fileSave();  // ファイルにデータを保存する
  }

  // (マニュアルモード) エンコーダー状態取得
  long enc_pos = DinMeter.Encoder.read() / 2;
  if (enc_pos != prev_pos) {
    int delta = prev_pos - enc_pos;
    prev_pos = enc_pos;
    spf("enc_pos=%d delta=%d\n", enc_pos, delta);
    beep(BEEP_VSHORT, false);
    // メニュー操作
    if (entermenu) {
      // メニュー選択時
      if (menu == 1 && manualmode) { // [1] ファンのスピード
        fan = constrain(fan + delta*5, 0, 100);
      } else if (menu == 2) { // [2] ポンプのスピード（Autoモードでも変更可能）
        pump = constrain(pump + delta*5, 0, 100);
      } else if (menu == 3 && manualmode) { // [3] none
        // reserved
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
  static uint32_t tm = millis();
  if (millis() - tm >= 1000) {
    uint32_t elapsed = millis() - tm;
    tm = millis();
    // spf("pulseCount=%d elapsed=%dms ", pulseCount, elapsed);
    rpm = (pulseCount * 60) / 2;
    pulseCount = 0;
    // spf("rpm=%d\n", rpm);
    refresh = true;
  }

  // 非常冷却モード
  if (temp >= EMARGENCY_TEMP && fan < 100) {
    fan = 100;
    refresh = true;
  }

  // LCD描画
  if (refresh) {
    canvas.fillSprite(TFT_BLACK);
    drawMenu(&canvas, 0,0, 119,66, "Mode", 
      String(manualmode ? "Manual" : "Auto"), "", 
      menu==0, (menu==0 && entermenu));
    drawMenu(&canvas, 120,0, 119,66, "Fan", 
      String(fan)+"%", String(rpm)+" rpm", 
      menu==1, (menu==1 && entermenu));
    char buff[6];
    sprintf(buff, "%2.1f", temp);
    drawMenu(&canvas, 0,67, 119,66, "Pump", 
      String(pump)+"%", "Temp: "+String(buff)+" \'C", 
      menu==2, (menu==2 && entermenu));
    drawMenu(&canvas, 120,67, 119,66, "Info", 
      MDNS_NAME, "IP: " + String(WiFi.localIP()[3]), 
      menu==3, (menu==3 && entermenu));
    canvas.pushSprite(0,0);
    refresh = false;
  }

  // ファンのスピードを反映
  static int prev_fan = -1;
  if (fan != prev_fan) {
    setFanDuty(fan);
    prev_fan = fan;
  }

  // ポンプのスピードを反映
  static int prev_pump = -1;
  if (pump != prev_pump) {
    setPumpDuty(pump);
    prev_pump = pump;
  }

  delay(10);
}


// Webページ /conf ステータス表示と変更フォーム
void handleConf() {
  String html = "<html lang=\"ja\"><head>\n\
    <meta charset=\"UTF-8\">\n\
    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n\
    <title>ATOMS3</title>\n\
    <style>\n\
      body { font-family: Arial, sans-serif; font-size: 16px; margin: 15px; line-height: 1.6; }\n\
      h1 { font-size: 24px; margin-bottom: 10px; }\n\
      h2 { font-size: 20px; margin-top: 15px; margin-bottom: 10px; }\n\
      h3 { font-size: 18px; margin-top: 15px; margin-bottom: 8px; }\n\
      p { font-size: 16px; margin: 8px 0; }\n\
      button, input[type=\"submit\"] { font-size: 16px; padding: 10px 20px; margin: 5px 0; min-height: 44px; }\n\
      input[type=\"text\"] { font-size: 16px; padding: 8px; width: 60px; }\n\
      input[type=\"radio\"] { width: 20px; height: 20px; margin-right: 8px; vertical-align: middle; }\n\
      input[type=\"range\"] { width: 50%; height: 30px; vertical-align: middle; }\n\
      output { display: inline-block; min-width: 30px; font-weight: bold; margin-left: 5px; margin-right: 3px; }\n\
      table { border-collapse: collapse; width: 100%; margin: 10px 0; }\n\
      th, td { padding: 8px; text-align: left; border: 1px solid #ddd; font-size: 16px; }\n\
      th { background-color: #f2f2f2; }\n\
      hr { margin: 20px 0; }\n\
      .auto-reload { margin-top: 20px; padding: 15px; background-color: #f9f9f9; border: 1px solid #ddd; border-radius: 5px; }\n\
      .reload-btn { font-size: 16px; padding: 10px 20px; margin: 5px; min-height: 44px; cursor: pointer; }\n\
      .reload-btn.on { background-color: #4CAF50; color: white; border: none; }\n\
      .reload-btn.off { background-color: #f44336; color: white; border: none; }\n\
    </style>\n\
    <script>\n\
      function setCookie(name, value, days) {\n\
        var expires = \"\";\n\
        if (days) {\n\
          var date = new Date();\n\
          date.setTime(date.getTime() + (days * 24 * 60 * 60 * 1000));\n\
          expires = \"; expires=\" + date.toUTCString();\n\
        }\n\
        document.cookie = name + \"=\" + value + expires + \"; path=/\";\n\
      }\n\
      function getCookie(name) {\n\
        var nameEQ = name + \"=\";\n\
        var ca = document.cookie.split(';');\n\
        for(var i = 0; i < ca.length; i++) {\n\
          var c = ca[i];\n\
          while (c.charAt(0) == ' ') c = c.substring(1, c.length);\n\
          if (c.indexOf(nameEQ) == 0) return c.substring(nameEQ.length, c.length);\n\
        }\n\
        return null;\n\
      }\n\
      var autoReloadEnabled = false;\n\
      var reloadTimer = null;\n\
      function toggleAutoReload() {\n\
        autoReloadEnabled = !autoReloadEnabled;\n\
        setCookie('autoReload', autoReloadEnabled ? '1' : '0', 30);\n\
        updateReloadButton();\n\
        if (autoReloadEnabled) {\n\
          reloadTimer = setTimeout(function() { location.href='/conf'; }, 10000);\n\
        } else {\n\
          if (reloadTimer) clearTimeout(reloadTimer);\n\
        }\n\
      }\n\
      function updateReloadButton() {\n\
        var btn = document.getElementById('reloadBtn');\n\
        if (autoReloadEnabled) {\n\
          btn.className = 'reload-btn on';\n\
          btn.innerHTML = 'Auto Reload: ON';\n\
        } else {\n\
          btn.className = 'reload-btn off';\n\
          btn.innerHTML = 'Auto Reload: OFF';\n\
        }\n\
      }\n\
      window.onload = function() {\n\
        var savedState = getCookie('autoReload');\n\
        if (savedState === '1') {\n\
          autoReloadEnabled = true;\n\
          reloadTimer = setTimeout(function() { location.href='/conf'; }, 10000);\n\
        }\n\
        updateReloadButton();\n\
      };\n\
    </script>\n\
    </head><body>\n\
    <h1>M5Stack Fan Controller</h1>\n\
    <h2>Status</h2>\n\
    <table>\n\
      <tr><th>Temperature</th><td>" + String(temp) + " \'C</td></tr>\n\
      <tr><th>Fan Speed</th><td>" + String(fan) + " % (" + String(rpm) + " rpm)</td></tr>\n\
      <tr><th>Pump Speed</th><td>" + String(pump) + " %</td></tr>\n\
      <tr><th>Mode</th><td>" + (manualmode ? "Manual" : "Auto")+ "</td></tr>\n\
    </table>\n\
    <p><button onclick=\"location.href='/conf'\">Refresh</button></p>\n\
    <hr><h2>Settings</h2>\n\
    <form action=\"/set\" method=\"POST\">\n\
    <h3>Mode Selection</h3>\n\
    <p><input type=\"radio\" name=\"mode\" value=\"0\" " + String(!manualmode ? "checked":"") + "> Auto Mode</p>\n\
    <p><input type=\"radio\" name=\"mode\" value=\"1\" " + String(manualmode ? "checked":"") + "> Manual Mode</p>\n\
    <h3>Manual Mode Settings</h3>\n\
    <p>Fan: <input type=\"range\" name=\"fan\" value=\""+ String(fan) +"\" min=\"0\" max=\"100\" step=\"1\" oninput=\"this.nextElementSibling.value=this.value\"><output>"+ String(fan) +"</output>%</p>\n\
    <p>Pump: <input type=\"range\" name=\"pump\" value=\""+ String(pump) +"\" min=\"0\" max=\"100\" step=\"1\" oninput=\"this.nextElementSibling.value=this.value\"><output>"+ String(pump) +"</output>%</p>\n\
    <h3>Auto Mode Settings</h3>\n\
    <table><tr><th>Temperature</th><th>Fan (%)</th></tr>\n";
  for (int i=0; i<array_length(conf); i++) {
    html += (i % 2 == 0) ? "<tr>\n<td>temp. &gt; " : "<td> to ";
    html += "<input type=\"text\" name=\"" + String(i) + "\" value=\"" + String(conf[i]) + "\" size=\"4\">";
    html += (i % 2 == 0) ? "</td>\n" : " %</td></tr>\n";
  }
  html += "</table>\n\
    <p><input type=\"submit\" name=\"save\" value=\"Save\"></p>\n\
    </form>\n\
    <hr>\n\
    <div class=\"auto-reload\">\n\
      <h3>Auto Reload</h3>\n\
      <p>Automatically reload this page every 10 seconds.</p>\n\
      <button id=\"reloadBtn\" class=\"reload-btn off\" onclick=\"toggleAutoReload()\">Auto Reload: OFF</button>\n\
    </div>\n\
    </body></html>\n";
  server.send(200, "text/html", html);
  sp("HTTP: /conf");
}

// Webページ /set 設定変更
void handleSet() {
  if (server.method() == HTTP_POST) {
    manualmode = (server.arg("mode") == "1");
    fan = server.arg("fan").toInt();
    pump = server.arg("pump").toInt();
    sp("HTTP: Set fan to "+String(fan));
    sp("HTTP: Set pump to "+String(pump));
    for (int i=0; i<array_length(conf); i++) {
      conf[i] = server.arg(String(i)).toInt();
    }
    fileSave();  // ファイルにデータを保存する
  }
  server.send(200, "text/html", "<head><meta http-equiv=\"refresh\" content=\"0;url=/conf\"></head>");
  sp("HTTP: /set");
}

