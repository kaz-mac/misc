#!/bin/bash

models=""
models="${models} qwen2.5-0.5B-prefill-20e"
models="${models} qwen2.5-0.5B-Int4-ax630c"
models="${models} qwen2.5-0.5B-p256-ax630c"
models="${models} qwen3-0.6B-ax630c"
# 'ERROR' models="${models} qwen2.5-1.5B-Int4-ax630c"
models="${models} qwen2.5-1.5B-ax630c"
models="${models} qwen2.5-1.5B-p256-ax630c"
# 'SKIP' models="${models} qwen2.5-coder-0.5B-ax630c"
models="${models} openbuddy-llama3.2-1B-ax630c"
models="${models} llama3.2-1B-p256-ax630c"
models="${models} llama3.2-1B-prefill-ax630c"
models="${models} deepseek-r1-1.5B-ax630c"
models="${models} deepseek-r1-1.5B-p256-ax630c"

#datasets=jcommonsenseqa
#datasets=mawps
datasets="jcommonsenseqa mawps mgsm"
#datasets=jaqket

bench () {
  model=$1
  dataset=$2
  timeout=$3

  offset=0 
  testnum=100

  # if [ "${model}" == "deepseek-r1-1.5B-p256-ax630c" ]; then
  #   timeout=180
  # fi

  server=modulellm
  savedir="result/${dataset}"
  mkdir -p ${savedir}
  uv run benchmark.py \
    --server ${server} \
    --timeout ${timeout} \
    --interval 0 \
    --model ${model} \
    --dataset test/test_${dataset}.json \
    --out ${savedir}/${server}_${model}.csv \
    --offset ${offset} \
    --testnum ${testnum}
}

for model in ${models}; do
  timeout=180
  echo
  echo "## Model: ${model}"
  uv run ./change_model.py --model "${model}"
  if [ $? -ne 0 ]; then
    echo "モデルの切り替えに失敗しました" >&2
    exit 1
  fi

  for dataset in ${datasets}; do
    echo
    echo "## Dataset: ${dataset}"
    bench ${model} ${dataset} ${timeout}
  done
done
