//
// 外部から新しい話題を提供する
// GameObject "SystemController" にアタッチして使う
// InputFieldのOn Edit Endに InputTopics > GetInputNewText() を追加する
//
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;

public class InputTopics: MonoBehaviour {
    [SerializeField] public InputField inputField;

    private SystemController syscon;

    private void Awake() {
        syscon = this.GetComponent<SystemController>();
    }

    // SystemControllerの会話履歴に入力したテキストを追加する
    public void GetInputNewText() {
        string text = inputField.text;
        Debug.Log("*** 新しい話題を投入: "+text);
        string message = $"会話がひと段落したら、新しい話題「{text}」に変えてください。";
        syscon.addTalkHistory(-1, message, -1); // role:system で発言する

        inputField.text = "";
    }
}
