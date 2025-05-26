#!/usr/bin/env python
# -*- coding: utf-8 -*-

# LLMベンチマーク結果の集計
#   LLMベンチマークテストの各結果を集計してCSVファイルに出力する
#
# Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
# Released under the MIT license.

import os
import sys
import argparse
import csv
import glob
import re
from pathlib import Path

# Windows環境での文字化けを防ぐためにUTF-8に設定
if sys.platform.startswith('win'):
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')

# 標準出力のバッファリングを無効化
sys.stdout.reconfigure(line_buffering=True) if hasattr(sys.stdout, 'reconfigure') else None


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


def extract_answer_number(response_text: str) -> tuple[str, str]:
    """
    LLMの解答の自然文から数字（整数または小数）を抽出する
    
    Args:
        response_text (str): LLMからの解答
        
    Returns:
        tuple: (抽出された数字（見つからない場合は空文字列）, 思考過程を除いたテキスト)
    """
    answer = ""
    wo_thinking_text = ""

    # 空の解答の場合は空文字列を返す
    if not response_text or response_text.strip() == "":
        return "", ""

    # 全角から半角への変換
    trans_table = str.maketrans("０１２３４５６７８９＝　", "0123456789= ")
    response_text = response_text.translate(trans_table)
    response_text = response_text.replace(',', '')

    # <think>～</think>タグ内のテキストをすべて削除
    response_text = re.sub(r'<think>.*?</think>', '', response_text, flags=re.DOTALL|re.IGNORECASE)
    response_text = re.sub(r'^.*</think>', '', response_text, flags=re.DOTALL|re.IGNORECASE)
    response_text = re.sub(r'<.*?>', '', response_text, flags=re.DOTALL)

    # 「答えはxxです」の場合、xxを抽出
    if not answer:
        choice_match = re.search(r'答えは\s*([\d\-\.]+)\s*(です)*', response_text)
        if choice_match:
            answer = choice_match.group(1)

    # 「答え: xx」の場合、xxを抽出
    if not answer:
        choice_match = re.search(r'(答え|解答|正解)は*\s*:*\s*([\d\-\.]+)', response_text)
        if choice_match:
            answer = choice_match.group(2)

    # 「The answer is: xx」の場合、xxを抽出
    if not answer:
        choice_match = re.search(r'answer is\s*:*\s*([\d\-\.]+)', response_text, re.IGNORECASE)
        if choice_match:
            answer = choice_match.group(1)

    # 選択肢の場合（例: (4)森）、最後の括弧内の数字を抽出
    if not answer:
        choice_matches = re.findall(r'\((\d+)\)', response_text)
        if choice_matches:
            answer = choice_matches[-1]  # 最後にマッチした括弧内の数字を使用
    
    # 計算式の場合（例: 1+1=2.5=2.5）、最後の等号の後の数字（小数点含む）を抽出
    if not answer:
        equals_match = re.search(r'=\s*(\d+\.\d+|\d+)(?![^=]*=)', response_text)
        if equals_match:
            answer = equals_match.group(1)
    
    # 単純に数字だけの場合（例: 4 または 3.14）- 後方一致
    if not answer:
        number_matches = re.findall(r'(\-?\d+\.\d+|\-?\d+)', response_text)
        if number_matches:
            answer = number_matches[-1]  # 最後にマッチした数値を使用
    
    # 小数点と間違えられた末尾の.や前後の余計なスペースを削除
    answer = answer.rstrip('.')
    answer = answer.strip()

    return answer, response_text


def compare_result_vs_answer(response_num: str, answer_num: str) -> bool:
    """
    解答と応答を比較して一致するかどうかを返す

    Args:
        response_num (str): 解答
        answer_num (str): 正解
        
    Returns:
        bool: 解答と応答が一致するかどうか
    """
    match = False

    # 正解がfloat型の場合、精度を考慮して ±1% の範囲に収まっていればよしとする
    if re.match(r'\d+\.\d+', answer_num):
        try:
            answer_float = float(answer_num)
            response_float = float(response_num)
            # 誤差1%以内かチェック
            tolerance = abs(answer_float * 0.01)  # 1%の許容範囲
            match = abs(answer_float - response_float) <= tolerance
        except ValueError:
            match = False
    # 小数点を含まない場合は文字列として厳密に比較
    else:
        match = answer_num == response_num

    return match


