/* M5StickCをBeetleCのコントローラーにする
 * BeetleCのソースはサンプルのまま使用 https://github.com/m5stack/M5-ProductExampleCodes/tree/master/Hat/beetleC/stickC/beetleC
*/
#include <M5StickC.h>
#include "WiFi.h"

// Wi-Fi設定 --------
#define WIFI_SSID "beetleC:xx:xx:xx:xx:xx:xx"   //自分のBeetleCのSSID
#define WIFI_PASS "12345678"

// beetleC接続先
const char BEETLEC_IP[] = "192.168.4.1";

// デバッグ --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)

// IMU(6軸センサー)補正値 -------- （↓examples/Basics/IMU で確認できる）
#define SH200Q_accX_Center   0.08  // 水平時のaccXの値 (SH200Q)
#define SH200Q_accY_Center   0.16  // 水平時のaccYの値
#define MPU6886_accX_Center  0.00  // 水平時のaccXの値 (MPU6886)
#define MPU6886_accY_Center  0.00  // 水平時のaccYの値

// IMUのX,Y軸の角度を求める
void get_imu_xy_degree (float *dx, float *dy) {
  float accX, accY, accZ;
  M5.IMU.getAccelData(&accX, &accY, &accZ);
  if (M5.IMU.imuType == M5.IMU.IMU_SH200Q) {
    accX = accX - SH200Q_accX_Center;
    accY = accY - SH200Q_accY_Center;
  } else if (M5.IMU.imuType == M5.IMU.IMU_MPU6886) {
    accX = accX - MPU6886_accX_Center;
    accY = accY - MPU6886_accY_Center;
  }
  *dx = accX * 90.0;
  *dy = accY * 90.0;
}

// beetleCにhttp GETでコマンドを送信する（left/right=-124〜124、color=0〜3）
void send_beetlec_command(int left, int right, int color) {
  int i;

  // サーバーに接続
  WiFiClient client;
  client.setTimeout(0);
  if (client.connect(BEETLEC_IP, 80)) {
    // httpリクエストの送信
    String uri = "/control?left="+String(left)+"&right="+String(right)+"&color="+String(color);
    client.print(String("GET ") + uri + " HTTP/1.1\r\n" +
      "Connection: close\r\n\r\n");
    sp("access http://"+String(BEETLEC_IP)+uri);
    for (i = 0; i < 100; i++) {
      if (client.available()) break;
      delay(10);
    }

    // ヘッダとコンテンツの取得
    String line;
    String buffer = "";
    while (client.available()) {
      line = client.readStringUntil('\r');
      line.trim();
      //sp("header|"+line);
      if (line.length() == 0) break;
    }
    while (client.available()) {
      line = client.readStringUntil('\r');
      line.trim();
      //sp("contents|"+line);
      buffer.concat(line);
    }
  }
  // サーバーから切断
  client.stop();
}

// beetleCの操作（bdirection/bspeed=-1.0〜1.0、bcolor=0〜3）
void beetlec(float bdirection, float bspeed, int bcolor) {
  int left  = bspeed * 124;
  int right = bspeed * 124;
  if (bdirection < 0) {
    left = left * (1.0 - fabs(bdirection));
  } else if (bdirection > 0) {
    right = right * (1.0 - fabs(bdirection));
  }
  if (left > 124) left = 124;
  if (left < -124) left = -124;
  if (right > 124) right = 124;
  if (right < -124) right = -124;
  send_beetlec_command(left, right, bcolor);
}

void setup() {
  M5.begin();

  // IMU初期化
  M5.IMU.Init();
  if (M5.IMU.imuType == M5.IMU.IMU_SH200Q) {
    sp("IMU is SH200Q");
  } else if (M5.IMU.imuType == M5.IMU.IMU_MPU6886) {
    sp("IMU is MPU6886");
  }

  //ディスプレイ
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(2, 2);
  M5.Lcd.println("Wi-Fi connecting...");
  sp("Wi-Fi connecting...");

  // Wi-Fi接続
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }
  sp("Wi-Fi connected.");

  M5.Lcd.fillScreen(BLACK);
}

void loop() {
  int i;
  float dx, dy;

  // 150msおきに実行する
  static int col = 0;
  static unsigned long tm = 0;
  if (tm + 150 < millis()) {

    // 角度を取得、dx=10〜55度、dy=10〜85度の範囲でモーターの速度を決める
    get_imu_xy_degree (&dx, &dy);
    float pm_x = (dx < 0) ? -1.0 : 1.0;
    float pm_y = (dy < 0) ? -1.0 : 1.0;
    float dx2 = fabs(dx) - 10.0;  //dx=10度以下の変化は無視
    float dy2 = fabs(dy) - 10.0;  //dy=10度以下の変化は無視
    if (dx2 > 45.0) dx2 = 45.0;  //dx=55度以上の変化は無視
    if (dy2 > 75.0) dy2 = 75.0;  //dy=85度以上の変化は無視
    if (dx2 < 0.0) dx2 = 0.0;
    if (dy2 < 0.0) dy2 = 0.0;
    float bdirection = (-dx2 / 45.0) * pm_x;
    float bspeed = (-dy2 / 75.0) * pm_y;
    spn("dx="+String(dx)+" dy="+String(dy)+" dx2="+String(dx2)+" dy2="+String(dy2)+" bdirection="+String(bdirection)+" bspeed="+String(bspeed)+" : ");
    
    // beetleCにコマンドを送信
    int bcolor = col++ % 3 + 1;
    beetlec(bdirection, bspeed, bcolor);
    tm = millis();
  }

  // 1sおきに実行する
  static unsigned long tm2 = 0;
  if (tm2 + 1000 < millis()) {
    // 未作成
    tm2 = millis();
  }

  M5.update();
  delay(10);
}
