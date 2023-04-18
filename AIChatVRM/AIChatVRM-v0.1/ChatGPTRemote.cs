//
// ChatGPT API�ɃA�N�Z�X���Č��ʂ��擾����
// GameObject "SystemController" �ɃA�^�b�`���Ďg��
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
        public GPTChatCompletions jsonQuery { get => _jsonQuery; }  // API�����JSON�f�[�^���i�[�����

        private List<GPTMessage> _messageList = new List<GPTMessage>(); // ChatGPT�ɑ��M���郁�b�Z�[�W���X�g
        private bool _messageListReady = false; // ���b�Z�[�W�̍쐬�����������true�ɂȂ�
        public bool responseReady = false;      // API����f�[�^���擾������true�ɂȂ�
        private string apiKey = "";

        private int apiCountLimit = 1000;      // �듮��h�~�̂���API�̃A�N�Z�X�񐔂̏��
        private float apiRetryLimit = 1.0f;    // �듮��h�~�̂���API�̍Œ�A�N�Z�X�Ԋu
        private float apiLastAccessTime = 0;

        private bool debug = false;

        // �����ݒ�
        void Start() {
            // API KEY���e�L�X�g�t�@�C������ǂݍ���
            try {
                string path = Application.dataPath + "/" + API_KEY_FILE;
                apiKey = File.ReadAllText(path);
                apiKey = Regex.Replace(apiKey, @"\s+", "");
            } catch (FileNotFoundException) {
                Debug.LogError("File not found: " + API_KEY_FILE);
            }
            apiLastAccessTime = Time.time - apiRetryLimit;
        }

        // ���b�Z�[�W�̏�������������R���[�`�������s����i���[�v�����j
        void Update() {
            if (_messageListReady && _messageList.Count > 0) {
                _messageListReady = false;
                responseReady = false;
                StartCoroutine(ChatGPTPostChatQuery());
            }
        }

        // ���b�Z�[�W���X�g���N���A����
        public void initMessageList() {
            _messageList.Clear();
        }
        // ���b�Z�[�W���X�g�ɒǉ�����
        public void addMessageList(string role, string content) {
            _messageList.Add(new GPTMessage { role = role, content = content });
        }
        // ���b�Z�[�W���X�g�̍쐬�����̃t���O�𗧂Ă�i�����ChatGPT�ɑ��M�����j
        public void postChatQuery() {
            _messageListReady = true;
        }

        // ChatGPI API�ɃA�N�Z�X����
        public IEnumerator ChatGPTPostChatQuery() {
            if (_messageList.Count < 0) yield break;

            // �O�̂��ߑ�ʃA�N�Z�X��h�~����
            if (--apiCountLimit < 0 ) yield break;
            if (Time.time - apiLastAccessTime < apiRetryLimit) yield break;
            apiLastAccessTime = Time.time;

            // �w�b�_�[�ƃ{�f�B�̍쐬
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

            // ���M����
            using (var request = new UnityWebRequest(url, "POST")) {
                foreach (var header in requestHeaders) {
                    request.SetRequestHeader(header.Key, header.Value);
                }
                byte[] postData = System.Text.Encoding.UTF8.GetBytes(requestBodyJson);
                request.uploadHandler = new UploadHandlerRaw(postData);
                request.downloadHandler = (DownloadHandler)new DownloadHandlerBuffer();
                yield return request.SendWebRequest();

                // ��M
                _jsonQuery = null;
                if (request.result == UnityWebRequest.Result.Success) {
                    if (debug) Debug.Log("ChatGPT API /chat/completions success");
                    try {
                        // JSON���p�[�X����
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

                // ����
                _messageList.Clear();
                responseReady = true;
            }
        }

        // ChatGPT API ��JSON�N���X
        // Document: https://platform.openai.com/docs/api-reference/making-requests

        // ����
        [System.Serializable]
        public class GPTMessage {
            public string role;
            public string content;
        }

        // ���N�G�X�g�̑��M�p
        [System.Serializable]
        public class GPTRequestData {
            public string model;
            public List<GPTMessage> messages;
            //public int max_tokens = 100;
        }

        // API����̎󂯎��p
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