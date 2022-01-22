//
// オブジェクトをON/OFFする
//
using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

public class Humidai_OnOff : UdonSharpBehaviour
{
    [SerializeField] private GameObject[] _targets;

    public override void Interact()
    {
        for (int i = 0; i < _targets.Length; i++) { 
            _targets[i].SetActive(!_targets[i].activeSelf);
        }
    }
}
