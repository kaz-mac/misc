//
// ��b�𐧌䂷�郁�C���v���O����
// GameObject "SystemController" �ɃA�^�b�`���Ďg��
//
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System.IO;
using System.Text.RegularExpressions;
using chatgpt_remote;

public class SystemController : MonoBehaviour {
    [SerializeField] public GameObject[] avaterObjects = new GameObject[2];   // �A�o�^�[�̃I�u�W�F�N�g
    [SerializeField] public int[] avaterSpeakerIDs = { 1, 1 };   // VOICEVOX�̃X�s�[�J�[ID
    [SerializeField] public string[] characterNames = { "���񂾂���1��", "���񂾂���2��" };   // �L�����N�^�[�̖��O
    [SerializeField] private string firstMessage = "���񂾂���1������ɂ��́I���Ȃɂ��Ă�̂��H";    // �ꌾ�ڂ̉�b�i�L����1��0�ցj
    [SerializeField] public int maxHistory = 5;    // �L�������b�����̏����

    SpeakController[] speakControllers; // �A�o�^�[����SpeakController

    private Queue<SpeakQueue> queue = new Queue<SpeakQueue>();  // ������e��\��̃L���[
    private List<TalkHistoryList> history = new List<TalkHistoryList>();    // ��b�����̃��X�g

    private ChatGPTRemote gpt;  // ChatGPT�̃N���X

    private bool chatGPTexecuting = false;  // ChatGPT�����s��
    private int talkCount = 0;  // ��b�񐔁i�g���ĂȂ��j

    // �L�����N�^�[�ݒ�
    private string defaultSetting = "";
    string[] characterSettings = new string[2];

    // �A�o�^�[���̔��b�R���g���[���[���Q�Ƃł���悤�ɂ���
    private void Awake() {
        speakControllers = new SpeakController[avaterObjects.Length];
        for (int av = 0; av < avaterObjects.Length; av++) {
            if (avaterObjects[av] != null) {
                speakControllers[av] = avaterObjects[av].GetComponent<SpeakController>();
            }
        }
        gpt = this.GetComponent<ChatGPTRemote>();
    }

    // ������
    void Start() {
        // �L�����ݒ���O���t�@�C������ǂݍ���
        defaultSetting = fileGetContent("AIChatVRM/config/system.txt");           // ChatGPT�ւ̎w��
        for (var i = 0; i < avaterObjects.Length; i++) {
            characterSettings[i] = fileGetContent($"AIChatVRM/config/character{i}.txt");   // �L�����ݒ�
        }
        // ������b���̍쐬
        addSpeakQueue(1, firstMessage);        // VOICEVOX�Œ���L���[�ɓ����
        addTalkHistory(1, firstMessage, 0);    // ��b������ۑ��i�L����1��0�ցj
        // ���[�v�����̊J�n
        StartCoroutine(StartAI());      // ChatGPT�ɂ���b���͂��쐬���J�n����R���[�`��
        StartCoroutine(StartSpeak());   // VOICEVOX�̉��������̃L���[�������J�n����R���[�`��
    }

    // ��b�����ɒǉ�����
    public void addTalkHistory(int speakAvater, string content, int nextAvater) {
        if (history.Count >= maxHistory) {  // ��b�������ꍇ�͌Â���b���폜
            int overnum = history.Count - maxHistory + 1;
            history.RemoveRange(0, overnum);
        }
        if (nextAvater == -1) { // ���̘b�҂����w��(-1)�̏ꍇ�͍Ō�̏�Ԃ������p��
            nextAvater = history[(history.Count - 1)].avatarNext;
        }
        history.Add(new TalkHistoryList {
            avatarFrom = speakAvater,
            text = content,
            avatarNext = nextAvater,
        });
    }

    // ��b����������ChatGPT�ɑ��M�����b���쐬����
    private void createChatGPTMessages() {
        if (history.Count >= 0) {
            int nextSpeakAvater = history[history.Count - 1].avatarNext;
            gpt.initMessageList();
            gpt.addMessageList("system", defaultSetting + characterSettings[nextSpeakAvater]);  // �L�����ݒ���ŏ��ɒ�`����
            foreach (TalkHistoryList hist in history) {
                if (hist.avatarFrom == -1) {
                    gpt.addMessageList("system", hist.text);
                }
                else if (hist.avatarFrom == nextSpeakAvater) {
                    gpt.addMessageList("assistant", hist.text);
                }
                else {
                    string otherText = $"{characterNames[hist.avatarFrom]}�u{hist.text}�v";
                    gpt.addMessageList("user", otherText);
                }
            }
        }
    }

    // TODO:
    // ����ɕʐl����b�����Ă���\��������̂őΏ����K�v

