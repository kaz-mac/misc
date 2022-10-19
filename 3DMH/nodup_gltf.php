#!/usr/bin/php
<?php
// glTFファイルから重複したマテリアルを削除して再割り当てを行う
// 想定: 3Dマイホームデザイナーが出力したglTFをUniVRM 0.99でインポートするのに使う
// 使い方: ./nodup_gltf.php 入力.gltf 出力.glft

// ファイル名の処理
$in_path = "";
$out_path = "";
if ($argv[1] && $argv[2]) {
    if (is_file($argv[1])) $in_path = $argv[1];
    else die("in-file not found.\n");
    if (! file_exists($argv[2])) $out_path = $argv[2];
    else die("out-file exists.\n");
} else {
    die("Usage: ${argv[0]} in-file out-file\n");
}

// JSONファイルの読み込み
$json = json_decode(file_get_contents($in_path), true);

// 重複マテリアルの抽出
$matidx = [];
$newmatno = [];
$k = 0;
foreach ($json['materials'] as $no => $mat) {
    $hash = hash("sha256", json_encode($mat));
    if (isset($matidx[$hash])) {
        $newmatno[$no] = $matidx[$hash];
    } else {
        $matidx[$hash] = $k;
        $newmatno[$no] = $k;
        $k++;
    }
}
$oldmat_num = count($json['materials']);
$newmat_num = $k;

// メッシュの対応マテリアル再割り当て
foreach ($json['meshes'] as $mno => $mesh) {
    if (isset($mesh['primitives']) && is_array($mesh['primitives'])) {
        foreach ($mesh['primitives'] as $pno => $prim) {
            if (isset($prim['material']) && isset($newmatno[$prim['material']])) {
                $json['meshes'][$mno]['primitives'][$pno]['material'] = $newmatno[$prim['material']];
            }
        }
    }
}

// マテリアルの再構築
$newmatdata = [];
$newmatno_rev = [];
foreach ($newmatno as $i => $k) $newmatno_rev[$k] = $i;
ksort($newmatno_rev, SORT_NUMERIC);
foreach ($newmatno_rev as $newno => $oldno) {
    $newmatdata[$newno] = $json['materials'][$oldno];
}
$json['materials'] = $newmatdata;

// ファイル保存
ini_set('serialize_precision', '-1');
file_put_contents($out_path, json_encode($json));
echo "Materials compressed! ${oldmat_num} to ${newmat_num}\n";

exit;
?>
