#!/usr/bin/env python
# -*- coding: utf-8 -*-

# LLMベンチマークテスト
#   LLMのAPI(gradio)にアクセスして結果CSVファイルに出力する
#
# Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
# Released under the MIT license.

# 使用データセット
# ・日本語Chain-of-Thoughtデータセット (jcommonsenseqa, mawps, mgsm)
#     https://github.com/t-nakabayashi/chain-of-thought-ja-benchmark
# ・JAQKET
#     https://www.nlp.ecei.tohoku.ac.jp/projects/jaqket/ （要変換）

# メモ
# OpenAI API料金 https://openai.com/ja-JP/api/pricing/?utm_source=chatgpt.com

import time
import sys
import os
import requests
import json
import argparse
import re
import csv
from gradio_client import Client

# グローバル変数
modulellm_api_url = "http://192.168.xx.xx:7860/"
modulellm_api_name = "/send_message"
chatgpt_api_url = "https://api.openai.com/v1/chat/completions"
lmstudio_api_url = "http://127.0.0.1:1234/v1/chat/completions"
model = ""
interval_chatgpt = 1000     # ChatGPT APIの場合のAPIコール間隔(ms)

# Windows環境での文字化けを防ぐためにUTF-8に設定
if sys.platform.startswith('win'):
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')

# 標準出力のバッファリングを無効化
sys.stdout.reconfigure(line_buffering=True) if hasattr(sys.stdout, 'reconfigure') else None


def test_modulellm_api(message: str, timeout: int=60) -> tuple[str, int]:
    """
    Module-LLMにメッセージを送信し、応答を取得する
    
    Args:
        message (str): LLMに送信するメッセージ
        
    Returns:
        tuple: (応答内容(str), 応答時間(ms単位の整数))
               エラー時は ("", 0) を返す
    """
    try:
        # APIクライアント(gradio)の初期化
        client = Client(modulellm_api_url)
        
        # APIにリクエスト送信
        start_time = time.time()
        result = client.predict(
            message=message,
            timeout=timeout,
            api_name=modulellm_api_name
        )
        
        # 応答時間を計算（ミリ秒単位）
        response_time_ms = int((time.time() - start_time) * 1000)
        return result, response_time_ms

    except Exception as e:
        print(f"エラーが発生しました: {e}")
        return "", 0


def test_chatgpt_api(message: str, timeout: int=60) -> tuple[str, int]:
    """
ChatGPT    
    Args:
        message (str): ChatGPTに送信するメッセージ
        
    Returns:
        tuple: (応答内容(str), 応答時間(ms単位の整数))
               エラー時は ("", 0) を返す
    """
    try:
        # APIキーの取得（環境変数からのみ）
        api_key = os.environ.get("OPENAI_API_KEY")
        
        # リクエストデータの作成
        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}"
        }
        data = {
            "model": model,
            "messages": [
                {"role": "user", "content": message}
            ],
            "temperature": 0.7
        }
        
        # APIにリクエスト送信
        start_time = time.time()
        response = requests.post(chatgpt_api_url, headers=headers, data=json.dumps(data))
        response.raise_for_status()  # エラーがあれば例外を発生させる
        
        # レスポンスを解析
        response_data = response.json()
        result = response_data["choices"][0]["message"]["content"]
        
        # 応答時間を計算（ミリ秒単位）
        response_time_ms = int((time.time() - start_time) * 1000)
        return result, response_time_ms

    except Exception as e:
        print(f"エラーが発生しました: {e}")
        return "", 0


def test_lmstudio_api(message: str, timeout: int=60) -> tuple[str, int]:
    """
    LM Studioにメッセージを送信し、応答を取得する
    
    Args:
        message (str): LM Studioに送信するメッセージ
        
    Returns:
        tuple: (応答内容(str), 応答時間(ms単位の整数))
               エラー時は ("", 0) を返す
    """
    try:
        # リクエストデータの作成
        start_time = time.time()
        headers = {
            "Content-Type": "application/json"
        }
        data = {
            "model": model,
            "messages": [
                {"role": "user", "content": message}
            ],
            "temperature": 0.7
        }
        
        # APIにリクエスト送信
        response = requests.post(lmstudio_api_url, headers=headers, data=json.dumps(data))
        response.raise_for_status()  # エラーがあれば例外を発生させる
        
        # レスポンスを解析
        response_data = response.json()
        result = response_data["choices"][0]["message"]["content"]
        
        # 応答時間を計算（ミリ秒単位）
        response_time_ms = int((time.time() - start_time) * 1000)
        return result, response_time_ms

    except Exception as e:
        print(f"エラーが発生しました: {e}")
        return "", 0


def test_api(server: str, message: str, timeout: int=60) -> tuple[str, int]:
    """
    サーバーを指定してAPIテストを実行する統合関数
    
    Args:
        server (str): APIサーバー名（'modulellm'または'chatgpt'または'lmstudio'など）
        message (str): 送信するメッセージ
        
    Returns:
        tuple: response(応答内容), response_time(応答時間)
               エラー時は ("", 0) を返す
    """
    # サーバー名に基づいて適切なAPI関数を呼び出す
    if server == 'modulellm':
        response, response_time = test_modulellm_api(message, timeout)
    elif server == 'chatgpt':
        response, response_time = test_chatgpt_api(message, timeout)
    elif server == 'lmstudio':
        response, response_time = test_lmstudio_api(message, timeout)
    else:
        print(f"未知のサーバー: {server}")
        return "", 0, server
    
    return response, response_time


