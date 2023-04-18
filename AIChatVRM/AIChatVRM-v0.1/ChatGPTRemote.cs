//
// ChatGPT APIにアクセスして結果を取得する
// GameObject "SystemController" にアタッチして使う
//
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;
using UnityEngine;
using UnityEngine.Networking;
using Newtonsoft.Json;

namespace chatgpt_remote {
    public class ChatGPTRemote : MonoBehaviour {
        private string API_ENDPOINT_URL = "https://api.openai.com/v1";
        private string API_KEY_FILE = "AIChatVRM/config/chatgpt_apikey.txt";
        private string GPT_MODEL = "gpt-3.5-turbo";

        private GPTChatCompletions _jsonQuery;
        public GPTChatCompletions jsonQuery { get => _jsonQuery; }  // APIからのJSONデータが格納される

        private List<GPTMessage> _messageList = new List<GPTMessage>(); // ChatGPTに送信するメッセージリスト
        private bool _messageListReady = false; // メッセージの作成が完了するとtrueになる
        public bool responseReady = false;      // APIからデータを取得したらtrueになる
        private string apiKey = "";

        private int apiCountLimit = 1000;      // 誤動作防止のためAPIのアクセス回数の上限
        private float apiRetryLimit = 1.0f;    // 誤動作防止のためAPIの最低アクセス間隔
        private float apiLastAccessTime = 0;

        private bool debug = false;

        // 初期設定
        void Start() {
            // API KEYをテキストファイルから読み込む
            try {
                string path = Application.dataPath + "/" + API_KEY_FILE;
                apiKey = File.ReadAllText(path);
                apiKey = Regex.Replace(apiKey, @"\s+", "");
            } catch (FileNotFoundException) {
                Debug.LogError("File not found: " + API_KEY_FILE);
            }
            apiLastAccessTime = Time.time - apiRetryLimit;
        }

        // メッセージの準備が整ったらコルーチンを実行する（ループ処理）
        void Update() {
            if (_messageListReady && _messageList.Count > 0) {
                _messageListReady = false;
                responseReady = false;
                StartCoroutine(ChatGPTPostChatQuery());
            }
        }

        // メッセージリストをクリアする
        public void initMessageList() {
            _messageList.Clear();
        }
        // メッセージリストに追加する
        public void addMessageList(string role, string content) {
            _messageList.Add(new GPTMessage { role = role, content = content });
        }
        // メッセージリストの作成完了のフラグを立てる（これでChatGPTに送信される）
        public void postChatQuery() {
            _messageListReady = true;
        }

        // ChatGPI APIにアクセスする
        public IEnumerator ChatGPTPostChatQuery() {
            if (_messageList.Count < 0) yield break;

            // 念のため大量アクセスを防止する
            if (--apiCountLimit < 0 ) yield break;
            if (Time.time - apiLastAccessTime < apiRetryLimit) yield break;
            apiLastAccessTime = Time.time;

            // ヘッダーとボディの作成
            string url = $"{API_ENDPOINT_URL}/chat/completions";
            var requestHeaders = new Dictionary<string, string>() {
                {"Authorization", "Bearer " + apiKey},
                {"Content-type", "application/json"},
            };
            GPTRequestData requestData = new GPTRequestData { 
                model = GPT_MODEL,
                messages = _messageList,
            };
            string requestBodyJson = JsonUtility.ToJson(requestData);
            Debug.Log("send: " + requestBodyJson);

            // 送信する
            using (var request = new UnityWebRequest(url, "POST")) {
                foreach (var header in requestHeaders) {
                    request.SetRequestHeader(header.Key, header.Value);
                }
                byte[] postData = System.Text.Encoding.UTF8.GetBytes(requestBodyJson);
                request.uploadHandler = new UploadHandlerRaw(postData);
                request.downloadHandler = (DownloadHandler)new DownloadHandlerBuffer();
                yield return request.SendWebRequest();

                // 受信
                _jsonQuery = null;
                if (request.result == UnityWebRequest.Result.Success) {
                    if (debug) Debug.Log("ChatGPT API /chat/completions success");
                    try {
                        // JSONをパースする
                        _jsonQuery = JsonConvert.DeserializeObject<GPTChatCompletions>(request.downloadHandler.text);
                        Debug.Log("receive: "+request.downloadHandler.text);
                    } catch {
                        Debug.Log("receive: " + request.downloadHandler.text);
                        Debug.Log("JSON decode failed");
                    }
                }
                else {
                    Debug.Log("receive: " + request.downloadHandler.text);
                    if (debug) Debug.Log("ChatGPT API /chat/completions failed");
                }

                // 完了
                _messageList.Clear();
                responseReady = true;
            }
        }

        // ChatGPT API のJSONクラス
        // Document: https://platform.openai.com/docs/api-reference/making-requests

        // 共通
        [System.Serializable]
        public class GPTMessage {
            public string role;
            public string content;
        }

        // リクエストの送信用
        [System.Serializable]
        public class GPTRequestData {
            public string model;
            public List<GPTMessage> messages;
            //public int max_tokens = 100;
        }

        // APIからの受け取り用
        [System.Serializable]
        public class GPTChatCompletions {
            public string id { get; set; }
            public string @object { get; set; }
            public long? created { get; set; }
            public string model { get; set; }
            public GPTUsage usage { get; set; }
            public List<GPTChoice> choices { get; set; }

            [System.Serializable]
            public class GPTUsage {
                public int? prompt_tokens { get; set; }
                public int? completion_tokens { get; set; }
                public int? total_tokens { get; set; }
            }

            [System.Serializable]
            public class GPTChoice {
                public GPTMessage message { get; set; }
                public string finish_reason { get; set; }
                public int? index { get; set; }
            }
        }

    }
}