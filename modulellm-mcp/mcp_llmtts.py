#!/usr/bin/env python3
# Module-LLM LLM/TTSのMCPサーバー
# Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
# Released under the MIT license.

# オリジナル https://github.com/anoken/modulellm_maniax
# Copyright (c) 2025 aNoken

import socket
import json
import time
import argparse
import asyncio
import sys
from typing import Any
from mcp.server.fastmcp import FastMCP
import signal

# Initialize FastMCP server
mcp = FastMCP(
    "module_llm",
    host="0.0.0.0",
    port=8000,
)

# グローバル変数
tts_sock = None
llm_work_id = None
tts_work_id = None
buffer_data = ""

def set_led_brightness(color: str, value: int) -> None:
    """LEDの明るさを設定する"""
    with open(f"/sys/class/leds/{color}/brightness", "w") as f:
        f.write(str(value))

def set_led_color(red: int, green: int, blue: int) -> None:
    """LEDの色を設定する"""
    set_led_brightness("R", red)
    set_led_brightness("G", green)
    set_led_brightness("B", blue)

def connect_server(host, port) -> bool:
    """StackFlowサーバーに接続する"""
    global tts_sock

    try:
        tts_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tts_sock.connect((host, port))
        print(f'サーバー {host}:{port} に接続しました')
    except Exception as e:
        print(f'エラー: {e}')
        return False

    return True

def disconnect_server():
    """StackFlowサーバーを切断する"""
    global tts_sock

    if tts_sock:
        tts_sock.close()
        tts_sock = None

def send_json_request(sock, request_data):
    """StackFlowサーバーにJSONリクエストを送信"""
    json_string = json.dumps(request_data)
    print(f'送信したリクエスト: {json_string}')
    try:
        sock.sendall(json_string.encode('utf-8'))
        time.sleep(1)
    except Exception as e:
        print(f'リクエスト送信エラー: {e}')

def receive_response(sock, timeout=20):
    """StackFlowサーバーからのレスポンスを受信して処理"""
    global buffer_data
    
    try:
        # バッファにデータがある場合はそれを使用
        if not buffer_data:
            sock.settimeout(None if timeout == 0 else timeout)
            data = sock.recv(4096)
            if not data:
                return None
            buffer_data = data.decode('utf-8')
        
        # 改行を含むか確認
        if '\n' in buffer_data:
            line, buffer_data = buffer_data.split('\n', 1)
            print(f'受信したレスポンス: {line}')
            return json.loads(line)
        else:
            response = buffer_data
            buffer_data = ""
            print(f'受信したレスポンス: {response}')
            return json.loads(response)
            
    except socket.timeout:
        print('レスポンス受信タイムアウト')
        return None
    except Exception as e:
        print(f'レスポンス受信エラー: {e}')
        buffer_data = ""
    return None

def setup_llm(model) -> bool:
    """LLMのセットアップを実行する"""
    global llm_work_id

    try:
        # LLMセットアップ
        llm_setup = {
            "request_id": "llm_001",
            "work_id": "llm",
            "action": "setup",
            "object": "llm.setup",
            "data": {
                "model": model,
                "response_format": "llm.utf-8.stream",
                "input": "llm.utf-8.stream",
                "enoutput": True,
                "max_token_len": 1023,
                "prompt": "あなたは、スタックチャン という名前の、親切で礼儀正しく正直なAI アシスタントです。。"
            }
        }
        send_json_request(tts_sock, llm_setup)
        res = receive_response(tts_sock)
        if res and res.get('error', {}).get('code', -999) != 0:
            raise Exception(f'LLMセットアップ エラー: {res["error"]["message"]}')
        llm_work_id = res['work_id']

    except Exception as e:
        print(f'エラー: {e}')
        return False

    return True

def setup_tts(model) -> bool:
    """TTSとオーディオのセットアップを実行する"""
    global tts_work_id

    try:
        # オーディオセットアップ
        audio_setup = {
            "request_id": "audio_setup",
            "work_id": "audio",
            "action": "setup",
            "object": "audio.setup",
            "data": {
                "capcard": 0,
                "capdevice": 0,
                "capVolume": 0.5,
                "playcard": 0,
                "playdevice": 1,
                "playVolume": 0.15
            }
        }
        send_json_request(tts_sock, audio_setup)
        res = receive_response(tts_sock)
        if res and res.get('error', {}).get('code', -999) != 0:
            raise Exception(f'オーディオセットアップ エラー: {res["error"]["message"]}')

        # TTSセットアップ
        tts_setup = {
            "request_id": "melotts_setup",
            "work_id": "melotts",
            "action": "setup",
            "object": "melotts.setup",
            "data": {
                "model": model,
                "response_format": "sys.pcm",
                "input": ["tts.utf-8.stream"],
                "enoutput": False,
                "enaudio": True
            }
        }
        send_json_request(tts_sock, tts_setup)
        res = receive_response(tts_sock)
        if res and res.get('error', {}).get('code', -999) != 0:
            raise Exception(f'TTSセットアップ エラー: {res["error"]["message"]}')
        tts_work_id = res['work_id']

    except Exception as e:
        print(f'エラー: {e}')
        return False

    return True

