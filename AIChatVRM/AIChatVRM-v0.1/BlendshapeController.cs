//
// VRM���f���̃u�����h�V�F�C�v�𐧌䂷��
// �A�o�^�[�ɃA�^�b�`���Ďg��
//
using System.Collections;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System;
using UnityEngine;
using VRM;

public class BlendshapeController : MonoBehaviour {
    [SerializeField] public bool autoBlinkEnable = true;    // �����I�ɂ܂΂�����������
    [SerializeField] public float aiueoMaxScale = 1f;     // �K�p������̃u�����h�V�F�C�v�̍ő�l

    private float blendAddDecPerOnece = 0.34f;  // ���̕ω���
    private float blendWaitPerOnece = 0.033f;   // ���̕ω��҂�����

    private VRMBlendShapeProxy proxy;

    private bool blinkOK = true;    // �܂΂����̏��
    private float[] blendWeight = { 0, 0, 0, 0, 0 };        // a,i,u,e,o �̌��ݒl
    private float[] blendWeightTarget = { 0, 0, 0, 0, 0 };  // a,i,u,e,o �̖ڕW�l

    private Dictionary<string, int> aiueo = new Dictionary<string, int>() { {"a",0}, {"i",1}, {"u",2}, {"e",3}, {"o",4} };
    private BlendShapePreset[] enumBlendShapePreset = { BlendShapePreset.A, BlendShapePreset.I, BlendShapePreset.U, BlendShapePreset.E, BlendShapePreset.O };

    // ������
    void Start() {
        proxy = this.gameObject.GetComponent<VRMBlendShapeProxy>();
        blinkOK = autoBlinkEnable;
        StartCoroutine(AutoBlink());          // �����܂΂����̃��[�v�̃R���[�`�������s����
        StartCoroutine(MouthAnimate());  // ���̓����̃��[�v�̃R���[�`�������s����
    }

    // �y���[�v�����z�����ł܂΂���������
    private IEnumerator AutoBlink() {
        while (true) {
            yield return new WaitForSeconds(UnityEngine.Random.Range(5f, 10f));   // �܂΂����Ԋu�������_���Ō���
            float[] weightAry = new float[] { 0.33f, 0.66f, 1f, 0.66f, 0.33f, 0 };
            if (autoBlinkEnable && blinkOK) {
                foreach (float weight in weightAry) {
                    if (blinkOK) {
                        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Blink), weight);
                        proxy.Apply();
                        yield return new WaitForSeconds(0.033f);
                    } else {
                        // �\����݂���������܂΂������~
                        proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Blink), 0);
                        proxy.Apply();
                        break;
                    }
                }
            }
            yield return null;
        }
    }

    // �ꉹ�ɉ����������J����
    public void Lipsync(string vowel) {
        vowel = vowel.ToLower();
        if (Regex.IsMatch(vowel, @"^[aiueo]$")) {
            float nowweight = 0;
            for (var i = 0; i < blendWeight.Length; i++) {
                if (blendWeight[i] > nowweight) nowweight = blendWeight[i];
            }
            for (var i = 0; i < blendWeight.Length; i++) {
                blendWeightTarget[i] = (i == aiueo[vowel]) ? 1f : 0;    // �^�[�Q�b�g��ݒ�
                // ���ݕʂ̔����̍Œ��Ȃ�ڂ��ւ���
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

    // �y���[�v�����z���̓��������Ԃ̌o�߂ɉ����ĕω�������
    private IEnumerator MouthAnimate() {
        while (true) {
            bool hit = false;
            for (var i = 0; i < blendWeightTarget.Length; i++) {
                if (blendWeight[i] > 0 || blendWeightTarget[i] > 0) {   // �J��
                    hit = true;
                    if (blendWeight[i] < blendWeightTarget[i]) {
                        blendWeight[i] += blendAddDecPerOnece;
                        if (blendWeight[i] > 1f) blendWeight[i] = 1f;
                    }
                    else if (blendWeight[i] > blendWeightTarget[i]) {   // ����
                        blendWeight[i] -= blendAddDecPerOnece * 0.5f;
                        if (blendWeight[i] < 0) blendWeight[i] = 0;
                    }
                    proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(enumBlendShapePreset[i]), blendWeight[i] * aiueoMaxScale);
                    if (blendWeight[i] >= 1f) blendWeightTarget[i] = 0; // �J�������������
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

    // �R�����g�F�\��̏����͂܂��쐬�r���B{joy}�ȊO���얢�m�F

    // �\��̕ύX
    private EmoteType nowEmote = EmoteType.Null;    // ���݂̕\��
    public void Emote(EmoteType emote, float wait) {
        if (nowEmote != EmoteType.Null) {
            EmoteSet(nowEmote, 0);  // ���ݍ쓮���Ȃ�߂�
        }
        nowEmote = emote;
        blinkOK = false;    // �܂΂������~
        StartCoroutine(EmoteAutoAnimation(emote, wait));  // �\��̕ω����A�j���[�V��������
    }
    public void EmoteSet(EmoteType emote, float weight) {
        switch (emote) {
            case EmoteType.Joy:
                proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Joy), weight);
                break;
            case EmoteType.Sorrow:
                proxy.ImmediatelySetValue(BlendShapeKey.CreateFromPreset(BlendShapePreset.Sorrow), weight);    // ��
                break;
        }
    }
    private IEnumerator EmoteAutoAnimation(EmoteType emote, float wait) {
        foreach (var weight in (new float[] { 0.25f, 0.50f, 0.75f, 1f })) {
            EmoteSet(emote, weight);  // �V�����\����Z�b�g�@�J�n
            yield return new WaitForSeconds(0.0334f);
        }
        yield return new WaitForSeconds(wait);
        foreach (var weight in (new float[] { 0.75f, 0.50f, 0.25f, 0 })) {
            EmoteSet(emote, weight);  // �V�����\����Z�b�g�@�I��
            yield return new WaitForSeconds(0.0334f);
        }
        nowEmote = EmoteType.Null;
        blinkOK = true;    // �܂΂����J�n
    }

    // �����FUnity�̃A�j���[�V��������VRM���f���̃V�F�C�v�L�[�𑀍삷��ꍇ�́A�ȉ��̂悤�ɂ���
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
