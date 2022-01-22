using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

public class Server_SV_LEDs : UdonSharpBehaviour
{
    public bool _enable;
    public Material _green_material_on;
    public Material _green_material_off;
    public Material _red_material_on;
    public Material _red_material_off;
    public GameObject[] _green_leds = new GameObject[2];
    public GameObject _red_led;

    float[] gwaits = new float[2];
    float rwait = 1.0f;
    float[] gpastsecs = { 0, 0 };
    float rpastsec = 0;
    bool[] goldstates = { true, true };
    bool roldstate = true;

    // Start is called before the first frame update
    void Start()
    {
        for (int i = 0; i < _green_leds.Length; i++)
        {
            gwaits[i] = Random.Range(0.07f, 0.17f);
        }
    }

    // Update is called once per frame
    void Update()
    {
        if (_enable)
        {
            blink_green_leds();
            blink_red_led();
        }
    }

    // 緑のLED
    void blink_green_leds()
    {
        for (int i = 0; i < _green_leds.Length; i++)
        {
            gpastsecs[i] += Time.deltaTime;
            if (gpastsecs[i] > gwaits[i])
            {
                if (goldstates[i])
                {
                    _green_leds[i].GetComponent<MeshRenderer>().material = _green_material_off;
                    //_leds1[i].GetComponent<MeshRenderer>().sharedMaterial = _material_off;
                }
                else
                {
                    _green_leds[i].GetComponent<MeshRenderer>().material = _green_material_on;
                    //_leds1[i].GetComponent<MeshRenderer>().sharedMaterial = _material_on;
                }
                gpastsecs[i] = 0;
                goldstates[i] = !goldstates[i];
            }
        }
    }

    // 赤のLED
    void blink_red_led()
    {
        rpastsec += Time.deltaTime;
        if (rpastsec > rwait)
        {
            if (roldstate)
            {
                _red_led.GetComponent<MeshRenderer>().material = _red_material_off;
            }
            else
            {
                _red_led.GetComponent<MeshRenderer>().material = _red_material_on;
            }
            rpastsec = 0;
            roldstate = !roldstate;
        }
    }
}