def scoring_csv_file(file_path: str) -> tuple[list, dict]:
    """CSVファイルを読み込んで採点する

    Args:
        file_path (str): CSVファイルのパス
        
    Returns:
        tuple: (採点結果(list), 備考情報(dict))
    """
    results = []
    condition = {
        'server': '',
        'model': '',
        'timeout': 9999999,
        'av_response_time': 0,
        'point_pass': 0,
        'point_count': 0,
        'total_count': 0,
        'llm_parameter': ''
    }
    valid_response_times = []
    
    # データを読み込む
    with open(file_path, 'r', encoding='utf-8') as f:
        reader = csv.reader(f)
        for row in reader:
            # 問題と解答の行
            if len(row) >= 6 and row[0].isdigit():
                no = int(row[0]) if row[0].isdigit() else 0
                result = {
                    'no': no,
                    'question': row[1],
                    'answer_raw': row[2],
                    'answer_num': row[3],
                    'response_raw': row[4],
                    'response_time': int(row[5]) if row[5].isdigit() else 0,
                }
                results.append(result)

            # 備考情報の行
            if len(row) >= 3 and row[0] == '#':
                if row[1] == 'server':
                    condition['server'] = row[2]
                elif row[1] == 'model':
                    condition['model'] = row[2]
                elif row[1] == 'timeout' and row[2].isdigit():
                    condition['timeout'] = int(row[2])

            # サーバー名、テスト名が不明な場合はファイル名から取得する file_path=xxx/yyy/server_model.csv
            if not condition['server'] or not condition['model']:
                file_name = os.path.basename(file_path)
                if '_' in file_name:
                    # 最後の_で分割して、最初の部分をserver、残りをmodelとする
                    parts = file_name.rsplit('_', 1)
                    if len(parts) == 2:
                        condition['server'] = parts[0]
                        # 拡張子のみを削除（最後の.以降を削除）
                        condition['model'] = re.sub(r'\.[^.]*$', '', parts[1])

    # 採点を行う
    total_count = 0
    point_pass = 0
    point_count = 0
    results_new = []
    for result in results:
        total_count += 1
        result['valid'] = True

        # タイムアウトした結果は採点しない（タイムアウト設定値95%以上）
        if result['response_time'] > condition['timeout'] * 0.95 or result['response_time'] == 0:
            result['valid'] = False
        if result['response_raw'] == 'error':
            result['valid'] = False

        # 採点
        if result['valid']:
            point_count += 1
            # 自然文の解答の中から解答の数字(文字列型)を抽出
            result['response_num'], wo_thinking = extract_answer_number(result['response_raw'])

            # 解答と正解を比較
            result['match'] = compare_result_vs_answer(result['response_num'], result['answer_num'])
            if result['match']:
                point_pass += 1

            # 解答の中に数字が出現した数をカウント（異常な結果の判断用）
            result['number_count'] = len(re.findall(r'-?\d+(?:\.\d+)?', wo_thinking))

            # 応答時間を集計
            valid_response_times.append(result['response_time'])
        else:
            result['response_num'] = ''
            result['match'] = False
            result['number_count'] = 0

        # 結果をまとめる
        results_new.append(result)
        condition['point_pass'] = point_pass
        condition['point_count'] = point_count
        condition['total_count'] = total_count

    # 応答時間の平均を計算
    condition['av_response_time'] = 0
    if valid_response_times and len(valid_response_times) > 0:
        condition['av_response_time'] = sum(valid_response_times) / len(valid_response_times)
    
    # ファイル名からパラメーター数を推測する
    condition['llm_parameter'] = ''
    file_name = os.path.basename(file_path)
    param_match = re.search(r'\-([\.\d]+B-A[\.\d]+B)', file_name.upper())
    if param_match:
        condition['llm_parameter'] = param_match.group(1)
    else:
        param_match = re.search(r'\-([\.\d]+B)', file_name.upper())
        if param_match:
            condition['llm_parameter'] = param_match.group(1)

    return results_new, condition


