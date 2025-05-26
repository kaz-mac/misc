#!/bin/bash

models=""
models="${models} llama-3.2-1b-instruct"
models="${models} sakanaai.tinyswallow-1.5b"
models="${models} sakanaai.evollm-jp-v1-7b"
models="${models} openbuddy-llama3.2-1b-v23.1-131k-i1"
models="${models} openbuddy-llama3.2-3b-v23.2-131k-i1"
models="${models} elyza-japanese-llama-2-7b-instruct"
#models="${models} cyberagent-deepseek-r1-distill-qwen-14b-japanese"
#models="${models} cyberagent-deepseek-r1-distill-qwen-32b-japanese"
#models="${models} qwen3-30b-a3b"

datasets="jcommonsenseqa mawps mgsm jaqket"
#datasets="jaqket"

bench () {
  model=$1
  dataset=$2
  timeout=$3

  offset=0 
  testnum=100

  server=lmstudio
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
  for dataset in ${datasets}; do
    echo
    echo "## Dataset: ${dataset}"
    bench ${model} ${dataset} ${timeout}
  done
done
