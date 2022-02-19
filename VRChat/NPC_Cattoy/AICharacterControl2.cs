//
// 移動先の指定、目標の位置による動作の決定、視線追従
//
// 参考 NavMesh設定
// Base 0, Speed 3, Angluer 360, Accelaration 16, Stopping 1.0, AutoBreak Y, Radius 0.3, Height 1.2 

using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

public class AICharacterControl2 : UdonSharpBehaviour
{
    public UnityEngine.AI.NavMeshAgent agent { get; private set; }
    public ThirdPersonCharacter2 character { get; private set; }
    public Animator animator { get { return GetComponent<Animator>(); } }

    // オブジェクトの指定
    [SerializeField] public GameObject[] _target = new GameObject[9];  // 目標のオブジェクト（必須）
    [SerializeField] public AudioSource _ashioto_sound;    // 足音のサウンド（任意）

    // 変更できるパラメーター
    [SerializeField, Range(0f, 2f)] float _nyaa_high = 2.00f;  // じゃれる高さ・上
    [SerializeField, Range(0f, 2f)] float _nyaa_mid = 0.98f;   // じゃれる高さ・中
    [SerializeField, Range(0f, 2f)] float _nyaa_low = 0.37f;   // じゃれる高さ・下
    [SerializeField, Range(0f, 90f)] float _nyaa_angle = 30f;  // じゃれる角度（正面から）
    [SerializeField, Range(0f, 2f)] float _nyaa_distance = 0.5f;   // じゃれる最大距離（正面）
    [SerializeField, Range(0f, 2f)] float _nyaa_hdistance = 0.2f;   // じゃれる最大距離（頭付近）
    [SerializeField, Range(1,99)] int _delay = 15;   // 追従の遅延 1=0.02秒、15=0.3秒

    // 目標を追いかけるスピード
    [SerializeField, Range(0f, 5f)] float _agent_speed_item = 3.0f; // ねこじゃらしとぬいぐるみ
    [SerializeField, Range(0f, 5f)] float _agent_speed_roomba = 2.05f; // ルンバ

    // ルンバを追いかける距離
    [SerializeField, Range(0f, 2f)] float _roomba_stop = 0.5f;   // 止まる距離
    [SerializeField, Range(0f, 2f)] float _roomba_run = 1.2f;   // 走り始める距離


    // 視線追従の設定（0～1.0）
    float _ikall = 1.00f;
    float _ikall_default = 1.00f;
    float _ikbody = 0.36f;
    float _ikhead = 0.55f;
    float _ikeye = 1.0f;
    float _ikmotion = 1.0f;

    // 変数
    int _selected = 0;  // 選択中の目標
    int _next_select = -1;  // 次に選択する目標
    float _next_cooltime = 0f;    // 目標切り替え時のクールタイム
    Vector3[] _dtargetpos_hists = new Vector3[100];  // 遅延用 目標座標の履歴
    Vector3 _dtargetpos;    // 遅延後の目標座標

    // 初期設定
    private void Start()
    {
        agent = GetComponentInChildren<UnityEngine.AI.NavMeshAgent>();
        character = GetComponent<ThirdPersonCharacter2>();
        agent.updateRotation = false;
        agent.updatePosition = true;
    }

    // フレームごとの処理
    private void Update()
    {
        bool skip = false;

        // 有効な目標に切り替える
        if (_next_select == -1)
        {
            for (var i=0; i<_target.Length; i++)
            {
                if (_target[i] != null && _target[i].activeSelf)
                {
                    skip = true;
                    if (_selected != i + 1)
                    {
                        _next_select = i + 1;
                        _next_cooltime = (i == (_target.Length-1)) ? 0.7f : 0.2f;    // ルンバのときはクール時間長く
                    }
                    break;
                }
            }
            if (_next_select == -1 && !skip) _selected = 0;
        }
        else
        {
            // クールタイム
            _next_cooltime -= Time.deltaTime;
            if (_next_cooltime < 0f)
            {
                _selected = _next_select;
                _next_select = -1;
                fill_delay_target_buffer(_target[_selected-1].transform.position);
                _ikall = _ikall_default;
            }
            else
            {
                _ikall -= Time.deltaTime / 0.15f;   // 首が急に戻るのを防止する
                if (_ikall < 0f)
                {
                    _ikall = 0f;
                    _selected = 0;
                }
            }
        }

        // 移動先の指定、目標の位置による動作の決定
        if (_selected > 0)
        {
            // 目標をセット
            agent.SetDestination(_dtargetpos);
            if (agent.remainingDistance > agent.stoppingDistance)
            {
                // 目標までの距離がある場合
                character.Move2(agent.desiredVelocity, 0);
                ashioto(true);
            }
            else
            {   // 停止範囲内に入った場合
                int nyaa = check_target_near();
                character.Move2(Vector3.zero, nyaa);
                ashioto(false);
            }
        }
        else
        {
            // 目標が非アクティブの場合はその場で停止
            agent.SetDestination(agent.transform.position);
            character.Move2(Vector3.zero, 0);
            ashioto(false);
        }

        // 目標を追いかけるスピードの設定
        if (_selected > 0)
        {
            //debug_remain = agent.remainingDistance;
            //debug_stop = agent.stoppingDistance;
            if (_selected == 1)
            {
                // ねこじゃらしの追いかけ速度/距離
                agent.speed = _agent_speed_item;
                agent.stoppingDistance = 1.0f;
            }
            else if (_selected == 9)
            {
                // ルンバに近づいたら止まってしばらく見つめて、遠くに行ったら走り始める処理
                agent.speed = _agent_speed_roomba;
                if (agent.stoppingDistance == _roomba_stop && agent.remainingDistance <= _roomba_stop + 0.2f)
                {
                    agent.stoppingDistance = _roomba_run;   //2.5
                }
                else if (agent.stoppingDistance == _roomba_run && agent.remainingDistance > _roomba_run - 0.2f)
                {
                    agent.stoppingDistance = _roomba_stop;  //0.5
                }
                else if (agent.stoppingDistance != _roomba_stop && agent.stoppingDistance != _roomba_run)
                {
                    agent.stoppingDistance = _roomba_run;   //2.5
                }
            }
            else
            {
                // ぬいぐるみの追いかけ速度/距離
                agent.speed = _agent_speed_item;
                agent.stoppingDistance = 0.65f;
            }
        }
    }
    //    [SerializeField] private float debug_remain;
    //    [SerializeField] private float debug_stop;

