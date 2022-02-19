
using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

public class DirectionalLightReceiver : UdonSharpBehaviour
{
    [SerializeField] public Light _directional_light;   // Directional Lightのオブジェクト
    [SerializeField] public GameObject _mirror;    // ミラーのオブジェクト
    [SerializeField] public GameObject _plate;    // プレートのオブジェクト

    public bool Yodo_isReceiveSliderValueChangeEvent = true;
    public float Yodo_lightIntensity = 0.75f;
    private float remain = 0f;
    private int cnt = 0;

    // スライダーから明るさを変更を行うレシーバー
    public void Yodo_OnSliderValueChanged()
    {
        _directional_light.intensity = Yodo_lightIntensity;
        _mirror.SetActive(true);
        _plate.SetActive(false);
        remain = (cnt++ > 0) ? 3.0f : 0f;
    }

    // ミラーを消すタイマー
    private void Update()
    {
        remain -= Time.deltaTime;
        if (remain < 0f && remain > -1f)
        {
            _mirror.SetActive(false);
            _plate.SetActive(true);
        }
    }
}