def exit_session():
    """StackFlowサーバーとのセッションを終了する"""
    global tts_sock, buffer_data
    
    # バッファをクリア
    buffer_data = ""
    
    if tts_sock:
        try:
            # リセット
            reset_request = {
                "request_id": "4",
                "work_id": "sys",
                "action": "reset"
            }
            send_json_request(tts_sock, reset_request)
            res = receive_response(tts_sock)
            if res and res.get('error', {}).get('code', -999) != 0:
                raise Exception(f'リセット エラー: {res["error"]["message"]}')

        except Exception as e:
            print(f'エラー: {e}')
        finally:
            disconnect_server()     # サーバーを切断

# MCP Tool: LLMにメッセージを送信する
@mcp.tool()
async def send_message(message: str) -> str:
    """Send a message to the LLM and get the response."""
    global tts_sock
    restext = ""
    timeout = 60

    if not tts_sock:
        return "\n---\nError: LLM is not initialized"

    try:
        # 推論リクエストの送信
        print("Message:",message)
        llm_request = {
            "request_id": "llm_001",
            "work_id": llm_work_id,
            "action": "inference",
            "object": "llm.utf-8.stream",
            "data": {
                "delta": message,
                "index": 0,
                "finish": True
            }
        }
        send_json_request(tts_sock, llm_request)
            
        # ストリーミングレスポンスの受信と表示
        start_time = time.time()
        while True:
            if time.time() - start_time > timeout:
                print(f"\nタイムアウト: 応答が{timeout}秒を超えました")
                break                
            res = receive_response(tts_sock, 1)
            if res and res.get('error', {}).get('code', 0) != 0:
                print(f'エラー: {res["error"]["message"]}')
                break
            if res and res.get('data', {}).get('delta'):
                print(res['data']['delta'], end='', flush=True)
                restext += res['data']['delta']
            if res and res.get('data', {}).get('finish'):
                print()
                restext += "\n"
                break

    except Exception as e:
        print(f'エラー: {e}')
        return f"\n---\nerror: {str(e)}"

    return "\n---\n" + restext

# MCP Tool: TTSでテキストを再生する
@mcp.tool()
async def speak_text(message: str) -> str:
    """It will speak when you give it a message. English only."""
    global tts_sock
    
    if not tts_sock:
        return "\n---\nError: TTS is not initialized"

    try:
        # TTS推論
        inference_request = {
            "request_id": "tts_inference",
            "work_id": tts_work_id,
            "action": "inference",
            "object": "tts.utf-8.stream",
            "data": {
                "delta": message,
                "index": 0,
                "finish": True
            }
        }
        send_json_request(tts_sock, inference_request)
        res = receive_response(tts_sock, 3)     # なぜか応答が来ないので3秒でタイムアウトさせる
        if res is None:
            print("レスポンスがタイムアウトしましたが、処理を続行します")
            
    except Exception as e:
        print(f'エラー: {e}')
        return f"\n---\nerror: {str(e)}"

    return "\n---\nsuccess"

# MCP Tool: LEDの明るさを設定する
@mcp.tool()
async def set_led_colors(red: int, green: int, blue: int) -> str:
    """Change the LED colors. (0-255)"""
    if (red < 0 or red > 255 or green < 0 or green > 255 or blue < 0 or blue > 255):
        return "Invalid color values. Please provide values between 0 and 255."
    
    set_led_color(red, green, blue)
    return "\n---\nsuccess"

async def main():
    parser = argparse.ArgumentParser(description='LLM MCP Server')
    parser.add_argument('--host', type=str, default='localhost', help='LLM server hostname')
    parser.add_argument('--port', type=int, default=10001, help='LLM server port')
    parser.add_argument('--ttsmodel', type=str, default='melotts_zh-cn', help='TTS Model')
    parser.add_argument('--llmmodel', type=str, default='qwen2.5-0.5B-prefill-20e', help='LLM Model')
    args = parser.parse_args()
    
    # シャットダウンハンドラの設定
    loop = asyncio.get_running_loop()
    def signal_handler():
        print("\nシャットダウン中...")
        exit_session()  # セッションを終了
        loop.stop()
    
    # Ctrl+Cのハンドリング
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, signal_handler)
    
    try:
        # StackFlowサーバーに接続
        if connect_server(args.host, args.port):
            print("Connected to server")
        else:
            raise Exception("Cannot connect to server")

        # LLMの初期化
        print(f"Initializing LLM with model: {args.llmmodel}")
        if setup_llm(args.llmmodel):    # LLMのセットアップ
            print("LLM initialization completed")
        else:
            raise Exception("LLM initialization failed")

        # TTSの初期化
        print(f"Initializing TTS with model: {args.ttsmodel}")
        if setup_tts(args.ttsmodel):    # TTSのセットアップ
            print("TTS initialization completed")
        else:
            raise Exception("TTS initialization failed")

        # MCPサーバーの起動
        print("Starting MCP server...")
        await mcp.run_sse_async()
        
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        print("Cleaning up...")
        exit_session()  # StackFlowサーバーとのセッションを終了

if __name__ == "__main__":
    asyncio.run(main())