def test_question(server: str, question: str, timeout: int=60) -> tuple[str, int]:
    """
    指定したサーバーで質問をテストし、解答を得る
    
    Args:
        server (str): APIサーバー名
        question (str): 質問文
        timeout (int): タイムアウト時間（秒）
        
    Returns:
        tuple: response(応答内容), response_time(応答時間)
               エラー時は ("", 0) を返す
    """
    # APIにリクエストを送信
    message = question + "\n数値のみで解答しててください。思考過程や考察は含めないでください。\n"
    raw_response, response_time = test_api(server, message, timeout)
            
    return raw_response, response_time


def load_dataset(file_path):
    """
    テストデータセットをJSONファイルから読み込む
    
    Args:
        file_path (str): JSONファイルのパス
        
    Returns:
        list: 問題と解答データのリスト
    """
    print(f"データセット読み込み中: {file_path}")
    questions = []
    no = 1
    
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            try:
                # JSONファイル全体を読み込む（配列形式を想定）
                data_array = json.load(f)
                
                # 配列形式の場合の処理
                if isinstance(data_array, list):
                    for data in data_array:
                        # 問題文の最後の「解答：」を削除
                        if 'question' in data and 'answer' in data:
                            data['question'] = re.sub(r'解答[:|：][\r\n]*$', '', data['question'])
                        data['question'] = re.sub(r'[\r\n]+$', '', data['question'])
                        # 正解の前処理 - 選択肢表記("(2)マザーボード"など)から数字だけを抽出
                        answer_num = ""
                        if isinstance(data['answer'], str):
                            choice_match = re.search(r'\((\d+)\)', data['answer'])
                            if choice_match:
                                answer_num = choice_match.group(1)
                            else:
                                choice_match = re.search(r'(-?\d+(?:\.\d+)?)', data['answer'])
                                if choice_match:
                                    answer_num = choice_match.group(1)
                        # answer_num = re.sub(r'[^\-\.\d]', '', answer_num)
                        # 辞書に代入
                        questions.append({
                            'no': no,
                            'question': data['question'],
                            'answer_raw': data['answer'],
                            'answer_num': answer_num
                        })
                        no += 1

            except json.JSONDecodeError:
                print(f"エラー: 配列形式のJSONとして解析できませんでした。")

    except Exception as e:
        print(f"データセットの読み込み中にエラーが発生しました: {e}")
        sys.exit(1)
        
    print(f"データセットから {len(questions)} 問の問題を読み込みました")
    return questions


def crlf2space(text: str, convert: str=" ") -> str:
    """
    改行コードをスペースに置換する

    Args:
        text (str): 改行コードを置換するテキスト
        convert (str): 置換する文字（デフォルト: " "）
        
    Returns:
        str: 改行コードが置換されたテキスト
    """
    return text.replace('\r\n', convert).replace('\r', convert).replace('\n', convert)


def save_results_to_csv(results, file_path, use_bom=True, offset=0):
    """
    テスト結果をCSVファイルに保存する
    
    Args:
        results (list): テスト結果のリスト
        file_path (str): 保存先のCSVファイルパス
        use_bom (bool): BOMを使用するかどうか（デフォルト: True）
        offset (int): 開始問題のオフセット
    """
    global model
    print(f"結果を保存中: {file_path}")

    try:
        # エンコーディングの設定（BOMの有無）
        encoding = 'utf-8-sig' if use_bom else 'utf-8'
        
        with open(file_path, 'w', encoding=encoding, newline='') as f:
            writer = csv.writer(f, quoting=csv.QUOTE_MINIMAL)
            
            # ヘッダー行を書き込み
            writer.writerow(['問題番号', '問題', '正解(raw)', '正解(num)', '解答(raw)', '応答時間(ms)'])
            
            # 各テスト結果を書き込み
            for i, result in enumerate(results, 1):
                # 改行コードをタグに置換
                question = crlf2space(result['question'], "<BR>")
                raw_response = crlf2space(result['raw_response'], "<BR>")

                # CSVに出力
                writer.writerow([
                    result['no'],
                    question,
                    result['answer_raw'],
                    result['answer_num'],
                    raw_response,
                    result['response_time'],
                ])

            # 備考情報を追記
            writer.writerow(["#", "server", args.server])
            writer.writerow(["#", "model", model])
            writer.writerow(["#", "timeout", args.timeout * 1000])

        print(f"テスト結果を {file_path} に保存しました")

    except Exception as e:
        print(f"結果の保存中にエラーが発生しました: {e}")

