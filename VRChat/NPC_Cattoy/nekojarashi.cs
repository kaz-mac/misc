//
// アイテムを持つと有効化、有効化するとNPCが寄ってくる
//
using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

public class nekojarashi : UdonSharpBehaviour
{
    [SerializeField] private GameObject _target;    // 目標点のオブジェクト

    // 同期する変数
    [UdonSynced(UdonSyncMode.None)] public bool nowstate = false;
    private bool nowstate_old = true;

    // 持ったとき
    public override void OnPickup()
    {
        toggle();
    }

    // 離したとき
    public override void OnDrop()
    {
        toggle();
    }

    // 同期変数を反転する
    public void toggle()
    {
        nowstate = !nowstate;
    }

    // オブジェクトの状態を変更する
    public void setactive()
    {
        _target.SetActive(nowstate);
    }

    // 変数が同期されて内容が変わったら処理を実行する
    private void Update()
    {
        if (nowstate != nowstate_old)
        {
            setactive();
            nowstate_old = nowstate;
        }
    }
}
