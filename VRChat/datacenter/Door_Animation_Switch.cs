//
// ドアの開閉アニメーションの変数を同期する　後から来た人も同期する
// ※同期モードは Manual にする
//
using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

public class Door_Animation_Switch : UdonSharpBehaviour
{
    public Animator doorAnimator;
    private int valhash = Animator.StringToHash("dooropen");
    private int wait = 0;

    // 同期する変数
    [UdonSynced(UdonSyncMode.None)] private bool nowstate;

    private void Start()
    {
        //Debug.Log("***** 初期状態 " + nowstate);
    }

    // 後から入ってきた人はドアの状態を同期する
    public override void OnPlayerJoined(VRCPlayerApi player)
    {
        if (Networking.LocalPlayer == player)
        {
            open_the_door();
        }
    }

    // 同期が完了するであろうタイミングを待つ
    private void Update()
    {
        if (wait > 0 && --wait <= 0)
        {
            //Debug.Log("***** 待機完了 実行!! "+nowstate);
            SendCustomNetworkEvent(VRC.Udon.Common.Interfaces.NetworkEventTarget.All, nameof(open_the_door)); // 全ユーザーに実行を指示
            wait = 0;
        }
    }

    // ボタンが押されたとき、押した人をオーナーにして変数を設定して同期実行
    public override void Interact()
    {
        if (!Networking.IsOwner(Networking.LocalPlayer, this.gameObject))
        {
            Networking.SetOwner(Networking.LocalPlayer, this.gameObject); //自分をオーナーにする
            //Debug.Log("***** オーナー変更");
        }
        nowstate = !doorAnimator.GetBool(valhash);
        RequestSerialization(); // マニュアル同期実行
        //Debug.Log("***** 同期変数変更　変更後=" + nowstate);
        wait = 10;   // 10フレーム後にドアを開けるメソッドを実行する
    }

    // ドアを開ける
    public void open_the_door()
    {
        //Debug.Log("***** 開閉 " + nowstate);
        doorAnimator.SetBool(valhash, nowstate);
    }
}
