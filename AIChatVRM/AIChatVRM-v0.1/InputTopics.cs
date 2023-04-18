//
// �O������V�����b���񋟂���
// GameObject "SystemController" �ɃA�^�b�`���Ďg��
// InputField��On Edit End�� InputTopics > GetInputNewText() ��ǉ�����
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

    // SystemController�̉�b�����ɓ��͂����e�L�X�g��ǉ�����
    public void GetInputNewText() {
        string text = inputField.text;
        Debug.Log("*** �V�����b��𓊓�: "+text);
        string message = $"��b���Ђƒi��������A�V�����b��u{text}�v�ɕς��Ă��������B";
        syscon.addTalkHistory(-1, message, -1); // role:system �Ŕ�������

        inputField.text = "";
    }
}
