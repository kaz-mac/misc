//
// イスの高さを調整する　同期対応
// ※注意 UpDownとVRC_Stationのy座標を同じにすること
//
using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

public class ChairUp : UdonSharpBehaviour
{
    [SerializeField] private GameObject _target;    // イスのオブジェクト(VRC_Station)
    [SerializeField] private bool _invert = false;  // 下げるときはtrue
    [SerializeField] private float _step = 0.03f;   // 昇降間隔
    [SerializeField] private float _max = 0.3f; // 昇降最大値

    // ボタンを押した場合
    public override void Interact()
    {
        // イスのオブジェクトのオーナーを変更する（VRC Object Syncで同期させるため）
        if (!Networking.IsOwner(Networking.LocalPlayer, _target.gameObject)) {
            Networking.SetOwner(Networking.LocalPlayer, _target.gameObject); //自分をオーナーにする
        }
        // 位置の変更
        Vector3 pos = _target.transform.position;
        pos.y += (_invert) ? -_step : _step;
        //Debug.Log("pos.y=" + pos.y +" trans="+ transform.position.y +" sa="+Mathf.Abs(pos.y - transform.position.y));
        if ((pos.y - transform.position.y) >= _max) pos.y = transform.position.y + _max;
        if ((transform.position.y - pos.y) >= _max) pos.y = transform.position.y - _max;
        _target.transform.position = pos;
    }
}
