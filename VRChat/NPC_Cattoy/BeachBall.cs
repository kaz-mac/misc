//
// アイテムを持っているかどうか状態を保存
//
using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

public class BeachBall : UdonSharpBehaviour
{
    private Rigidbody rb;

    // 同期する変数
    [UdonSynced(UdonSyncMode.None)] public bool ispick = false;

    void Start()
    {
        //rb = (Rigidbody)transform.GetComponent(typeof(Rigidbody));
        rb = this.GetComponent<Rigidbody>();
    }

    private bool oldpick = true;
    private int oldid = -1;
    private void Update()
    {
        if (ispick != oldpick)
        {
            oldpick = ispick;
        }
        VRCPlayerApi owner = Networking.GetOwner(this.gameObject);
        if (owner != null && owner.playerId != oldid)
        {
            oldid = owner.playerId;
        }
    }

    // 持ったとき
    public override void OnPickup()
    {
        // ボールのオブジェクトのオーナーを変更する
        if (!Networking.IsOwner(Networking.LocalPlayer, this.gameObject))
        {
            Networking.SetOwner(Networking.LocalPlayer, this.gameObject); // 持った人をオーナーにする
        }
        ispick = true;
        rb.velocity = Vector3.zero;
        //BallGravityOn();    // 重力有効
    }

    // 離したとき
    public override void OnDrop()
    {
        ispick = false;
    }

    // ボールの表示
    public void BallShow()
    {
        //gameObject.SetActive(true);
        this.gameObject.GetComponent<Renderer>().enabled = true;
    }

    // ボールの非表示
    public void BallHide()
    {
        //this.gameObject.SetActive(false);
        this.gameObject.GetComponent<Renderer>().enabled = false;
    }

    // ボールの静止
    public void BallStop()
    {
        rb.velocity = Vector3.zero;
        rb.angularVelocity = Vector3.zero;
    }

    // ボールの重力無効　（※VRC Pickupを入れると初期状態に固定されてしまうのでこれは使えない）
    public void BallGravityOff()
    {
        rb.useGravity = false;
        rb.isKinematic = true;
    }

    // ボールの重力有効　（※VRC Pickupを入れると初期状態に固定されてしまうのでこれは使えない）
    public void BallGravityOn()
    {
        rb.useGravity = true;
        rb.isKinematic = false;
    }

    // リセット
    public void BallReset()
    {
        ispick = false;
        BallShow();
        //BallGravityOn();
        BallStop();
    }
}
