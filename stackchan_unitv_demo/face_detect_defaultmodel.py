# 人の顔を追跡する
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

# モデルの読み込み
task_fd = kpu.load(0x300000)  # デフォルトのファームウェアの0x300000にあるfacedetect.kmodelを読み込む
anchor = (1.889, 2.5245, 2.9465, 3.94056, 3.99987, 5.3658, 5.155437, 6.92275, 6.718375, 9.01025)
kpu.init_yolo2(task_fd, 0.5, 0.3, 5, anchor)

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
clock = time.clock()
no_hit = 0

# メインループ
while True:
    # 顔を検出する
    clock.tick()
    img = sensor.snapshot()
    code = kpu.run_yolo2(task_fd, img)
    if code:
        #print(code)
        for i in code:
            # 認識した顔の領域を表示
            img.draw_rectangle(i.x(), i.y(), i.w(), i.h())
    
    # ディスプレイ出力
    fps = clock.fps()

    # シリアルに出力
    if code:
        # hit, x, y, w, h, pixel, cx, cy, fps のフォーマットに合わせる
        x = code[0].x()
        y = code[0].y()
        w = code[0].w()
        h = code[0].h()
        cx = x + w / 2
        cy = y + h / 2
        area = w * h /2
        data = "1,{},{},{},{},{},{},{},{}".format(x, y, w, h, area, cx, cy, int(fps))
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

