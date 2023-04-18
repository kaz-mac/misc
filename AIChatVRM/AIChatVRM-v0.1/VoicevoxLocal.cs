//
// ���[�J���ŋN�����Ă���VOICEVOX��API�ɃA�N�Z�X���āA���������p�̃N�G���Ɖ����f�[�^���擾����
// �I�u�W�F�N�g�ɃA�^�b�`����K�v�͂Ȃ�
//
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Networking;
using Newtonsoft.Json;

namespace voicevox_local {
    public class VoicevoxLocal {
        public string voiceboxServerUrl = "http://127.0.0.1:50021";
        public int speakerID = 1;       // �X�s�[�J�[ID�i���ۂ̕ύX��SystemController�ōs���j
        public float speedScale = 1.0f; // �b�����x

        private VVAudioQuery _jsonAudioQuery;   
        public  VVAudioQuery jsonAudioQuery { get => _jsonAudioQuery; } // ���������p�̃N�G��
        private AudioClip  _audioClip;          
        public  AudioClip  AudioClip { get => _audioClip; }     // �������������I�[�f�B�I�N���b�v

        private bool debug = false;

        // �w�肵���e�L�X�g�ŉ����������s���A�I�[�f�B�I�N���b�v���擾����
        public IEnumerator GenerateAudioClip(string text) {
            _audioClip = null;
            yield return VoicevoxPostAudioQuery(text);
            if (_jsonAudioQuery == null) yield break;
            _jsonAudioQuery.speedScale = speedScale;
            yield return VoicevoxPostSynthesis(_jsonAudioQuery);
        }

        // ���������p�̃N�G�����쐬����
        IEnumerator VoicevoxPostAudioQuery(string text) {
            // VOICEBOX API audio_query �ɃA�N�Z�X
            string url = $"{voiceboxServerUrl}/audio_query?speaker={speakerID}&text={text}";
            using (var request = new UnityWebRequest(url, "POST")) {
                request.downloadHandler = (DownloadHandler)new DownloadHandlerBuffer();
                request.SetRequestHeader("Content-Type", "application/json");

                // ���M
                _jsonAudioQuery = null;
                yield return request.SendWebRequest();
                if (request.result == UnityWebRequest.Result.Success) {
                    if (debug) Debug.Log("VOICEVOX API audio_query success");
                    try {
                        // JSON���p�[�X����
                        _jsonAudioQuery = JsonConvert.DeserializeObject<VVAudioQuery>(request.downloadHandler.text);
                    } catch {
                        if (debug) Debug.Log("JSON decode failed");
                    }
                }
                else {
                    throw new System.Exception("VOICEVOX API audio_query failed");
                }
            }
        }

        // ������������
        IEnumerator VoicevoxPostSynthesis(VVAudioQuery audioQuerySub) {
            // VOICEBOX API synthesis �ɃA�N�Z�X
            string url = $"{voiceboxServerUrl}/synthesis?speaker={speakerID}";
            using (var request = new UnityWebRequest(url, "POST")) {
                string jsonString = JsonConvert.SerializeObject(audioQuerySub);
                byte[] postData = System.Text.Encoding.UTF8.GetBytes(jsonString);
                request.uploadHandler = new UploadHandlerRaw(postData);
                request.downloadHandler = new DownloadHandlerAudioClip(url, AudioType.WAV);
                request.SetRequestHeader("Content-Type", "application/json");

                // ���M
                yield return request.SendWebRequest();
                if (request.result == UnityWebRequest.Result.Success) {
                    if (debug) Debug.Log("VOICEVOX API synthesis success");
                    _audioClip = ((DownloadHandlerAudioClip)request.downloadHandler).audioClip;
                }
                else {
                    throw new System.Exception("VOICEVOX API synthesis failed");
                }
            }
        }
    }

    // VOICEBOX API audio_query ��JSON�N���X
    // Document: https://voicevox.github.io/voicevox_engine/api/
    public class VVAudioQuery {
        public List<VVAccentPhrase> accent_phrases { get; set; }
        public float speedScale { get; set; }
        public float pitchScale { get; set; }
        public float intonationScale { get; set; }
        public float volumeScale { get; set; }
        public float prePhonemeLength { get; set; }
        public float postPhonemeLength { get; set; }
        public int outputSamplingRate { get; set; }
        public bool outputStereo { get; set; }
        public string kana { get; set; }
        public VVDetail[] detail { get; set; }

        public class VVAccentPhrase {
            public List<VVMora> moras { get; set; }
            public int accent { get; set; }
            public VVMora pause_mora { get; set; }
            public bool is_interrogative { get; set; }
        }

        public class VVMora {
            public string text { get; set; }
            public string consonant { get; set; }
            public float? consonant_length { get; set; }
            public string vowel { get; set; }
            public float vowel_length { get; set; }
            public float pitch { get; set; }
        }

        public class VVDetail {
            public string[] loc { get; set; }
            public string msg { get; set; }
            public string type { get; set; }
        }
    }

}
