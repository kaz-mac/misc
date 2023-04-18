//
// 会話を制御するメインプログラム
// GameObject "SystemController" にアタッチして使う
//
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System.IO;
using System.Text.RegularExpressions;
using chatgpt_remote;

public class SystemController : MonoBehaviour {
    [SerializeField] public GameObject[] avaterObjects = new GameObject[2];   // アバターのオブジェクト
    [SerializeField] public int[] avaterSpeakerIDs = { 1, 1 };   // VOICEVOXのスピーカーID
    [SerializeField] public string[] characterNames = { "ずんだもん1号", "ずんだもん2号" };   // キャラクターの名前
    [SerializeField] private string firstMessage = "ずんだもん1号こんにちは！今なにしてるのだ？";    // 一言目の会話（キャラ1→0へ）
    [SerializeField] public int maxHistory = 5;    // 記憶する会話履歴の上限数

    SpeakController[] speakControllers; // アバター側のSpeakController

    private Queue<SpeakQueue> queue = new Queue<SpeakQueue>();  // 喋る内容や表情のキュー
    private List<TalkHistoryList> history = new List<TalkHistoryList>();    // 会話履歴のリスト

    private ChatGPTRemote gpt;  // ChatGPTのクラス

    private bool chatGPTexecuting = false;  // ChatGPTを実行中
    private int talkCount = 0;  // 会話回数（使ってない）

    // キャラクター設定
    private string defaultSetting = "";
    string[] characterSettings = new string[2];

    // アバター側の発話コントローラーを参照できるようにする
    private void Awake() {
        speakControllers = new SpeakController[avaterObjects.Length];
        for (int av = 0; av < avaterObjects.Length; av++) {
            if (avaterObjects[av] != null) {
                speakControllers[av] = avaterObjects[av].GetComponent<SpeakController>();
            }
        }
        gpt = this.GetComponent<ChatGPTRemote>();
    }

    // 初期化
    void Start() {
        // キャラ設定を外部ファイルから読み込む
        defaultSetting = fileGetContent("AIChatVRM/config/system.txt");           // ChatGPTへの指示
        for (var i = 0; i < avaterObjects.Length; i++) {
            characterSettings[i] = fileGetContent($"AIChatVRM/config/character{i}.txt");   // キャラ設定
        }
        // 初期会話文の作成
        addSpeakQueue(1, firstMessage);        // VOICEVOXで喋るキューに入れる
        addTalkHistory(1, firstMessage, 0);    // 会話履歴を保存（キャラ1→0へ）
        // ループ処理の開始
        StartCoroutine(StartAI());      // ChatGPTによる会話文章を作成を開始するコルーチン
        StartCoroutine(StartSpeak());   // VOICEVOXの音声合成のキュー処理を開始するコルーチン
    }

    // 会話履歴に追加する
    public void addTalkHistory(int speakAvater, string content, int nextAvater) {
        if (history.Count >= maxHistory) {  // 会話が長い場合は古い会話を削除
            int overnum = history.Count - maxHistory + 1;
            history.RemoveRange(0, overnum);
        }
        if (nextAvater == -1) { // 次の話者が未指定(-1)の場合は最後の状態を引き継ぐ
            nextAvater = history[(history.Count - 1)].avatarNext;
        }
        history.Add(new TalkHistoryList {
            avatarFrom = speakAvater,
            text = content,
            avatarNext = nextAvater,
        });
    }

    // 会話履歴を元にChatGPTに送信する会話を作成する
    private void createChatGPTMessages() {
        if (history.Count >= 0) {
            int nextSpeakAvater = history[history.Count - 1].avatarNext;
            gpt.initMessageList();
            gpt.addMessageList("system", defaultSetting + characterSettings[nextSpeakAvater]);  // キャラ設定を最初に定義する
            foreach (TalkHistoryList hist in history) {
                if (hist.avatarFrom == -1) {
                    gpt.addMessageList("system", hist.text);
                }
                else if (hist.avatarFrom == nextSpeakAvater) {
                    gpt.addMessageList("assistant", hist.text);
                }
                else {
                    string otherText = $"{characterNames[hist.avatarFrom]}「{hist.text}」";
                    gpt.addMessageList("user", otherText);
                }
            }
        }
    }

    // TODO:
    // 勝手に別人が会話入ってくる可能性があるので対処が必要