    // 0.02秒毎の実行
    private void FixedUpdate()
    {
        delay_target(); // 目標の遅延処理
    }

    // 目標の遅延処理
    private void delay_target()
    {
        if (_selected > 0 && _target[_selected-1] != null)
        {
            _dtargetpos = _dtargetpos_hists[0]; // Shift()が使えないのなんで？
            for (var i = 1; i < _dtargetpos_hists.Length; i++)
            {
                _dtargetpos_hists[i - 1] = _dtargetpos_hists[i];
            }
            _dtargetpos_hists[_delay] = _target[_selected-1].transform.position;
            if (_dtargetpos == null) _dtargetpos = _target[_selected-1].transform.position;
        }
    }
    private void fill_delay_target_buffer(Vector3 pos)
    {
        for (var i = 0; i < _dtargetpos_hists.Length; i++)
        {
            _dtargetpos_hists[i] = pos;
        }
    }

    // 目標が視界内の一定距離 & 高さにあるか？（0=範囲外 1=高い位置 2=低い位置）
    private int check_target_near()
    {
        int nyaa = 0;
        Vector3 eyeDir = this.transform.forward; // プレイヤーの視線ベクトル
        Vector3 playerPos = this.transform.position; // プレイヤーの位置
        Vector3 targetPos = _target[_selected-1].transform.position;    // 目標の位置（遅延なし）
        //Vector3 targetPos = _dtargetpos;    // 目標の位置（遅延あり）
        float height = targetPos.y - playerPos.y;   // 目標の高さ
        eyeDir.y = 0;
        playerPos.y = 0;
        targetPos.y = 0;
        float angle = Vector3.Angle((targetPos - playerPos).normalized, eyeDir);
        float distance = Vector3.Distance(playerPos, targetPos);
        //Debug.Log("**** angle=" + angle + " / distance=" + distance + " / height=" + height);
        if (distance >= 0.1f && distance < _nyaa_distance && angle < _nyaa_angle)   // 正面にある場合
        {
            if (height > _nyaa_mid && height <= _nyaa_high) nyaa = 1;    // 高い位置にある
            else if (height > _nyaa_low && height <= _nyaa_mid) nyaa = 2;   // 低い位置にある
        }
        else if (height > _nyaa_mid && height <= _nyaa_high && distance < _nyaa_hdistance)   // 頭付近にある場合
        {
            nyaa = 1;
        }
        return nyaa;
    }

    // 足音を鳴らす
    private void ashioto(bool newstat)
    {
        if (_ashioto_sound != null)
        {
            if (newstat && !_ashioto_sound.isPlaying)
            {
                _ashioto_sound.pitch = (agent.speed / 3.0f);
                _ashioto_sound.Play();
            }
            else if (!newstat && _ashioto_sound.isPlaying)
            {
                _ashioto_sound.Stop();
            }
        }
    }

    // 視線追従
    private void OnAnimatorIK(int layerIndex)
    {
        if (_selected > 0 && _target[_selected-1] != null && _selected > 0 && agent.remainingDistance < 3.0f)  //目標がセットされ距離が3m以内
        {
            animator.SetLookAtWeight(_ikall, _ikbody, _ikhead, _ikeye, _ikmotion); // 全体,体,頭,目,モーション
            //animator.SetLookAtPosition(_target[_selected-1].transform.position);  // 遅延なしver
            animator.SetLookAtPosition(_dtargetpos);  // 遅延ありver
        }
    }
}
