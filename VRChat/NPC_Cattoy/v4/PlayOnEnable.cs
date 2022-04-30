//
// Activeになったら1回だけ再生
//
using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

public class PlayOnEnable : UdonSharpBehaviour
{
    AudioSource audio;

    void OnEnable()
    {
        audio = GetComponent<AudioSource>();
        if (audio != null) audio.Play();
    }

    void OnDisable()
    {
        audio = GetComponent<AudioSource>();
        if (audio != null) audio.Stop();
    }
}