    // 【ループ処理】ChatGPTで会話文章を作成する。キューが一定数溜まったら処理を一時停止する
    private IEnumerator StartAI() {
        while (true) {
            // 溜まってるキューの数（会話ブロック単位）をカウントする
            int speakTextBlock = 0;
            foreach (SpeakQueue q in queue) {
                if (q.type == SpeakType.Fiinsh) speakTextBlock++;
            }

            // ChatGPTのリクエストを送信する
            if (!chatGPTexecuting && history.Count > 0 && speakTextBlock <= 1) {
                chatGPTexecuting = true;
                createChatGPTMessages();    // ChatGPTに送信する会話を作成する
                gpt.postChatQuery();        // 実行する
                yield return null;
            }

            // ChatGPTからの応答を待つ
            if (chatGPTexecuting) {
                while (!gpt.responseReady) {
                    yield return null;
                }
                string content = gpt.jsonQuery.choices[0].message.content ?? "";
                Debug.Log("回答：" + content);
                // "名前「本文」" という形式で回答された場合はカッコ内のみを抜き出す
                if (Regex.IsMatch(content, @"「.*?」$")) {
                    content = Regex.Replace(content, @"」$", "");
                    content = Regex.Replace(content, @"^[^「]+「", "");
                    //Debug.Log("REG: " + content);
                }

                // 喋る
                if (content.Length > 0) {
                    int nowSpeakAvater = history[history.Count - 1].avatarNext;

                    // 言葉と感情パラメーター{xxx}を分離する
                    string[] contentSplitted = Regex.Split(content, @"(\{[a-z]+\})");
                    List<string> contentArray = new List<string>();
                    foreach (string str in contentSplitted) {
                        if (str.Length > 0) contentArray.Add(str);
                    }

                    // VOICEBOXで喋る言葉と感情パラメーターをキューに入れる
                    foreach (string subContent in contentArray) {
                        if (Regex.IsMatch(subContent, @"^{[a-z]+}$")) {
                            if (subContent == "{joy}") addEmoteQueue(nowSpeakAvater, EmoteType.Joy);
                            else if (subContent == "{sorrow}") addEmoteQueue(nowSpeakAvater, EmoteType.Sorrow);
                            //else addEmoteQueue(nowSpeakAvater, EmoteType.Neutral);
                        } else {
                            addSpeakQueue(nowSpeakAvater, subContent);    // VOICEVOXで喋るキューに入れる
                        }
                    }
                    addFinishQueue(nowSpeakAvater); // 文末の指定

                    // 次の発言者を決める（現在は2人しかいないのでもう片方が次に喋る）
                    int nextSpeakAvater = (nowSpeakAvater + 1 < characterNames.Length) ? nowSpeakAvater + 1 : 0;

                    // ChatGPT用の会話履歴を保存
                    addTalkHistory(nowSpeakAvater, content, nextSpeakAvater);
                    talkCount++;
                }
                chatGPTexecuting = false;
            }
            yield return null;
        }
    }

    // VOICEVOXで喋る内容をキューに入れる
    private void addSpeakQueue(int speakAvater, string content) {
        content = Regex.Replace(content, "[・「」]", "");  // 無音の記号を取る
        queue.Enqueue(new SpeakQueue {
            avatar = speakAvater,
            text = content,
        });
    }

    // 表情の情報をVOICEVOXのキューに入れる
    private void addEmoteQueue(int speakAvater, EmoteType emote) {
        queue.Enqueue(new SpeakQueue {
            type = SpeakType.Emote,
            avatar = speakAvater,
            emote = emote,
        });
    }

    // 文末を意味するフラグをVOICEVOXのキューに入れる
    private void addFinishQueue(int speakAvater) {
        queue.Enqueue(new SpeakQueue {
            type = SpeakType.Fiinsh,
            avatar = speakAvater,
        });
    }

    // 【ループ処理】VOICEVIXで音声合成を行う。キューが空の時は待機する
    private IEnumerator StartSpeak() {
        while (true) {
            if (queue.Count > 0) {
                SpeakQueue q = queue.Dequeue();
                // 会話の場合
                int speakerID = (q.speakerID != null) ? q.speakerID ?? 0 : avaterSpeakerIDs[q.avatar];
                if (q.type == SpeakType.Speak) {
                    speakControllers[q.avatar].Speak(q.text, speakerID);
                    while (speakControllers[q.avatar].nowSpeaking) {    // 喋り終わるまで待機
                        yield return null;
                    }
                    yield return new WaitForSeconds(0.2f);
                }
                // 表情の場合
                else if (q.type == SpeakType.Emote) {
                    speakControllers[q.avatar].Emote(q.emote, UnityEngine.Random.Range(1.5f, 3f));
                }
            }
            yield return null;
        }
    }

    // テキストファイルを読み込む Asset/フォルダ以下
    private string fileGetContent(string filename) {
        string text = "";
        string path = Application.dataPath + "/" + filename;
        try {
            text = File.ReadAllText(path);
        } catch (FileNotFoundException) {
            Debug.LogError("File not found: " + path);
        }
        return text;
    }

    // 発話するキューのクラス
    public class SpeakQueue {
        public SpeakType type = SpeakType.Speak;
        public int avatar;              // 発話者
        public int? speakerID = null;   // VOICEVOXのspeakerのID
        public string text = null;      // Speakのとき: 喋る内容
        public EmoteType emote;         // Emoteのとき: 感情フラグ
    }
    public enum SpeakType {
        Speak,
        Emote,
        Pause,
        Fiinsh,
    }

    // 会話履歴のリスト
    public class TalkHistoryList {
        public int avatarFrom;      // 喋った人（0または1。-1の場合はsystem扱い）
        public string text = null;  // 喋った内容
        public int avatarNext;      // 次に喋ってほしい人（0または1。-1の場合は1つ前と同じにする）
    }

}


