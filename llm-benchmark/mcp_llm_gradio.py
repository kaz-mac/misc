#!/usr/bin/env python3
# Module-LLM LLMのMCPサーバー gradio版
# Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
# Released under the MIT license.

# オリジナル https://github.com/anoken/modulellm_maniax
# Copyright (c) 2025 aNoken

# 参考 StackFlowのコマンド
# https://github.com/m5stack/StackFlow/blob/main/doc/projects_llm_framework_doc/llm_llm_en.md

import socket
import json
import time
import argparse
import asyncio
import signal
import sys
import gradio as gr
import traceback

# グローバル変数
sf_sock = None
llm_work_id = None
tts_work_id = None
buffer_data = ""
running_model = ""
system_prompt = "あなたは、スタックチャン という名前の、親切で礼儀正しく正直なAI アシスタントです。。"
llm_sequence = 0
tts_sequence = 0
quiet = False


def connect_server(host, port) -> bool:
    """StackFlowサーバーに接続する"""
    global sf_sock

    try:
        sf_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sf_sock.connect((host, port))
        print(f'サーバー {host}:{port} に接続しました')
    except Exception as e:
        print(f'エラー: {e}')
        return False

    return True


def disconnect_server():
    """StackFlowサーバーを切断する"""
    global sf_sock

    if sf_sock:
        sf_sock.close()
        sf_sock = None


def send_json_request(sock, request_data):
    """StackFlowサーバーにJSONリクエストを送信"""
    json_string = json.dumps(request_data, ensure_ascii=False) + '\n'
    if not quiet:
        print(f'送信したリクエスト: {json_string}')
    try:
        sock.sendall(json_string.encode('utf-8'))
        time.sleep(1)
    except Exception as e:
        print(f'リクエスト送信エラー: {e}')


def receive_response(sock, timeout=20, request_id=None):
    """StackFlowサーバーからのレスポンスを受信して処理 1行分のJSONデータを返す"""
    global buffer_data
    jsondata = None
    
    start_time = time.time()
    sock.settimeout(0)
    while True:
        # タイムアウト
        if time.time() - start_time > timeout:
            break

        # 新規データを受信し、データをバッファに追加
        try:
            data = sock.recv(4096)
            if data:
                #buffer_data += data.decode('utf-8')
                text = data.decode('utf-8')
                buffer_data += text
                # print(f'DATA: {text}')
        
        except BlockingIOError: # データがない場合
            #print(f'データがない buffer_data=\"{buffer_data}\"')
            pass
        
        except socket.timeout:
            print('レスポンス受信タイムアウト')

        except Exception as e:
            print(f'レスポンス受信エラー: {e}')
            break

        # 改行が出現したらJSONデコード
        try:
            jsondata = None
            if '\n' in buffer_data:
                line, buffer_data = buffer_data.split('\n', 1)
                jsondata = json.loads(line)
                if not quiet:
                    print(f'受信したレスポンス1: {line}')
            elif buffer_data:
                jsondata = json.loads(buffer_data)
                if not quiet:
                    print(f'受信したレスポンス2: {buffer_data}')
            # request_idが一致するかチェック
            if not jsondata: continue
            if request_id:
                if jsondata.get('request_id', None) == request_id:
                    break
                else:
                    if not quiet:
                        print(f'request_id({request_id})が一致しないデータを破棄します: {jsondata}')
            else:
                break
        except json.JSONDecodeError as e:
            pass
    
        time.sleep(0.1)

    return jsondata if jsondata else {}


def setup_llm(model) -> bool:
    """LLMのセットアップを実行する"""
    global llm_work_id

    try:
        # LLMセットアップ
        request_id = f"llm_{int(time.time())}_setup"
        llm_setup = {
            "request_id": request_id,
            "work_id": "llm",
            "action": "setup",
            "object": "llm.setup",
            "data": {
                "model": model,
                #"response_format": "llm.utf-8.stream",
                "response_format": "llm.utf-8.stream",
                #"input": "llm.utf-8.stream",
                "input": "llm.utf-8",
                "enoutput": True,
                "max_token_len": 1023,
                "prompt": system_prompt
            }
        }
        send_json_request(sf_sock, llm_setup)
        res = receive_response(sf_sock, 180, request_id)
        if res and res.get('error', {}).get('code', -999) != 0:
            raise Exception(f'LLMセットアップ エラー: {res["error"]["message"]}')
        llm_work_id = res['work_id']

    except Exception as e:
        print(f'エラー setup_llm: {e}')
        return False

    return True


