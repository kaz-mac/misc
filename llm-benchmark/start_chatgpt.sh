#!/bin/bash

models=""
# 'ERROR' models="${models} o4-mini"
models="${models} gpt-4o-mini"

#datasets="jcommonsenseqa"
datasets="jcommonsenseqa mawps mgsm jaqket"
#datasets="jaqket"

bench () {
  model=$1
  dataset=$2
  timeout=$3

  offset=0 
  testnum=100

  server=chatgpt
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