def parse_arguments():
    """コマンドライン引数を解析する"""
    parser = argparse.ArgumentParser(description='LLMベンチマークテスト')
    parser.add_argument('--server', 
                        choices=['modulellm', 'chatgpt', 'lmstudio'],
                        help='テストするサーバー（modulellm または lmstudio など')
    parser.add_argument('--timeout',
                       type=int,
                       default=60,
                       help='APIのタイムアウト時間（デフォルト: 60秒）')
    parser.add_argument('--model',
                         type=str,
                         default="",
                         help='使用するモデル（gpt-3.5-turboなど）')
    parser.add_argument('--dataset',
                       type=str,
                       help='テストデータセットを含むJSONファイルのパス')
    parser.add_argument('--testnum',
                       type=int,
                       default=None,
                       help='実行するテスト問題の数（指定しない場合は全問題）')
    parser.add_argument('--offset',
                       type=int,
                       default=0,
                       help='テストを開始する問題の位置（0から始まる、デフォルト: 0）')
    parser.add_argument('--nobom',
                       action='store_true',
                       help='CSVファイルにBOM（Byte Order Mark）を付けない')
    parser.add_argument('--out',
                       type=str,
                       default=None,
                       help='結果を保存するCSVファイルのパス')
    parser.add_argument('--interval',
                       type=int,
                       default=1000,
                       help='API呼び出し間隔（ミリ秒単位、デフォルト: 1000）')
    return parser.parse_args()


if __name__ == "__main__":
    # コマンドライン引数の解析
    args = parse_arguments()
    model = args.model

    print("LLM APIベンチマークテスト")
    
    # APIキーのチェック (ChatGPT)
    if args.server == 'chatgpt' and 'OPENAI_API_KEY' not in os.environ:
        print("OPENAI_API_KEY が環境変数に設定されていません")
        sys.exit(1)

    # サーバーが指定されていない場合は何もしない
    if not args.server:
        print("使用方法: python benchmark.py --server [modulellm|chatgpt|lmstudio] --dataset [データセットファイル]")
        sys.exit(0)
        
    # データセットが指定されていない場合は何もしない
    if not args.dataset:
        print("使用方法: python benchmark.py --server [modulellm|chatgpt|lmstudio] --dataset [データセットファイル]")
        sys.exit(0)
    
    # CSVファイルのチェック
    if not args.out:
        print("保存先CSVファイルのパスが指定されていません")
        sys.exit(1)
    csv_filename = args.out
    if os.path.exists(csv_filename):
        print(f"エラー: 保存先CSVファイル {csv_filename} がすでに存在します")
        sys.exit(1)

    # スタート
    print(f"選択されたサーバー: {args.server}")
    if model:
        print(f"モデル: {model}")
    print(f"データセット: {args.dataset}")
    
    # テストデータセットの読み込み
    questions = load_dataset(args.dataset)
    
    # 問題数の確認
    total_questions = len(questions)
    if total_questions == 0:
        print("有効な問題がデータセットにありません")
        sys.exit(1)
    
    # オフセットの確認
    if args.offset < 0 or args.offset >= total_questions:
        print(f"エラー: 指定されたオフセット {args.offset} が範囲外です（0-{total_questions-1}）")
        sys.exit(1)
    
    # 有効なテスト数の計算
    available_questions = total_questions - args.offset
    test_count = min(args.testnum or available_questions, available_questions)
    
    print(f"オフセット: {args.offset} （{args.offset+1}番目の問題から開始）")
    print(f"テスト問題数: {test_count}/{total_questions}")
    
    # 結果を格納するリスト
    results = []
    
    # 各問題をテスト
    last_time = None
    for i, question_data in enumerate(questions[args.offset:args.offset+test_count], 1):

        # 過剰アクセスの防止（ChatGPT APIの場合）
        if args.server == 'chatgpt':
            if last_time is not None:
                elapsed = (time.time() - last_time) * interval_chatgpt
                wait_ms = args.interval - elapsed
                if wait_ms > 0:
                    time.sleep(wait_ms / 1000)
            last_time = time.time()

        # 進捗状況の表示
        current_question_index = args.offset + i
        print(f"\n=== 問題 {current_question_index}/{total_questions} (進行中: {i}/{test_count}) ===")
        print(f"サーバー: {args.server}")
        print(f"モデル: {model}")
        print(f"データセット: {args.dataset}")
        print(f"問題: {question_data['question']}")
        print(f"正解(raw): {question_data['answer_raw']}")
        print(f"正解(num): {question_data['answer_num']}")
        
        # テストAPI実行
        raw_response, response_time = test_question(
            args.server, 
            question_data['question'], 
            timeout=args.timeout
        )
        question_data['raw_response'] = raw_response
        question_data['response_time'] = response_time
        
        # 結果の表示
        question = crlf2space(question_data['question'])
        raw_response = crlf2space(question_data['raw_response'])
        print(f"解答(raw): {raw_response}")
        print(f"応答時間: {question_data['response_time']} ms")
        
        # 結果をリストに追加
        results.append(question_data)
        
        # 進捗状況の表示
        print(f"進捗: {i}/{test_count} 完了 ({i/test_count*100:.1f}%)")
    
    # 結果をCSVファイルに保存
    print(f"結果出力先: {csv_filename}")
    save_results_to_csv(results, csv_filename, not args.nobom, args.offset)
    