    // �y���[�v�����zChatGPT�ŉ�b���͂��쐬����B�L���[����萔���܂����珈�����ꎞ��~����
    private IEnumerator StartAI() {
        while (true) {
            // ���܂��Ă�L���[�̐��i��b�u���b�N�P�ʁj���J�E���g����
            int speakTextBlock = 0;
            foreach (SpeakQueue q in queue) {
                if (q.type == SpeakType.Fiinsh) speakTextBlock++;
            }

            // ChatGPT�̃��N�G�X�g�𑗐M����
            if (!chatGPTexecuting && history.Count > 0 && speakTextBlock <= 1) {
                chatGPTexecuting = true;
                createChatGPTMessages();    // ChatGPT�ɑ��M�����b���쐬����
                gpt.postChatQuery();        // ���s����
                yield return null;
            }

            // ChatGPT����̉�����҂�
            if (chatGPTexecuting) {
                while (!gpt.responseReady) {
                    yield return null;
                }
                string content = gpt.jsonQuery.choices[0].message.content ?? "";
                Debug.Log("�񓚁F" + content);
                // "���O�u�{���v" �Ƃ����`���ŉ񓚂��ꂽ�ꍇ�̓J�b�R���݂̂𔲂��o��
                if (Regex.IsMatch(content, @"�u.*?�v$")) {
                    content = Regex.Replace(content, @"�v$", "");
                    content = Regex.Replace(content, @"^[^�u]+�u", "");
                    //Debug.Log("REG: " + content);
                }

                // ����
                if (content.Length > 0) {
                    int nowSpeakAvater = history[history.Count - 1].avatarNext;

                    // ���t�Ɗ���p�����[�^�[{xxx}�𕪗�����
                    string[] contentSplitted = Regex.Split(content, @"(\{[a-z]+\})");
                    List<string> contentArray = new List<string>();
                    foreach (string str in contentSplitted) {
                        if (str.Length > 0) contentArray.Add(str);
                    }

                    // VOICEBOX�Œ��錾�t�Ɗ���p�����[�^�[���L���[�ɓ����
                    foreach (string subContent in contentArray) {
                        if (Regex.IsMatch(subContent, @"^{[a-z]+}$")) {
                            if (subContent == "{joy}") addEmoteQueue(nowSpeakAvater, EmoteType.Joy);
                            else if (subContent == "{sorrow}") addEmoteQueue(nowSpeakAvater, EmoteType.Sorrow);
                            //else addEmoteQueue(nowSpeakAvater, EmoteType.Neutral);
                        } else {
                            addSpeakQueue(nowSpeakAvater, subContent);    // VOICEVOX�Œ���L���[�ɓ����
                        }
                    }
                    addFinishQueue(nowSpeakAvater); // �����̎w��

                    // ���̔����҂����߂�i���݂�2�l�������Ȃ��̂ł����Е������ɒ���j
                    int nextSpeakAvater = (nowSpeakAvater + 1 < characterNames.Length) ? nowSpeakAvater + 1 : 0;

                    // ChatGPT�p�̉�b������ۑ�
                    addTalkHistory(nowSpeakAvater, content, nextSpeakAvater);
                    talkCount++;
                }
                chatGPTexecuting = false;
            }
            yield return null;
        }
    }

    // VOICEVOX�Œ�����e���L���[�ɓ����
    private void addSpeakQueue(int speakAvater, string content) {
        content = Regex.Replace(content, "[�E�u�v]", "");  // �����̋L�������
        queue.Enqueue(new SpeakQueue {
            avatar = speakAvater,
            text = content,
        });
    }

    // �\��̏���VOICEVOX�̃L���[�ɓ����
    private void addEmoteQueue(int speakAvater, EmoteType emote) {
        queue.Enqueue(new SpeakQueue {
            type = SpeakType.Emote,
            avatar = speakAvater,
            emote = emote,
        });
    }

    // �������Ӗ�����t���O��VOICEVOX�̃L���[�ɓ����
    private void addFinishQueue(int speakAvater) {
        queue.Enqueue(new SpeakQueue {
            type = SpeakType.Fiinsh,
            avatar = speakAvater,
        });
    }

    // �y���[�v�����zVOICEVIX�ŉ����������s���B�L���[����̎��͑ҋ@����
    private IEnumerator StartSpeak() {
        while (true) {
            if (queue.Count > 0) {
                SpeakQueue q = queue.Dequeue();
                // ��b�̏ꍇ
                int speakerID = (q.speakerID != null) ? q.speakerID ?? 0 : avaterSpeakerIDs[q.avatar];
                if (q.type == SpeakType.Speak) {
                    speakControllers[q.avatar].Speak(q.text, speakerID);
                    while (speakControllers[q.avatar].nowSpeaking) {    // ����I���܂őҋ@
                        yield return null;
                    }
                    yield return new WaitForSeconds(0.2f);
                }
                // �\��̏ꍇ
                else if (q.type == SpeakType.Emote) {
                    speakControllers[q.avatar].Emote(q.emote, UnityEngine.Random.Range(1.5f, 3f));
                }
            }
            yield return null;
        }
    }

    // �e�L�X�g�t�@�C����ǂݍ��� Asset/�t�H���_�ȉ�
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

    // ���b����L���[�̃N���X
    public class SpeakQueue {
        public SpeakType type = SpeakType.Speak;
        public int avatar;              // ���b��
        public int? speakerID = null;   // VOICEVOX��speaker��ID
        public string text = null;      // Speak�̂Ƃ�: ������e
        public EmoteType emote;         // Emote�̂Ƃ�: ����t���O
    }
    public enum SpeakType {
        Speak,
        Emote,
        Pause,
        Fiinsh,
    }

    // ��b�����̃��X�g
    public class TalkHistoryList {
        public int avatarFrom;      // �������l�i0�܂���1�B-1�̏ꍇ��system�����j
        public string text = null;  // ���������e
        public int avatarNext;      // ���ɒ����Ăق����l�i0�܂���1�B-1�̏ꍇ��1�O�Ɠ����ɂ���j
    }

}


