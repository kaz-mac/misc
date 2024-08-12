/*
  DoorOpen.cs
  3Dマイホームデザイナー想定　ドアの開閉スクリプト

  Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

public class DoorOpen : UdonSharpBehaviour {
    [Header("基本設定")]
    public bool isSlider = false;       // 引き戸の場合はtrue、開き戸の場合はfalse
    public bool invert = false;         // 逆方向に開く場合はtrue（falseにして正の値を与えてもよい）
    public float speed = 1.0f;          // 開閉速度(s)
    [Header("対象オブジェクト（オプション）未指定時は親")]
    public bool parentX2 = false;       // ドアのオブジェクトは親の親
    public bool parentX3 = false;       // ドアのオブジェクトは親の親の親
    public Transform doorObject;        // ドアのオブジェクト 優先順位1（省略可）
    public string doorObjectName = "";  // ドアのオブジェクトの名前 優先順位2（省略可）
    [Header("開き戸の場合")]
    public int closeAngle = 0;          // 開き戸: 閉じたときの角度
    public int openAngle = -88;         // 開き戸: 開いた時の角度
    [Header("引き戸の場合")]
    public float closePosition = 0f;    // 引き戸: 閉じたときの位置(m)
    public float openPosition = -0.74f; // 引き戸: 開いた時の位置(m)

    private bool isOpening = false;
    private bool isClosing = false;
    private bool toggle = false;
    private float elapsedTime = 0.0f;

    // 初期化
    private void Start() {
        if (doorObjectName != "") {
            GameObject obj = GameObject.Find(doorObjectName);
            if (obj != null) {
                doorObject = obj.transform;
            }
        }
        if (doorObject == null) {
            if (parentX3) {
                doorObject = transform.parent.parent.parent.gameObject.transform;
            } else if (parentX2) {
                doorObject = transform.parent.parent.gameObject.transform;
            } else {
                doorObject = transform.parent.gameObject.transform;
            }
        }
    }

    // 開閉のアニメーション
    private void Update() {
        if (doorObject == null) return;
        int inv = (invert) ? -1 : 1;
        if (isOpening) {
            elapsedTime += Time.deltaTime;
            if (isSlider) {
                float xPosition = Mathf.Lerp(closePosition, openPosition, elapsedTime / speed) * inv;
                doorObject.transform.localPosition = new Vector3(xPosition, 0f, 0f);
            }
            else {
                float zRotation = Mathf.Lerp(closeAngle, openAngle, elapsedTime / speed) * inv;
                doorObject.localRotation = Quaternion.Euler(0f, 0f, zRotation);
            }
            if (elapsedTime >= speed) {
                isOpening = false;
                elapsedTime = 0.0f;
            }
        }
        else if (isClosing) {
            elapsedTime += Time.deltaTime;
            if (isSlider) {
                float xPosition = Mathf.Lerp(openPosition, closePosition, elapsedTime / speed) * inv;
                doorObject.transform.localPosition = new Vector3(xPosition, 0f, 0f);
            }
            else {
                float zRotation = Mathf.Lerp(openAngle, closeAngle, elapsedTime / speed) * inv;
                doorObject.localRotation = Quaternion.Euler(0f, 0f, zRotation);
            }
            if (elapsedTime >= speed) {
                isClosing = false;
                elapsedTime = 0.0f;
            }
        }
    }

    // インタラクト
    public override void Interact() {
        if (doorObject == null) return;
        if (!isOpening && !isClosing) {
            if (toggle) {
                isClosing = true;
                toggle = !toggle;
            }
            else {
                isOpening = true;
                toggle = !toggle;
            }
        }
    }
}
