import csv
import sys
import json
#
# JAQKETのテストデータを、日本語Chain-of-Thoughtデータセットの形式に変換する
#
# JAQKET: https://www.nlp.ecei.tohoku.ac.jp/projects/jaqket/
# 日本語Chain-of-Thoughtデータセット: https://github.com/t-nakabayashi/chain-of-thought-ja-benchmark
#
# 使用例: convert_test_jaqket.py dev1_questions.json test_jaqket.json

# Windows環境での文字化けを防ぐためにUTF-8に設定
if sys.platform.startswith('win'):
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')


def convert_test(input_file, output_file):
    converted_data = []
    
    with open(input_file, 'r', encoding='utf-8') as infile:
        for idx, line in enumerate(infile):                
            data = json.loads(line.strip())
            
            # 問題文の作成
            question = f"問題：{data['question']}\n"
            for i, candidate in enumerate(data['answer_candidates']):
                question += f"({i}) {candidate}\n"
            question += "解答："
            
            # 正解のインデックスを取得
            answer_idx = str(data['answer_candidates'].index(data['answer_entity']))
            
            # 変換後のデータを作成
            converted_item = {
                "idx": idx,
                "question": question,
                "answer": answer_idx
            }
            
            converted_data.append(converted_item)
    
    # JSONファイルとして出力
    with open(output_file, 'w', encoding='utf-8') as outfile:
        json.dump(converted_data, outfile, ensure_ascii=False, indent=4)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('使用方法: python convert_test_jaqket.py 入力ファイル 問題数 出力ファイル')
        sys.exit(1)
        
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    try:
        convert_test(input_file, output_file)
        print(f'変換が完了しました。出力ファイル: {output_file}')
    except Exception as e:
        print(f'エラーが発生しました: {str(e)}')
        sys.exit(1) 