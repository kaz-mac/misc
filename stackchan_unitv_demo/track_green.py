# 緑色の領域を追跡する
# for M5Stack UnitV (K210)
# 
# Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
# Released under the MIT license.
# see https://opensource.org/licenses/MIT

import sensor
import KPU as kpu
import image
import lcd
import time

from machine import UART
from fpioa_manager import fm
# from modules import ws2812    # WS2812はデフォルトのファームウェア以外では有効になっていない可能性あり

# 閾値の設定（MaixPy IDEのツール→マシンビジョン→閾値エディタで調整する）
#green_threshold   = (0,   80,  -70,   -10,   -0,   30)   # 緑色の領域を追跡するための閾値（サンプルのオリジナル値）
green_threshold   = (0,   80,  -70,   -15,   -0,   30)   # 緑色の領域を追跡するための閾値

# LCD初期化（UnitVはディスプレイが無いから不要）
# lcd.init()
# lcd.rotation(0)

# シリアルポートの設定
fm.register(35, fm.fpioa.UART1_TX, force=True)
fm.register(34, fm.fpioa.UART1_RX, force=True)
uart = UART(UART.UART1, 115200,8,0,0, timeout=1000, read_buf_len=4096)

# カメラモジュールの準備
sensor.reset(dual_buff=True)        # デュアルバッファを有効にする
sensor.set_pixformat(sensor.RGB565) # カラーモード RGB565
sensor.set_framesize(sensor.QVGA)   # サイズ QVGA 320x240
sensor.set_hmirror(0)   #カメラミラーリングの設定
sensor.set_vflip(0)     #カメラのフリップを設定する
sensor.skip_frames(time = 2000)     # 2秒間フレームをスキップして安定化
sensor.set_auto_exposure(False, 10000)  # 蛍光灯のフリッカー対策(50Hz) 固定露出時間10ms(自動露出無効)
sensor.run(1)           #カメラを有効にする

# その他の初期化
# class_ws2812 = ws2812(8, 1)
clock = time.clock()
color = (255, 0, 0)
max_area = 25   # 最大面積（ピクセル）
no_hit = 0

# メインループ
while True:
    # 緑領域を検出する
    clock.tick()
    img = sensor.snapshot()
    # blobs = img.find_blobs([green_threshold])
    # blobs = img.find_blobs([green_threshold], x_stride=2, y_stride=2, pixels_threshold=100, merge=True, margin=20)
    # blobs = img.find_blobs([green_threshold], x_stride=2, y_stride=2, pixels_threshold=64)
    blobs = img.find_blobs([green_threshold], pixels_threshold=max_area)  # 面積36以上の領域を検出

    # 最大の緑色領域を検出
    if blobs:
        # print(blobs)
        blob = max(blobs, key=lambda b: b.area()) # 面積が最大の領域を取得
        # 認識した緑色領域を表示
        img.draw_rectangle(blob[0:4], color=color)
        img.draw_cross(blob[5], blob[6], color=color)

    # ディスプレイ出力
    # lcd.display(img)
    fps = clock.fps()
    #print("%2.1f fps" % fps)

    # シリアルに出力
    if blobs:
        # hit, x, y, w, h, pixel, cx, cy, fps
        data = "1,{},{},{},{},{},{},{},{}".format(blob[0], blob[1], blob[2], blob[3], blob[4], blob[5], blob[6], int(fps))
        print(data)
        uart.write(data + "\r\n")
    else:
        # 見つからない場合も死活監視のため出力
        no_hit += 1
        if no_hit > 30:
            data = "0,,,,,,,{}".format(int(fps))
            print(data)
            uart.write(data + "\r\n")
            no_hit = 0

