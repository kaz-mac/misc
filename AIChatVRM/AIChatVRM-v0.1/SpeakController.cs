//
// �A�o�^�[�𒝂点��R���g���[���[�B���b�v�V���N�ƕ\��
// �A�o�^�[�ɃA�^�b�`���Ďg��
//
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using voicevox_local;

public class SpeakController : MonoBehaviour
{
    [SerializeField] private float speakpeedScale = 1.0f;   // ����X�s�[�h
    [SerializeField] private float LipSyncSizeScale = 0.7f;    // ��ԑ傫�������J�������̃V�F�C�v�L�[�̍ő�l max 1.0f

    private AudioSource _audioSource;
    private BlendshapeController brendshape;
    private VoicevoxLocal voicebox = new VoicevoxLocal();

    private LipDatas lipsDatas;     // 1�������������b�v�V���N�̃f�[�^�̃��X�g
    bool lipPlaying = false;

    public bool nowSpeaking = false;    // �����Ă���Ԃ�true�ɂȂ�

    // �A�o�^�[���̃R���g���[���[���Q�Ƃł���悤�ɂ���
    private void Awake() {
        _audioSource = this.GetComponent<AudioSource>();
        brendshape = GetComponent<BlendshapeController>();
    }

    // ����
    public void Speak(string text, int SpeakerID) {
        StartCoroutine(SpeakText(text, SpeakerID));
    }
    private IEnumerator SpeakText(string text, int SpeakerID) {
        nowSpeaking = true;
        voicebox.speakerID = SpeakerID;
        voicebox.speedScale = speakpeedScale;
        yield return voicebox.GenerateAudioClip(text);  // VOICEVOX API�Ƀe�L�X�g��n���ĉ����f�[�^���擾����
        if (voicebox.AudioClip != null) {
            // �����̍Đ�
            _audioSource.clip = voicebox.AudioClip;
            _audioSource.Play();    // �������Đ�����
            // ���b�v�V���N�̏���
            StoreLipsyncList();     // VOIVEVOX���쐬�����N�G����1�����f�[�^�ɕϊ�����
            foreach (LipData lip in lipsDatas.lips) {
                brendshape.Lipsync(lip.vowel);  // ���̃V�F�C�v�L�[��ύX
                yield return new WaitForSeconds(lip.vowel_length);
            }
        }
        nowSpeaking = false;
    }

    // �\��̕ύX
    public void Emote(EmoteType emote, float wait) {
        brendshape.Emote(emote, wait);
    }

    // VOICEVOX�̃��b�v�f�[�^��1�����̃��X�g�Ɋi�[����
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

    // ���b�v�f�[�^��1�������X�g�̃N���X
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


