using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

public class Pickup_Use_Toggle_with_Sound : UdonSharpBehaviour
{
    AudioSource thisAudioSource;
    [SerializeField] GameObject[] objs;

    void Start()
    {
        thisAudioSource = this.GetComponent<AudioSource>();
    }

    public override void OnPickupUseDown()
    {
        if (!Networking.IsOwner(Networking.LocalPlayer, this.gameObject)) Networking.SetOwner(Networking.LocalPlayer, this.gameObject);
        SendCustomNetworkEvent(VRC.Udon.Common.Interfaces.NetworkEventTarget.All, "ToggleObjs_and_PlaySound");
    }

    public void ToggleObjs_and_PlaySound()
    {
        if (objs.Length > 0)
        {
            if (objs[0].activeSelf) thisAudioSource.Stop();
            else thisAudioSource.Play();
        }
        for (var i = 0; objs.Length > i; i++)
        {
            objs[i].SetActive(!objs[i].activeSelf);
        }
    }
}
