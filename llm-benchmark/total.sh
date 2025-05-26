#!/bin/bash
csvdir=result
scoreddir=scored

subdirs="jcommonsenseqa mawps mgsm jaqket"
#subdirs="jaqket"

servers="lmstudio modulellm chatgpt"
#servers="lmstudio"

mkdir -p ${scoreddir}
echo -n > ${scoreddir}/total.csv

for sdir in ${subdirs} ; do
  # 採点と集計
  mkdir -p ${scoreddir}/${sdir}
  ./totalling.py \
    --csvdir ${csvdir}/${sdir} \
    --scoreddir ${scoreddir}/${sdir} \
    --outfile ${scoreddir}/total_${sdir}.csv
  cat ${scoreddir}/total_${sdir}.csv >> ${scoreddir}/total.csv
  echo >> ${scoreddir}/total.csv

  # 全ての解答一覧を結合（目視デバッグ用）
  find ${scoreddir}/${sdir} \
    -type f -name "*.csv" \
    -exec awk '{print FILENAME "," $0}' {} \; \
    > ${scoreddir}/all_${sdir}.csv
  sed -i '1s/^/\xEF\xBB\xBF/' ${scoreddir}/all_${sdir}.csv

  # グラフ作成
  for server in ${servers} ; do
    python plot_graph.py \
      --csvfile ${scoreddir}/total_${sdir}.csv \
      --server ${server} \
      --title "Test: ${sdir} , Server: ${server}" \
      --outfile ${scoreddir}/graph_${sdir}_${server}.png
  done
done


