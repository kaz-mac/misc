#!/usr/bin/env python
# -*- coding: utf-8 -*-

# ModuleLLMのモデルを変更する
#
# Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
# Released under the MIT license.

import time
import sys
import requests
import argparse
from gradio_client import Client

modulellm_api_url = "http://192.168.xx.xx:7860"

# Windows環境での文字化けを防ぐためにUTF-8に設定
if sys.platform.startswith('win'):
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')

def change_model(model: str) -> bool:
    try:
        # APIクライアントの初期化
        client = Client(modulellm_api_url)
        
        # APIにリクエスト送信
        result = client.predict(
            model=model,
            api_name="/change_model"
        )
        
        print(f"change_model {model} : {result}")
        if result == "success":
            return True
        else:
            print(f"エラー: モデル変更に失敗しました（結果: {result}）")
            return False

    except requests.exceptions.Timeout:
        print(f"エラー: APIリクエストがタイムアウトしました")
        return False
    except requests.exceptions.RequestException as e:
        print(f"エラー: APIリクエストに失敗しました: {e}")
        return False
    except Exception as e:
        print(f"エラーが発生しました: {e}")
        return False

if __name__ == "__main__":
    # コマンドライン引数の解析
    parser = argparse.ArgumentParser(description="モデル変更プログラム")
    parser.add_argument("--model", type=str, required=True, help="変更するモデル名")
    args = parser.parse_args()

    # モデル変更の実行
    success = change_model(args.model)
    sys.exit(0 if success else 1)
