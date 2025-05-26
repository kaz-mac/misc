#!/usr/bin/env python
# -*- coding: utf-8 -*-

# ModuleLLMの使用可能なモデル一覧を取得する
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

def get_modellist():
    try:
        # APIクライアントの初期化
        client = Client(modulellm_api_url)
        
        # APIにリクエスト送信
        result = client.predict(
            api_name="/get_model_list"
        )
        print(f"{result}")

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
    success = get_modellist()
    sys.exit(0 if success else 1)