def setup_tts(model) -> bool:
    """TTSとオーディオのセットアップを実行する"""
    global tts_work_id

    try:
        # オーディオセットアップ
        request_id = f"audio_{int(time.time())}_setup"
        audio_setup = {
            "request_id": request_id,
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
        send_json_request(sf_sock, audio_setup)
        res = receive_response(sf_sock, 180, request_id)
        if res and res.get('error', {}).get('code', -999) != 0:
            raise Exception(f'オーディオセットアップ エラー: {res["error"]["message"]}')

        # TTSセットアップ
        request_id = f"tts_{int(time.time())}_setup"
        tts_setup = {
            "request_id": request_id,
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
        send_json_request(sf_sock, tts_setup)
        res = receive_response(sf_sock, 180, request_id)
        if res and res.get('error', {}).get('code', -999) != 0:
            raise Exception(f'TTSセットアップ エラー: {res["error"]["message"]}')
        tts_work_id = res['work_id']

    except Exception as e:
        print(f'エラー setup_tts: {e}')
        return False

    return True


def setup_link_llmtts():
    """LLMの出力をTTSの入力にリンクする"""
    global llm_work_id, tts_work_id

    try:
        request_id = f"link_{int(time.time())}_setup"
        link_setup = {
            "request_id": request_id,
            "work_id": tts_work_id,
            "action": "link",
            "object": "work_id",
            "data": llm_work_id
        }
        send_json_request(sf_sock, link_setup)
        res = receive_response(sf_sock, 180, request_id)
        if res and res.get('error', {}).get('code', -999) != 0:
            raise Exception(f'LLM/TTSリンク エラー: {res["error"]["message"]}')
        
    except Exception as e:
        print(f'エラー setup_link_llmtts: {e}')
        return False

    return True


def exit_llm() -> bool:
    """LLMモデルの利用を終了する"""
    global sf_sock, buffer_data, llm_work_id
    
    buffer_data = ""
    if sf_sock:
        try:
            # 終了
            request_id = f"exit_{int(time.time())}"
            reset_request = {
                "request_id": request_id,
                "work_id": llm_work_id,
                "action": "exit"
            }
            send_json_request(sf_sock, reset_request)
            res = receive_response(sf_sock, 20, request_id)
            if res and res.get('error', {}).get('code', -999) == 0:
                return True
            else:
                raise Exception(f'LLM終了 エラー: {res["error"]["message"]}')
        except Exception as e:
            print(f'エラー exit_llm: {e}')

    return False


def exit_session():
    """StackFlowサーバーとのセッションを終了する"""
    global sf_sock, buffer_data
    
    buffer_data = ""
    if sf_sock:
        try:
            # リセット
            request_id = f"reset_{int(time.time())}"
            reset_request = {
                "request_id": request_id,
                "work_id": "sys",
                "action": "reset"
            }
            send_json_request(sf_sock, reset_request)
            res = receive_response(sf_sock, 20, request_id)
            if res and res.get('error', {}).get('code', -999) != 0:
                raise Exception(f'リセット エラー: {res["error"]["message"]}')
        except Exception as e:
            print(f'エラー exit_session: {e}')
        finally:
            disconnect_server()     # サーバーを切断


def abort_llm():
    """LLMの推論を中止する"""
    # https://github.com/m5stack/StackFlow/blob/main/doc/projects_llm_framework_doc/llm_llm_en.md
    global llm_work_id

    # # 受信バッファを空にする
    # tts_sock.setblocking(False) # ノンブロッキングモードに切り替え
    # try:
    #     while True:
    #         chunk = tts_sock.recv(4096)
    #         if chunk:
    #             print(f'破棄する受信データ: {chunk}')
    #         else:
    #             break
    # except BlockingIOError:
    #     pass
    # finally:
    #     tts_sock.setblocking(True)  # 元のブロッキングモードに戻す

    # 中断コマンドを送信（pauseしたままでいいのかは知らない）
    try:
        request_id = f"pause_{int(time.time())}"
        abort_setup = {
            "request_id": request_id,
            "work_id": llm_work_id,
            "action": "pause",
        }
        send_json_request(sf_sock, abort_setup)
        res = receive_response(sf_sock, 20, request_id)
        if res and res.get('error', {}).get('code', -999) != 0:
            raise Exception(f'推論停止 エラー: {res["error"]["message"]}')
        
    except Exception as e:
        print(f'エラー abort_llm: {e}')
        return False

    # ダミーの推論をさせる
    # print("ダミーの推論をします")
    # send_message_llm("hello", 100, False)

    
# MCP Tool: LLMのモデルを変更する
def change_model(model: str) -> str:
    """LLMのモデルを変更する
    Args:
        model (str): モデル名
    Returns:
        bool: 成功したらTrue
    """
    global running_model

    if model:
        exit_llm()
        if setup_llm(model):
            running_model = model
            return "success"
        else:
            print(f'モデル変更 エラー: New model={model}')
            return "error"


# MCP Tool: 使用可能なモデルの一覧を取得
def get_model_list(long=False) -> str:
    """使用可能なモデルの一覧を取得する
    Returns:
        str: モデル一覧
    """
    models = []

    try:
        request_id = f"sys_lsmode_{int(time.time())}"
        lsmode_setup = {
            "request_id": request_id,
            "work_id": "sys",
            "action": "lsmode",
        }
        send_json_request(sf_sock, lsmode_setup)
        res = receive_response(sf_sock, 20, request_id)
        if not res or res.get('error', {}).get('code', -999) != 0:
            raise Exception(f'エラー: {res["error"]["message"]}')
        
        # error.code==0 の場合のみ続行
        for item in res.get("data", []):
            if long:
                models.append(f"{item.get('type','')}: {item.get('mode','')}")
            else:
                if item.get("type") == "llm":
                    models.append(item.get("mode", ""))

    except Exception as e:
        print(f'エラー get_model_list: {e}')
        return "モデル一覧取得エラー"

    # 改行区切りで返す
    return "\n".join(sorted(models)) if models else "利用可能なLLMモデルはありません"


# MCP Tool: LLMにメッセージを送信する
def send_message(message: str, timeout: int) -> str:
    """LLMにメッセージを送信する
    Args:
        message (str): 送信するメッセージ
        timeout (int): タイムアウト時間
    Returns:
        str: 応答テキスト
    """
    return send_message_llm(message, timeout, True)


# LLMにメッセージを送信する
def send_message_llm(message: str, timeout: int, abort=True) -> str:
    """LLMにメッセージを送信する
    Args:
        message (str): 送信するメッセージ
        timeout (int): タイムアウト時間
        abort (bool): 異常時に推論を中断するかどうか
    Returns:
        str: 応答テキスト
    """
    global sf_sock, llm_sequence
    restext = ""
    noresponse_count = 2
    same_response_count = 5  # 同じ回答が続いたら中断
    same_response_words = 3  # それは何種類の同じ回答か?（ただし{same_response_count}回につき1種類とカウント）
    llm_sequence += 1
    # json_zero_index = False
    if not sf_sock:
        return "Error: LLM is not initialized"

    try:
        # 推論リクエストの送信
        print("Message:",message)
        request_id = f"llm_{int(time.time())}_{llm_sequence}"
        llm_request = {
            "request_id": request_id,
            "work_id": llm_work_id,
            "action": "inference",
            "object": "llm.utf-8.stream",
            "data": {
                "delta": message,
                "index": 0,
                "finish": True
            }
        }
        # llm_request = {
        #     "request_id": request_id,
        #     "work_id": llm_work_id,
        #     "action": "inference",
        #     "object": "llm.utf-8",
        #     "data": message
        # }
        send_json_request(sf_sock, llm_request)
            
        # ストリーミングレスポンスの受信と表示
        start_time = time.time()
        no_response = 0
        last_responses = {}
        stop_inference = False
        while True:
            # タイムアウト
            if time.time() - start_time > timeout:
                print(f"\nタイムアウト: 応答が{timeout}秒を超えました")
                stop_inference = True
                break                
            # JSONレスポンスの受信
            res = receive_response(sf_sock, 20, request_id)
            if res == None:
                no_response += 1
                if no_response > 3:
                    print(f"\nタイムアウト: 無応答が{noresponse_count}回を超えました")
                    stop_inference = True
                    break
                continue
            # エラーチェック
            if res and res.get('error', {}).get('code', -1) != 0:
                print(f'エラー: {res["error"]["message"]}')
                break
            # テキスト取得
            data = ""
            if res and res.get('data', {}).get('delta' , ""):
                data = res['data']['delta']
                print(data, end='', flush=True)
            # 同じレスポンスが5回以上続いたものが3種類以上あったら中断（ぐるぐる対策）
            if data:
                if data in last_responses:
                    last_responses[data] += 1
                    # 5回以上続いたレスポンスの種類をカウント（ただし5回につき1種類とカウント）
                    total_count = 0
                    over_limit_responses = {}
                    for k, v in last_responses.items():
                        if v >= same_response_count:
                            total_count += v
                            over_limit_responses[k] = v
                    if total_count >= same_response_count * same_response_words:
                        print(f"\n異常検知: {same_response_count}回以上繰り返された応答が{same_response_words}種類に達しました。")
                        for k, v in over_limit_responses.items():
                            k_str = k.replace("\n", " ")
                            print(f"  {v}回 : {k_str}")
                        stop_inference = True
                        restext = "error"
                        break
                else:
                    last_responses[data] = 1
                # 新規レスポンスを受信したら結果を蓄積していく
                restext += data
                no_response = 0
            # 推論が終了したら結果を返す
            if res and res.get('data', {}).get('finish'):
                print()
                break

    except Exception as e:
        print(f'エラー send_message_llm: {e}')
        return f"error: {str(e)}"

    # おかしくなった推論を中断する
    if stop_inference and abort:
        print()
        print("LLMの推論を中断します")
        abort_llm()
        print("LLMの推論を中断しました")

    return restext.rstrip()


# MCP Tool: TTSでテキストを再生する
def speak_text(message: str) -> str:
    """TTSでテキストを再生する (English only)
    Args:
        message (str): 再生するテキスト
    Returns:
        str: 結果
    """
    global sf_sock, tts_sequence, args
    
    if not sf_sock:
        return "Error: TTS is not initialized"
    if not args.enabletts:
        return "Error: TTS is not enabled"

    try:
        # TTS推論
        request_id = f"tts_{int(time.time())}_{tts_sequence}"
        tts_request = {
            "request_id": request_id,
            "work_id": tts_work_id,
            "action": "inference",
            "object": "tts.utf-8.stream",
            "data": {
                "delta": message,
                "index": 0,
                "finish": True
            }
        }
        send_json_request(sf_sock, tts_request)
        res = receive_response(sf_sock, 3)     # なぜか応答が来ないので3秒でタイムアウトさせる
        if res is None:
            print("レスポンスがタイムアウトしましたが、処理を続行します")
            
    except Exception as e:
        print(f'エラー: {e}')
        return f"error: {str(e)}"

    return "success"


def create_gradio_interface():
    global running_model, args
    with gr.Blocks(title="Module-LLM MCP Server") as demo:
        gr.Markdown("# Module-LLM MCP Server")
        gr.Markdown(f"Model: {running_model}")
        
        # LLMにメッセージ送信
        gr.Markdown("## LLMにメッセージ送信")
        msg_input = gr.Textbox(label="メッセージ")
        msg_output = gr.Textbox(label="応答")
        timeout_input = gr.Number(label="タイムアウト", value=30, precision=0)
        msg_button = gr.Button("送信")
        msg_button.click(fn=send_message, inputs=[msg_input, timeout_input], outputs=msg_output)
        gr.Markdown("<br>")
        gr.Markdown("---")
        gr.Markdown("<br>")

        # 使用可能なモデルの一覧を取得
        gr.Markdown("## 使用可能なモデルの一覧を取得")
        model_list_button = gr.Button("モデル一覧を取得")
        model_list_output = gr.Textbox(label="モデル一覧")
        model_list_button.click(fn=get_model_list, inputs=[], outputs=model_list_output)
        gr.Markdown("<br>")
        gr.Markdown("---")
        gr.Markdown("<br>")

        # モデルを変更
        gr.Markdown("## モデルを変更")
        model_input = gr.Textbox(label="モデル名", lines=1)
        model_button = gr.Button("変更")
        model_output = gr.Textbox(label="応答")
        model_button.click(fn=change_model, inputs=[model_input], outputs=model_output)
        gr.Markdown("<br>")
        gr.Markdown("---")
        gr.Markdown("<br>")

        # TTSでテキスト読み上げ
        if args.enabletts:
            gr.Markdown("## TTSでテキスト読み上げ")
            speak_input = gr.Textbox(label="読み上げるテキスト")
            speak_output = gr.Textbox(label="結果")
            speak_button = gr.Button("読み上げる")
            speak_button.click(fn=speak_text, inputs=speak_input, outputs=speak_output)
            gr.Markdown("<br>")
            gr.Markdown("---")
            gr.Markdown("<br>")

        # ページ更新
        gr.Markdown("## ページ更新")
        reload_button = gr.Button("ページ更新")
        reload_button.click(js="window.location.reload()")
        gr.Markdown("<br>")
        gr.Markdown("---")
        gr.Markdown("<br>")
    return demo


def main():
    global running_model, args, quiet
    parser = argparse.ArgumentParser(description='LLM MCP Server')
    parser.add_argument('--host', type=str, default='127.0.0.1', help='LLM server hostname')
    parser.add_argument('--port', type=int, default=10001, help='LLM server port')
    parser.add_argument('--llmmodel', type=str, default='qwen2.5-0.5B-prefill-20e', help='LLM Model')
    parser.add_argument('--ttsmodel', type=str, default='melotts_zh-cn', help='TTS Model')
    parser.add_argument('--linktts', action='store_true', help='LLM output to TTS')
    parser.add_argument('--enabletts', action='store_true', help='Enable to TTS')
    parser.add_argument('--listmodels', action='store_true', help='Show installed model')
    parser.add_argument('--quiet', action='store_true', help='no output JSON log')
    args = parser.parse_args()
    running_model = args.llmmodel
    quiet = args.quiet

    # インストールされているモデルの一覧を表示して終了
    if args.listmodels:
        if connect_server(args.host, args.port):
            quiet = True
            models = get_model_list(long=True)
            print(models)
            disconnect_server()
            sys.exit()

    # MCPサーバー起動
    try:
        # StackFlowサーバーに接続
        print(f"Connecting to StackFlow server: {args.host}:{args.port}")
        if connect_server(args.host, args.port):
            print("Connected to StackFlow server")
        else:
            raise Exception("Cannot connect to StackFlow server")

        # LLMの初期化
        print(f"Initializing LLM with model: {args.llmmodel}")
        if setup_llm(args.llmmodel):    # LLMのセットアップ
            print("LLM initialization completed")
        else:
            raise Exception("LLM initialization failed")

        # TTSの初期化
        if args.enabletts:
            print(f"Initializing TTS with model: {args.ttsmodel}")
            if setup_tts(args.ttsmodel):
                print("TTS initialization completed")
            else:
                raise Exception("TTS initialization failed")

        # LLMの出力をTTSの入力にリンクする
        if args.linktts:
            print("Linking LLM to TTS")
            if setup_link_llmtts():
                print("LLM/TTS linking completed")
            else:
                raise Exception("LLM/TTS linking failed")

        # MCPサーバーの起動
        print("Starting gradio MCP server...")
        mcpgr = create_gradio_interface()
        mcpgr.launch(
            mcp_server=True, 
            server_name="0.0.0.0", 
            server_port=7860
        )
                    
    except KeyboardInterrupt:
        print("\nShutting down...")
    except Exception as e:
        print(f"予期せぬエラーが発生しました: {e}")
        traceback.print_exc()  # スタックトレース出力
    finally:
        print("Cleaning up...")
        exit_session()  # StackFlowサーバーとのセッションを終了


if __name__ == "__main__":
    main()
