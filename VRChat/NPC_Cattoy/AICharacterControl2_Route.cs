//
// 移動先の指定、目標の位置による動作の決定、視線追従
//
// 参考 NavMesh設定
// Base 0, Speed 3, Angluer 360, Accelaration 16, Stopping 1.0, AutoBreak Y, Radius 0.3, Height 1.2 

using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

public class AICharacterControl2_Route : UdonSharpBehaviour
{
    public UnityEngine.AI.NavMeshAgent agent { get; private set; }
    public ThirdPersonCharacter2 character { get; private set; }

    // 変更できるパラメーター
    [SerializeField] public GameObject _route_object; // ルートの親オブジェクト
    [SerializeField, Range(0f, 5f)] float _agent_speed = 3.0f;    // 目標を追いかけるスピード
    [SerializeField] AudioSource _ashioto_sound;    // 足音のサウンド（任意）

    int _selected = 0;  // 選択中の目標

    private void Start()
    {
        agent = GetComponentInChildren<UnityEngine.AI.NavMeshAgent>();
        character = GetComponent<ThirdPersonCharacter2>();
        agent.updateRotation = false;
        agent.updatePosition = true;
        agent.speed = _agent_speed; // NavMeshで設定した値は上書きされる
        //agent.stoppingDistance = 1.0f;

        // 最初の目標を設定する
        if (_route_object != null)
        {
            set_next_target(0);
        }
        else
        {
            agent.SetDestination(agent.transform.position);
            character.Move2(Vector3.zero, 0);
        }
    }

    private void Update()
    {
        if (_route_object != null)
        {
            if (agent.remainingDistance > agent.stoppingDistance)
            {
                // 目標までの距離がある場合
                character.Move2(agent.desiredVelocity, 0);
                ashioto(true);
            }
            else
            {   // 目標に到達したら次の目標へ向かう
                _selected++;
                if (_selected >= _route_object.transform.childCount) _selected = 0;
                set_next_target(_selected);
                ashioto(false);
            }
        }
    }

    // 目標を設定する
    private void set_next_target(int no)
    {
        if (_route_object.transform.childCount > 0)
        {
            if (_route_object.transform.GetChild(no) != null)
            {
                agent.SetDestination(_route_object.transform.GetChild(no).transform.position);
            }
        }
    }

    // 足音を鳴らす
    private void ashioto(bool newstat)
    {
        if (_ashioto_sound != null)
        {
            if (newstat && !_ashioto_sound.isPlaying)
            {
                _ashioto_sound.pitch = (agent.speed / 3.0f);    // サウンドは速さ3.0を基準で計算
                _ashioto_sound.Play();
            }
            else if (!newstat && _ashioto_sound.isPlaying)
            {
                _ashioto_sound.Stop();
            }
        }
    }
}
