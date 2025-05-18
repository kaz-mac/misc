# これは何？
Module-LLMのAPI/MCPサーバーです。

単にベンチマークのAPIとしてgradioが便利だったので、MCPサーバー機能はおまけです。
MCPサーバーの動作は未確認です。

# mcp_llm_gradio.py の使い方

## コマンドラインオプション

| オプション | 説明 | デフォルト値 |
|------------|------|--------------|
| `--host` | LLMサーバーのホスト名 | 127.0.0.1 |
| `--port` | LLMサーバーのポート番号 | 10001 |
| `--llmmodel` | 使用するLLMモデル名 | qwen2.5-0.5B-prefill-20e |
| `--ttsmodel` | 使用するTTSモデル名 | melotts_zh-cn |
| `--linktts` | LLMの出力をTTSの入力にリンクする | オフ |
| `--enabletts` | TTS機能を有効にする | オフ |
| `--listmodels` | インストールされているモデルの一覧を表示して終了 | オフ |
| `--quiet` | JSONログの出力を抑制する | オフ |

## 使用例
Module-LLM上で実行することを想定。--host, --portを指定すればリモートからでもいけるはず。（試してない）

1. 基本的な使用方法:
```bash
uv run mcp_llm_gradio.py
```

2. 特定のLLMモデルを指定して起動:
```bash
uv run mcp_llm_gradio.py --llmmodel モデル名
```

3. 特定のTTSモデルを指定して、TTS機能を有効にして起動:（LLMは未指定でも起動する）
```bash
uv run mcp_llm_gradio.py --enabletts --ttsmodel モデル名 
```

4. LLMとTTSをリンクさせて起動:（日本語対応の例）
```bash
uv run mcp_llm_gradio.py \
  --llmmodel qwen2.5-0.5B-prefill-20e \
  --enabletts \
  --ttsmodel melotts-ja-jp \
  --linktts
```

5. インストール済みのモデル一覧を出力:
```bash
uv run mcp_llm_gradio.py --listmodels
```
出力例
```text
asr: sherpa-ncnn-streaming-zipformer-20M-2023-02-17
asr: sherpa-ncnn-streaming-zipformer-zh-14M-2023-02-23
cv: yolo11n
cv: yolo11n-pose
cv: yolo11s-seg
kws: sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01
kws: sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01
llm: deepseek-r1-1.5B-ax630c
llm: deepseek-r1-1.5B-p256-ax630c
llm: llama3.2-1B-p256-ax630c
llm: llama3.2-1B-prefill-ax630c
llm: openbuddy-llama3.2-1B-ax630c
llm: qwen2.5-0.5B-Int4-ax630c
llm: qwen2.5-0.5B-p256-ax630c
llm: qwen2.5-0.5B-prefill-20e
llm: qwen2.5-1.5B-Int4-ax630c
llm: qwen2.5-1.5B-ax630c
llm: qwen2.5-1.5B-p256-ax630c
llm: qwen2.5-coder-0.5B-ax630c
llm: qwen3-0.6B-ax630c
tts: melotts-ja-jp
tts: melotts_zh-cn
tts: single_speaker_english_fast
tts: single_speaker_fast
```

6. 終了方法:
```bash
CTRL + C
```

## ブラウザからアクセスする方法
`http://IPアドレス:7860/`

## MCPクライアントからアクセスする方法
`http://IPアドレス:7860/gradio_api/mcp/sse`

