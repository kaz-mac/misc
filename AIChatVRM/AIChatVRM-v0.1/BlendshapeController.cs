//
// VRMモデルのブレンドシェイプを制御する
// アバターにアタッチして使う
//
using System.Collections;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System;
using UnityEngine;
using VRM;

public class BlendshapeController : MonoBehaviour {
    [SerializeField] public bool autoBlinkEnable = true;    // 自動的にまばたきをさせる
    [SerializeField] public float aiueoMaxScale = 1f;     // 適用する口のブレンドシェイプの最大値

    private float blendAddDecPerOnece = 0.34f;  // 口の変化量
    private float blendWaitPerOnece = 0.033f;   // 口の変化待ち時間

    private VRMBlendShapeProxy proxy;

    private bool blinkOK = true;    // まばたき可の状態
    private float[] blendWeight = { 0, 0, 0, 0, 0 };        // a,i,u,e,o の現在値
    private float[] blendWeightTarget = { 0, 0, 0, 0, 0 };  // a,i,u,e,o の目標値

    private Dictionary<string, int> aiueo = new Dictionary<string, int>() { {"a",0}, {"i",1}, {"u",2}, {"e",3}, {"o",4} };
    private BlendShapePreset[] enumBlendShapePreset = { BlendShapePreset.A, BlendShapePreset.I, BlendShapePreset.U, BlendShapePreset.E, BlendShapePreset.O };

    // 初期化
    void Start() {
        proxy = this.gameObject.GetComponent<VRMBlendShapeProxy>();
        blinkOK = autoBlinkEnable;
        StartCoroutine(AutoBlink());          // 自動まばたきのループのコルーチンを実行する
        StartCoroutine(MouthAnimate());  // 口の動きのループのコルーチンを実行する
    }

    // 【ループ処理】自動でまばたきをする
    private IEnumerator AutoBlink() {
        while (true) {
            yield return new WaitForSeconds(UnityEngine.Random.Range(5f, 10f));   // まばたき間隔をランダムで決定
            float[] weightAry = new float[] { 0.33f, 0.66f, 1f, 0.66f, 0.33f, 0 };
            if (autoBlinkEnable && blinkOK) {
                foreach (float weight in weightAry) {
                    if (blinkOK) {
                        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Blink), weight);
                        proxy.Apply();
                        yield return new WaitForSeconds(0.033f);
                    } else {
                        // 表情割込みが入ったらまばたき中止
                        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Blink), 0);
                        proxy.Apply();
                        break;
                    }
                }
            }
            yield return null;
        }
    }

    // 母音に応じた口を開ける
    public void Lipsync(string vowel) {
        vowel = vowel.ToLower();
        if (Regex.IsMatch(vowel, @"^[aiueo]$")) {
            float nowweight = 0;
            for (var i = 0; i < blendWeight.Length; i++) {
                if (blendWeight[i] > nowweight) nowweight = blendWeight[i];
            }
            for (var i = 0; i < blendWeight.Length; i++) {
                blendWeightTarget[i] = (i == aiueo[vowel]) ? 1f : 0;    // ターゲットを設定
                // 現在別の発音の最中なら移し替える
                if (nowweight > 0) {
                    if (i == aiueo[vowel]) {
                        blendWeight[i] = nowweight;
                        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(enumBlendShapePreset[i]), nowweight * aiueoMaxScale);
                    }
                    else {
                        blendWeight[i] = 0;
                        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(enumBlendShapePreset[i]), 0);
                    }
                }
            }
            if (nowweight > 0) proxy.Apply();
        }
        else {
            Array.Clear(blendWeightTarget, 0, blendWeightTarget.Length);
        }
    }

    // 【ループ処理】口の動きを時間の経過に応じて変化させる
    private IEnumerator MouthAnimate() {
        while (true) {
            bool hit = false;
            for (var i = 0; i < blendWeightTarget.Length; i++) {
                if (blendWeight[i] > 0 || blendWeightTarget[i] > 0) {   // 開く
                    hit = true;
                    if (blendWeight[i] < blendWeightTarget[i]) {
                        blendWeight[i] += blendAddDecPerOnece;
                        if (blendWeight[i] > 1f) blendWeight[i] = 1f;
                    }
                    else if (blendWeight[i] > blendWeightTarget[i]) {   // 閉じる
                        blendWeight[i] -= blendAddDecPerOnece * 0.5f;
                        if (blendWeight[i] < 0) blendWeight[i] = 0;
                    }
                    proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(enumBlendShapePreset[i]), blendWeight[i] * aiueoMaxScale);
                    if (blendWeight[i] >= 1f) blendWeightTarget[i] = 0; // 開ききったら閉じる
                }
            }
            if (hit) {
                proxy.Apply();
                yield return new WaitForSeconds(blendWaitPerOnece);
            }
            else {
                yield return null;
            }
        }
    }

    // コメント：表情の処理はまだ作成途中。{joy}以外動作未確認

    // 表情の変更
    private EmoteType nowEmote = EmoteType.Null;    // 現在の表情
    public void Emote(EmoteType emote, float wait) {
        if (nowEmote != EmoteType.Null) {
            EmoteSet(nowEmote, 0);  // 現在作動中なら戻す
        }
        nowEmote = emote;
        blinkOK = false;    // まばたき中止
        StartCoroutine(EmoteAutoAnimation(emote, wait));  // 表情の変化をアニメーションする
    }
    public void EmoteSet(EmoteType emote, float weight) {
        switch (emote) {
            case EmoteType.Joy:
                proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Joy), weight);
                break;
            case EmoteType.Sorrow:
                proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Sorrow), weight);    // 仮
                break;
        }
    }
    private IEnumerator EmoteAutoAnimation(EmoteType emote, float wait) {
        foreach (var weight in (new float[] { 0.25f, 0.50f, 0.75f, 1f })) {
            EmoteSet(emote, weight);  // 新しい表情をセット　開始
            yield return new WaitForSeconds(0.0334f);
        }
        yield return new WaitForSeconds(wait);
        foreach (var weight in (new float[] { 0.75f, 0.50f, 0.25f, 0 })) {
            EmoteSet(emote, weight);  // 新しい表情をセット　終了
            yield return new WaitForSeconds(0.0334f);
        }
        nowEmote = EmoteType.Null;
        blinkOK = true;    // まばたき開始
    }

    // メモ：UnityのアニメーションからVRMモデルのシェイプキーを操作する場合は、以下のようにする
    /*
    [SerializeField, Range(0, 1f)] public float Blink = 0;
    void applyShapeKey() {
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.A), A);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.I), I);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.U), U);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.E), E);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.O), O);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Blink), Blink);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Blink_L), Blink_L);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Blink_R), Blink_R);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Angry), Angry);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Fun), Fun);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Joy), Joy);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Sorrow), Sorrow);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.LookUp), LookUp);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.LookDown), LookDown);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.LookLeft), LookLeft);
        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.LookRight), LookRight);
        proxy.Apply();
    }
    */
}