def save_csv_file(file_path: str, results: list, condition: dict) -> None:
    """採点結果をCSVファイルに保存する

    Args:
        file_path (str): CSVファイルのパス
        results (dict): 採点結果
        condition (dict): 採点条件
    """

    with open(file_path, 'w', encoding='utf-8-sig', newline='') as f:
        writer = csv.writer(f)
        
        # ヘッダー行を書き込み
        writer.writerow(['問題番号', '問題', '正解(raw)', '正解(num)', '解答(raw)', '応答時間(ms)', 
                         '有効解答', '解答(num)', '採点結果', '採点結果', '数値出現回数'])
        
        # 各問題の結果を書き込み
        for result in results:
            writer.writerow([
                result['no'],
                result['question'],
                result['answer_raw'],
                result['answer_num'],
                result['response_raw'],
                result['response_time'],
                1 if result['valid'] else 0,
                result['response_num'],
                1 if result['match'] else 0,
                ("正解" if result['match'] else "不正解") if result['valid'] else "error",
                result['number_count']
            ])
        
        # 条件情報を書き込み
        writer.writerow([])
        writer.writerow(['#', 'server', condition['server']])
        writer.writerow(['#', 'model', condition['model']])
        writer.writerow(['#', 'timeout', condition['timeout']])
        writer.writerow(['#', 'av_response_time', round(condition['av_response_time'])])
        writer.writerow(['#', 'point_count', condition['point_count']])
        writer.writerow(['#', 'point_pass', condition['point_pass']])


if __name__ == "__main__":
    """メイン関数"""

    # コマンドライン引数の解析
    parser = argparse.ArgumentParser(description='LLMベンチマークテスト結果の集計')
    parser.add_argument('--csvdir', 
                        type=str,
                        required=True,
                        help='テスト結果があるディレクトリ')
    parser.add_argument('--scoreddir', 
                        type=str,
                        required=True,
                        help='採点結果を保存するディレクトリ')
    parser.add_argument('--outfile',
                        type=str,
                        required=True,
                        help='出力する集計結果のファイル名')
    args = parser.parse_args()

    # ディレクトリの存在チェック
    if not os.path.exists(args.csvdir):
        print(f"エラー: テスト結果のディレクトリ '{args.csvdir}' が見つかりません")
        sys.exit(1)
    if not os.path.exists(args.scoreddir):
        print(f"エラー: 採点結果の保存先のディレクトリ '{args.scoreddir}' が見つかりません")
        sys.exit(1)

    # 集計結果ファイルが存在したらエラーで終了
    # if os.path.exists(args.outfile):
    #     print(f"エラー: 集計結果ファイル '{args.outfile}' が存在します")
    #     sys.exit(1)
    
    # 読み込むCSVファイルの検索
    csv_files = glob.glob(os.path.join(args.csvdir, '*.csv'))
    if not csv_files:
        print(f"エラー: '{args.csvdir}' にCSVファイルが見つかりません")
        sys.exit(1)

    # 各CSVファイルを処理
    all_results = []
    for in_csv_file in csv_files:
        # ベンチマーク結果を読み込んで採点を行う
        results, condition = scoring_csv_file(in_csv_file)

        # 採点結果を保存する
        if results:
            out_csv_file = os.path.join(args.scoreddir, os.path.basename(in_csv_file))
            save_csv_file(out_csv_file, results, condition)

        # 採点結果を集計する
        if results:
            accuracy = condition['point_pass'] / condition['point_count'] * 100 if condition['point_count'] > 0 else 0
            all_result = {
                'test_name': os.path.basename(os.path.dirname(in_csv_file)),
                'server': condition['server'],
                'model': condition['model'],
                'accuracy': accuracy,
                'point_pass': condition['point_pass'],
                'point_count': condition['point_count'],
                'total_count': condition['total_count'],
                'invalid_count': condition['total_count'] - condition['point_count'],
                'av_response_time': condition['av_response_time'],
                'llm_parameter': condition['llm_parameter']
            }
            all_results.append(all_result)

    # 集計結果をCSVファイルに出力
    with open(args.outfile, 'w', encoding='utf-8-sig', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['テスト名', 'サーバー', 'パラメーター数', 'モデル', '正解率', 
                         '正解数', '有効解答数', '異常回答数', '合計問題数', '平均応答時間'])
        for all_result in all_results:
            writer.writerow([
                all_result['test_name'],
                all_result['server'],
                all_result['llm_parameter'],
                all_result['model'],
                f"{all_result['accuracy']:.2f}%",
                all_result['point_pass'],
                all_result['point_count'],
                all_result['invalid_count'],
                all_result['total_count'],
                round(all_result.get('av_response_time', 0)),
            ])

    # 終了
    print(f"集計結果を {args.outfile} に保存しました")

