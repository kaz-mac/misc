/*
  時刻に応じてLEDの明るさを調整する for M5Stack M5Stamp Pico Mate

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#include <Arduino.h>

// WIFI関連
#include <WiFi.h>
const char* WIFI_SSID = "****";
const char* WIFI_PASS = "****";

// 動作設定
#define TIME_LED_ON_HOUR    6   // LEDの点灯開始時間（時）
#define TIME_LED_ON_MINUTE  0   // LEDの点灯開始時間（分）
#define TIME_LED_OFF_HOUR   18  // LEDの点灯終了時間（時）
#define TIME_LED_OFF_MINUTE 30  // LEDの点灯終了時間（分）
#define LED_BRIGHTNESS 31       // LEDの明るさ
#define LED_COLOR_TEMPERATURE 4200  // LEDの色温度(K)
#define NTP_SYNC_INTERVAL 7200*1000 // NTP同期間隔（mS）

// NTP関連
#include <esp_sntp.h>
const char NTP_TIMEZONE[] = "JST-9";
const char NTP_SERVER1[]  = "ntp.nict.jp";
const char NTP_SERVER2[]  = "ntp.jst.mfeed.ad.jp";

// GPIO設定
// #define PIN_LED    27   // 内蔵LED
#define PIN_LED    32   // 外付LED
#define PIN_BUTTON 39

// FastLED
#define NUM_LEDS   7    // LEDの数
#include <FastLED.h>
CRGB leds[NUM_LEDS];

// グローバル変数
unsigned long lastNtpSync = 0;  // 最後にNTP同期した時刻

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spp(k,v) Serial.println(String(k)+"="+String(v))
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)


// Wi-Fi接続する
bool wifiConnect() {
  bool stat = false;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Wifi connecting.");
    for (int j=0; j<10; j++) {
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASS);  //  Wi-Fi APに接続
      for (int i=0; i<10; i++) {
        if (WiFi.status() == WL_CONNECTED) break;
        Serial.print(".");
        delay(500);
      }
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("connected!");
        Serial.println(WiFi.localIP());
        stat = true;
        break;
      } else {
        Serial.println("failed");
        WiFi.disconnect();
      }
    }
  }
  return stat;
}

// NTP時刻同期を実行
bool syncNTP() {
  static bool ntpInitialized = false;
  
  // WiFi接続チェック
  if (!wifiConnect()) {
    sp("WiFi connection failed");
    return false;
  }
  
  // 初回のみNTP設定
  if (!ntpInitialized) {
    sp("NTP initializing...");
    configTzTime(NTP_TIMEZONE, NTP_SERVER1, NTP_SERVER2);
    ntpInitialized = true;
  }
  
  // NTP同期待機
  spn("NTP syncing...");
  int timeout = 0;
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    if (timeout++ > 20) {  // 10秒でタイムアウト
      sp("NTP sync timeout");
      return false;
    }
    spn(".");
    delay(500);
  }
  sp("done");
  
  // 同期完了
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  spf("NTP synced: %s\n", timeStr);
  
  return true;
}

// 色温度(K)からRGBに変換
CRGB colorTemperatureToCRGB(uint16_t kelvin) {
  float temp = kelvin / 100.0;
  float red, green, blue;
  
  // Red
  if (temp <= 66) {
    red = 255;
  } else {
    red = temp - 60;
    red = 329.698727446 * pow(red, -0.1332047592);
    if (red < 0) red = 0;
    if (red > 255) red = 255;
  }
  
  // Green
  if (temp <= 66) {
    green = temp;
    green = 99.4708025861 * log(green) - 161.1195681661;
  } else {
    green = temp - 60;
    green = 288.1221695283 * pow(green, -0.0755148492);
  }
  if (green < 0) green = 0;
  if (green > 255) green = 255;
  
  // Blue
  if (temp >= 66) {
    blue = 255;
  } else if (temp <= 19) {
    blue = 0;
  } else {
    blue = temp - 10;
    blue = 138.5177312231 * log(blue) - 305.0447927307;
    if (blue < 0) blue = 0;
    if (blue > 255) blue = 255;
  }
  
  return CRGB((uint8_t)red, (uint8_t)green, (uint8_t)blue);
}

// LEDの点灯制御
void ledControl(bool on) {
  if (on) {
    // 点灯
    CRGB color = colorTemperatureToCRGB(LED_COLOR_TEMPERATURE);
    ledSetColor(color, LED_BRIGHTNESS);
  } else {
    // 消灯
    ledSetColor(CRGB::Black, 0);
  }
}

// LEDを指定の色と明るさで光らす
void ledSetColor(CRGB color, uint8_t brightness) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = color;
  }
  FastLED.setBrightness(brightness);
  FastLED.show();
}

// セットアップ
void setup() {
  Serial.begin(115200);
  delay(1000);
  sp("\n\n=== M5Stamp-Pico Timer LED Start ===");

  // GPIO設定
  pinMode(PIN_BUTTON, INPUT);

  // FastLED設定
  FastLED.addLeds<WS2812, PIN_LED, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);

  // LEDを黄色に光らす
  ledSetColor(CRGB::Yellow, LED_BRIGHTNESS);

  // 初回のNTP時刻同期
  if (!syncNTP()) {
    sp("Initial NTP sync failed. Retrying...");
    delay(5000);
    syncNTP();  // リトライ
  }  
  lastNtpSync = millis();

  // セットアップ完了の合図
  ledSetColor(CRGB::Green, LED_BRIGHTNESS);
  delay(1000);
  ledSetColor(CRGB::Black, 0);
  sp("Setup complete!\n");
}

// メイン
void loop() {
  static unsigned long lastLedUpdate = 0;
  static int lastTimeInMinutes = -1;
  
  unsigned long currentMillis = millis();
  
  // 定期的にNTP時刻同期（起動後から指定間隔ごと）
  if (currentMillis - lastNtpSync >= NTP_SYNC_INTERVAL) {
    lastNtpSync = currentMillis;
    sp("\n--- Periodic NTP Sync ---");
    if (syncNTP()) {
      lastTimeInMinutes = -1;  // 同期成功時はLED更新を強制
    }
  }
  
  // 1分ごとに時刻をチェックしてLEDを更新
  if (currentMillis - lastLedUpdate >= 60000 || lastLedUpdate == 0) {
    lastLedUpdate = currentMillis;
    
    // WiFi接続状態チェック（切断されていたら再接続）
    if (WiFi.status() != WL_CONNECTED) {
      sp("WiFi disconnected. Reconnecting...");
      wifiConnect();
    }
    
    // 現在時刻を取得
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    int currentHour = timeinfo.tm_hour;
    int currentMinute = timeinfo.tm_min;
    int currentTimeInMinutes = currentHour * 60 + currentMinute;
    
    // 時刻が変わったらLEDを更新
    if (currentTimeInMinutes != lastTimeInMinutes) {
      lastTimeInMinutes = currentTimeInMinutes;
      
      // 時刻に応じたLED ON/OFF判定（時刻を分単位で比較）
      int ledOnTime = TIME_LED_ON_HOUR * 60 + TIME_LED_ON_MINUTE;
      int ledOffTime = TIME_LED_OFF_HOUR * 60 + TIME_LED_OFF_MINUTE;
      bool led = (currentTimeInMinutes >= ledOnTime && currentTimeInMinutes < ledOffTime);
      ledControl(led);
      
      // デバッグ出力
      char timeStr[32];
      strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
      const char* status = led ? "ON" : "OFF";
      spf("[%s] LED: %s\n", timeStr, status);
    }
  }
  
  delay(10);
} 