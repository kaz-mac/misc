//
// アバターを喋らせるコントローラー。リップシンクと表情
// アバターにアタッチして使う
//
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using voicevox_local;

public class SpeakController : MonoBehaviour
{
    [SerializeField] private float speakpeedScale = 1.0f;   // 喋るスピード
    [SerializeField] private float LipSyncSizeScale = 0.7f;    // 一番大きく口を開けた時のシェイプキーの最大値 max 1.0f

    private AudioSource _audioSource;
    private BlendshapeController brendshape;
    private VoicevoxLocal voicebox = new VoicevoxLocal();

    private LipDatas lipsDatas;     // 1次元化したリップシンクのデータのリスト
    bool lipPlaying = false;

    public bool nowSpeaking = false;    // 喋っている間はtrueになる

    // アバター側のコントローラーを参照できるようにする
    private void Awake() {
        _audioSource = this.GetComponent<AudioSource>();
        brendshape = GetComponent<BlendshapeController>();
    }

    // 喋る
    public void Speak(string text, int SpeakerID) {
        StartCoroutine(SpeakText(text, SpeakerID));
    }
    private IEnumerator SpeakText(string text, int SpeakerID) {
        nowSpeaking = true;
        voicebox.speakerID = SpeakerID;
        voicebox.speedScale = speakpeedScale;
        yield return voicebox.GenerateAudioClip(text);  // VOICEVOX APIにテキストを渡して音声データを取得する
        if (voicebox.AudioClip != null) {
            // 音声の再生
            _audioSource.clip = voicebox.AudioClip;
            _audioSource.Play();    // 音声を再生する
            // リップシンクの処理
            StoreLipsyncList();     // VOIVEVOXが作成したクエリを1次元データに変換する
            foreach (LipData lip in lipsDatas.lips) {
                brendshape.Lipsync(lip.vowel);  // 口のシェイプキーを変更
                yield return new WaitForSeconds(lip.vowel_length);
            }
        }
        nowSpeaking = false;
    }

    // 表情の変更
    public void Emote(EmoteType emote, float wait) {
        brendshape.Emote(emote, wait);
    }

    // VOICEVOXのリップデータを1次元のリストに格納する
    void StoreLipsyncList () {
        lipsDatas = new LipDatas();
        int pCount = voicebox.jsonAudioQuery.accent_phrases.Count;
        for (int i = 0; i < pCount; i++) {
            int mCount = voicebox.jsonAudioQuery.accent_phrases[i].moras.Count;
            for (int j = 0; j < mCount; j++) {
                string vowel = voicebox.jsonAudioQuery.accent_phrases[i].moras[j].vowel;
                float length = (voicebox.jsonAudioQuery.accent_phrases[i].moras[j].consonant_length ?? 0f) + voicebox.jsonAudioQuery.accent_phrases[i].moras[j].vowel_length;
                lipsDatas.lips.Add(new LipData() { vowel = vowel, vowel_length = length });
            }
            if (voicebox.jsonAudioQuery.accent_phrases[i].pause_mora != null) {
                string vowel = voicebox.jsonAudioQuery.accent_phrases[i].pause_mora.vowel;
                float length = voicebox.jsonAudioQuery.accent_phrases[i].pause_mora.vowel_length;
                lipsDatas.lips.Add(new LipData() { vowel = vowel, vowel_length = length });
            }
        }
    }

    // リップデータの1次元リストのクラス
    public class LipDatas {
        public List<LipData> lips { get; set; }
        public LipDatas() {
            lips = new List<LipData>();
        }
    }
    public class LipData {
        public string vowel { get; set; }
        public float vowel_length { get; set; }
    }

}


