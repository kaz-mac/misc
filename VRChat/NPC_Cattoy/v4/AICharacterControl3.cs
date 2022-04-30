//
// 移動先の指定、目標の位置による動作の決定、視線追従
//
// 参考 NavMesh設定
// Base 0, Speed 3, Angluer 360, Accelaration 16, Stopping 1.0, AutoBreak Y, Radius 0.3, Height 1.2 
//
// 2022.04.18 ポテチ食べる追加

using UdonSharp;
using UnityEngine;
using UnityEngine.UI;
using VRC.SDKBase;
using VRC.Udon;

public class AICharacterControl3 : UdonSharpBehaviour
{
    public UnityEngine.AI.NavMeshAgent agent { get; private set; }
    public ThirdPersonCharacter3 character { get; private set; }
    public Animator animator { get { return GetComponent<Animator>(); } }

    // オブジェクトの指定
    [SerializeField] public bool debug = false; // デバッグモード
    [SerializeField] public GameObject _tgroot; // 目標の親オブジェクト
    [SerializeField] public GameObject _myball;  // 手元のビーチボールのオブジェクト（必須）
    [SerializeField] public GameObject _myita;  // 手元の見えない板のオブジェクト（必須）
    [SerializeField] public AudioSource _ashioto_sound;    // 足音のサウンド（任意）
    [SerializeField] public GameObject _switch;  // スイッチのオブジェクト（必須）

    // 変更できるパラメーター
    [SerializeField, Range(0f, 2f)] float nyaa_low = 0.37f;   // ボールを拾う高さ・下
    [SerializeField, Range(0f, 2f)] float nyaa_distance = 0.5f;   // ボールを拾う最大距離（正面）

    // ボールを追いかける距離
    [SerializeField, Range(0f, 2f)] float bstop_distance = 0.5f;   // 止まる距離
    [SerializeField, Range(0f, 2f)] float brun_distance = 1.0f;   // 走り始める距離
    // ボールを持った人を追いかける距離
    [SerializeField, Range(0f, 2f)] float ostop_distance = 1.5f;   // 止まる距離
    [SerializeField, Range(0f, 2f)] float orun_distance = 3.0f;   // 走り始める距離
    // ポテチを追いかける距離
    [SerializeField, Range(0f, 2f)] float pstop_distance = 0.8f;   // 止まる距離
    [SerializeField, Range(0f, 2f)] float prun_distance = 1.6f;   // 走り始める距離

    // その他
    [SerializeField, Range(0f, 5f)] float agent_speed_far = 3.0f; // 目標を追いかけるスピード
    [SerializeField, Range(0f, 30f)] float rusk_timaout = 10.0f;   // ボールを渡すのを諦める時間
    [SerializeField] float houchi_max = 30f;   // 放置されたら帰る時間

    // なでなで
    [SerializeField] public Transform _nadePosition;  // なでなでの位置オブジェクト（必須）
    [SerializeField, Range(0f, 0.5f)] float nadenade_distance = 0.2f; // なでなでの最大距離
    [SerializeField, Range(0f, 3f)] float nadenade_time = 2.0f;   // 一度のなでなでで有効になる時間

    // 座標情報
    [SerializeField] public Transform _rusk_spawn_point; // ラスクちゃんのスポーン地点
    [SerializeField] public Transform _rusk_home_point; // ラスクちゃんのホーム地点
    [SerializeField] public Transform _ball_spawn_point; // ボールのスポーン地点
    [SerializeField] public Transform _rusk_eat_point; // ラスクちゃんのもぐもぐ地点

    // ポテチ
    [SerializeField] public GameObject[] _ObonRoots; // ヨドコロちゃんポテトチップスの最上位オブジェクト

    // 同期する変数
    [UdonSynced(UdonSyncMode.None)] public bool nadenaded = false;  // なでなでしてる

    // 視線追従の設定（0～1.0）
    float ikall = 1.00f;
    float ikbody = 0.36f;
    float ikhead = 0.55f;
    float ikeye = 1.0f;
    float ikmotion = 1.0f;

    // 変数
    private int state = 0;  // 現在の状態
    private int selected = 0;   // 選択中の目標 (ボール) 0=なし、1スタート
    private int ctable = -1;  // 選択中のポテチがあるテーブル -1=なし、0スタート
    private int ctable_return = -1; // 関数の戻り値をグローバル変数で受け取る用
    private int cselected = -1;  // 選択中の目標 (ポテチ) -1=なし、0スタート
    private int effidx = 0;  // ポテチに対応したエフェクト番号 -1=なし、0スタート
    private Vector3 targetpos;  // 目標の座標
    private float remain = 0f;  // 待機時間の残り
    private float naderemain = 0f;  // なでなでニコニコの残り時間
    private float wdctimer = 0f;    // Watch Dog Timer
    private bool wdc_on = false;    // WDC有効
    private bool exec_once = false; // 次のループで1回だけ実行フラグ
    private bool targetik = true;   // 視線追従有効
    private float houchitime;   // 放置された時間
    private float mogupoint;    // もぐもぐ好感度
    private float mogupoint_div = 7f;    // もぐもぐ好感度減衰度　7秒待機で1回分の好感度低下
    private float turnremain = 0f;  // 回転の残り角度

    // デバッグ
    GameObject debugcanvas;
    Text debugtext;

    // 初期設定
    private void Start()
    {
        // ラスクちゃん
        agent = GetComponentInChildren<UnityEngine.AI.NavMeshAgent>();
        character = GetComponent<ThirdPersonCharacter3>();
        agent.updateRotation = false;
        agent.updatePosition = true;
        // デバッグ用
        debugcanvas = GameObject.Find("Text_Debug");
        if (debugcanvas != null)
        {
            if (debug) debugtext = debugcanvas.GetComponent<Text>();
            else debugcanvas.SetActive(false);
        }
    }

    // デバッグ用表示
    private void showDebugText(string text)
    {
        if (debug && debugtext != null)
        {
            debugtext.text = "---DEBUG MODE---\n" + text;
            //Debug.Log(text);
        }
    }

    // フレームごとの処理
    private void Update()
    {
        GameObject ball;    // 追いかける対象のボール
        UdonBehaviour udon; // そのボールに付いてるUdon
        VRCPlayerApi owner; // そのボールのオーナー
        GameObject yodoroot;
        GameObject yodoeffects, yodochips, effobj, chips;

        WDCcheck();
        showDebugText("state=" + state + "\nmogupoint=" + mogupoint);
        switch (state)
        {
            //【スイッチOFF時】
            case 0:
                if (_switch.activeSelf) // スイッチ押したら
                {
                    // ボールを出現する
                    for (var i = 0; i < _tgroot.transform.childCount; i++)
                    {
                        ball = _tgroot.transform.GetChild(i).gameObject;
                        if (ball != null)
                        {
                            udon = (UdonBehaviour)ball.GetComponent(typeof(UdonBehaviour));
                            if (udon != null) udon.SendCustomEvent("BallShow");    // ボール表示
                            Vector3 pos = _ball_spawn_point.position;
                            pos.x += 0.4f * i;
                            ball.transform.position = pos;
                        }
                    }
                    agent.transform.position = _rusk_spawn_point.position;  // スポーン地点に瞬間移動
                    foreach (var obon in _ObonRoots) obon.SetActive(true);   // ポテチ皿表示
                    state++;
                    exec_once = true;
                }
                break;

            //【スイッチON時】
            case 1:
                // ラスクちゃんをホームポジションまで誘導する
                agent.SetDestination(_rusk_home_point.position);   // 目標をセット
                agent.stoppingDistance = bstop_distance;   //0.5
                agent.speed = agent_speed_far;
                if (exec_once)
                {
                    exec_once = false;
                    break;  // SetDestination()直後はagent.remainingDistanceがすぐに計算されないので一旦戻る
                }
                // 到着判定 
                if (agent.remainingDistance > agent.stoppingDistance)
                {
                    // 目標までの距離がある場合
                    character.Move3(agent.desiredVelocity, 0);
                    ashioto(true);
                }
                else
                {   // 停止範囲内に入った場合
                    ashioto(false);
                    character.Move3(Vector3.zero, 0);
                    houchitime = 0f;
                    state++;
                }
                break;

            //【待機】
            case 2:
                // 一度アイテムを持ったらそれを目標にする、離しても目標のままでいる
                for (var i = 0; i < _tgroot.transform.childCount; i++)
                {
                    ball = _tgroot.transform.GetChild(i).gameObject;
                    if (ball != null && ball.activeSelf)
                    {
                        udon = (UdonBehaviour)ball.GetComponent(typeof(UdonBehaviour));
                        if (udon != null && (bool)udon.GetProgramVariable("ispick"))    // ボールを持ったら
                        {
                            selected = i + 1;
                            state++;    // ボールを追いかけるモード
                            exec_once = true;
                        }
                    }
                }
                if (selected == 0)
                {
                    cselected = scan_potechi_all();    // ポテチをスキャン
                    ctable = ctable_return;
                    if (ctable >= 0 && cselected >= 0) // ポテチを持ったら
                    {
                        character.Emote(2);    // 表情変更 口
                        state = 10;    // ポテチを追いかけるモード
                        exec_once = true;
                        turnremain = 0f;
                    }
                }

                // 目標が未設定のときはその場で停止
                if (selected == 0 && cselected == -1)
                {
                    // なでた場合は7に飛ぶ
                    if (CheckNadenade())
                    {
                        state = 7;
                        break;
                    }
                    // スイッチOFFした場合
                    if (!_switch.activeSelf)
                    {
                        state = 9; // 帰るモードに移行
                        exec_once = true;
                        break;
                    }
                    // 30秒以上放置されたら帰る
                    houchitime += Time.deltaTime;
                    if (houchitime >= houchi_max)
                    {
                        state = 9; // 帰るモードに移行
                        exec_once = true;
                        break;
                    }
                    agent.SetDestination(agent.transform.position);
                    character.Move3(Vector3.zero, 0);
                    character.Emote(0);    // 表情変更 なし
                    ashioto(false);
                }
                mogupoint -= Time.deltaTime / mogupoint_div;   // 好感度減衰
                if (mogupoint < 0f) mogupoint = 0f;
                break;

            //【追いかける】
            case 3:
                // 投げたボールのところまで取りに行く
                ball = _tgroot.transform.GetChild(selected - 1).gameObject;
                targetpos = ball.transform.position;
                udon = (UdonBehaviour)ball.GetComponent(typeof(UdonBehaviour));
                if (udon != null && !(bool)udon.GetProgramVariable("ispick"))   // ボールから手を離したら
                {
                    character.Emote(1);    // 表情変更 目
                    agent.SetDestination(targetpos);   // 目標をセット
                    agent.stoppingDistance = bstop_distance;   //0.5
                    agent.speed = agent_speed_far;
                    // 到着判定 
                    if (agent.remainingDistance > agent.stoppingDistance)
                    {
                        // 目標までの距離がある場合
                        character.Move3(agent.desiredVelocity, 0);
                        ashioto(true);
                    }
                    else
                    {   // 停止範囲内に入った場合
                        ashioto(false);
                        if (check_target_near() == 2)
                        {
                            udon.SendCustomEvent("BallStop");
                            udon.SendCustomEvent("BallHide");
                            state++;
                            character.Move3(Vector3.zero, 1);   // 拾うアニメーション
                            remain = 1.1f;
                            WDCstop();
                        }
                        else
                        {
                            character.Move3(Vector3.zero, 0);
                        }
                    }
                }
                else if (udon != null && (bool)udon.GetProgramVariable("ispick"))   // ボールをまだ持ってたら
                {
                    // ラスクちゃんのオブジェクトのオーナーを変更する（変数同期のため）
                    owner = Networking.GetOwner(ball);
                    if (!Networking.IsOwner(owner, this.gameObject))
                    {
                        Networking.SetOwner(owner, this.gameObject); // ボールを持った人をオーナーにする
                    }
                    // ボールを離すまでの間はちょっと近づく
                    if (owner != null)
                        targetpos = owner.GetTrackingData(VRCPlayerApi.TrackingDataType.Head).position; // オーナーの頭位置
                    agent.SetDestination(targetpos);   // 目標をセット
                    if (exec_once)
                    {
                        agent.stoppingDistance = ostop_distance;  //1.5;
                        exec_once = false;
                        break;
                    }
                    WDCstart(30f);
                    // 到着判定 
                    if (agent.remainingDistance > agent.stoppingDistance)
                    {
                        // 目標までの距離がある場合
                        character.Move3(agent.desiredVelocity, 0);
                        ashioto(true);
                    }
                    else
                    {   // 停止範囲内に入った場合、ちょっと近づいて離れたらまた近づく
                        ashioto(false);
                        character.Move3(Vector3.zero, 0);   // アニメーション停止
                        agent.speed = agent_speed_far;
                        if (agent.stoppingDistance == ostop_distance && agent.remainingDistance <= ostop_distance + 0.2f)
                            agent.stoppingDistance = orun_distance;   //3.0
                        else if (agent.stoppingDistance == orun_distance && agent.remainingDistance > orun_distance - 0.2f)
                            agent.stoppingDistance = ostop_distance;  //1.5
                        else if (agent.stoppingDistance != ostop_distance && agent.stoppingDistance != orun_distance)
                            agent.stoppingDistance = orun_distance;   //3.0
                    }
                }
                break;

            //【拾う】
            case 4:
                // ボールを拾うアニメーション、表示切替
                remain -= Time.deltaTime;
                if (remain < 0f)
                {
                    state++;
                    exec_once = true;
                    WDCstart(30f);
                }
                break;

            //【持ってくる】
            case 5:
                // ボールを投げた人(Owner)のところに戻ってくる
                ball = _tgroot.transform.GetChild(selected - 1).gameObject;
                owner = Networking.GetOwner(ball);
                if (owner != null)
                    targetpos = owner.GetTrackingData(VRCPlayerApi.TrackingDataType.Head).position; // オーナーの頭位置
                agent.SetDestination(targetpos);   // 目標をセット
                agent.stoppingDistance = brun_distance;
                agent.speed = agent_speed_far;
                if (exec_once)
                {
                    exec_once = false;
                    break;
                }
                // 到着判定 
                if (agent.remainingDistance > agent.stoppingDistance)
                {
                    // 目標までの距離がある場合
                    character.Move3(agent.desiredVelocity, 2);  // 持ってるアニメーション
                    _myball.SetActive(true);
                    ashioto(true);
                }
                else
                {   // 停止範囲内に入った場合
                    character.Move3(Vector3.zero, 3);   // 渡すアニメーション（ボール非表示状態）
                    targetik = false;    // 視線追従無効
                    ResetOldLookAtWeight(new int[] { 1, 2 });
                    _myita.SetActive(true); // 見えない板をオン
                    ball.transform.position = _myball.transform.position;
                    ashioto(false);
                    remain = rusk_timaout;
                    state++;
                    exec_once = true;
                    WDCstop();
                }
                break;

            //【渡す】
            case 6:
                // ボールの位置をアニメーションのボールに合わせて差し替える
                remain -= Time.deltaTime;
                ball = _tgroot.transform.GetChild(selected - 1).gameObject;
                udon = (UdonBehaviour)ball.GetComponent(typeof(UdonBehaviour));
                if (exec_once && remain < rusk_timaout - (43f / 60f))   // 43コマ目=手を伸ばし切った位置で実行
                {
                    if (udon != null)
                    {
                        ball.transform.position = _myball.transform.position;
                        ball.transform.rotation = _myball.transform.rotation;
                        udon.SendCustomEvent("BallStop");
                        udon.SendCustomEvent("BallShow");
                    }
                    exec_once = false;
                    break;
                }
                // 0.5秒経過後から口を開ける
                if (remain < rusk_timaout - 0.5f)
                {
                    character.Emote(2);    // 表情変更 口
                    targetik = true;    // 視線追従開始
                }
                // なでるか、ボールを受け取るか、スイッチがOFFか、一定時間経ったか、ポテチを持ったら抜ける
                if (!exec_once)
                {
                    CheckNadenade();    // なでなでされてるか？
                    bool exitflag = nadenaded;
                    if (udon != null && (bool)udon.GetProgramVariable("ispick")) exitflag = true;
                    if (!_switch.activeSelf) exitflag = true;
                    if (remain < 0f) exitflag = true;
                    if (!exitflag)
                    {
                        int tmpcselected = scan_potechi_all();    // ポテチをスキャン
                        if (ctable_return >= 0 && tmpcselected >= 0) exitflag = true;
                    }
                    if (exitflag)
                    {
                        _myita.SetActive(false); // 見えない板をオフ
                        ball.GetComponent<Rigidbody>().AddForce(Vector3.down);  // ボールが落ちないバグ対策
                        character.Emote(0);    // 表情変更 なし
                        character.Move3(Vector3.zero, 0);   // アニメーション停止
                        remain = 0f;
                        state++;
                    }
                }
                break;

            //【配達完了＆なでなで】
            case 7:
                CheckNadenade();    // なでなでされてるか？
                if (nadenaded)  // なでなでしてる間（なでるとnade=trueが2秒維持される）
                {
                    character.Emote(3);    // 表情変更 ニコニコ
                    remain = 0.2f;
                }
                else // なで終わった場合 or なでてない場合
                {
                    character.Emote(0); // 表情変更 目口戻す
                    state++;
                }
                break;

            //【任務終了】
            case 8:
                remain -= Time.deltaTime;
                if (remain <= 0f)
                {
                    selected = 0;
                    cselected = -1;
                    ctable = -1;
                    houchitime = 0f;
                    state = 2;  // 待機モードへ移行
                }
                break;

            //【おうちに帰る】
            case 9:
                // ラスクちゃんをおうちまで誘導する
                agent.SetDestination(_rusk_spawn_point.position);   // 目標をセット
                agent.stoppingDistance = bstop_distance;   //0.5
                agent.speed = agent_speed_far;
                if (exec_once)
                {
                    exec_once = false;
                    break;
                }
                // 到着判定 
                if (agent.remainingDistance > agent.stoppingDistance)
                {
                    // 目標までの距離がある場合
                    character.Move3(agent.desiredVelocity, 0);
                    ashioto(true);
                }
                else
                {   // 停止範囲内に入った場合
                    ashioto(false);
                    character.Move3(Vector3.zero, 0);
                    // 放置されて戻った場合は終了せず待機状態(2)に移行
                    if (_switch.activeSelf)
                    {
                        selected = 0;
                        cselected = -1;
                        ctable = -1;
                        houchitime = 0f;
                        state = 2;  // 待機モードへ移行
                        break;
                    }
                    // ボールを消す
                    for (var i = 0; i < _tgroot.transform.childCount; i++)
                    {
                        ball = _tgroot.transform.GetChild(i).gameObject;
                        if (ball != null)
                        {
                            udon = (UdonBehaviour)ball.GetComponent(typeof(UdonBehaviour));
                            if (udon != null) udon.SendCustomEvent("BallHide");    // ボール非表示
                        }
                    }
                    foreach (var obon in _ObonRoots) obon.SetActive(false);   // ポテチち皿非表示
                    state = 0;  // 初期状態に移行
                }
                break;

            //【追いかける】ポテチ
            case 10:
                // ポテチの近くに行く
                if (ctable >=0 &&  cselected >= 0)
                {
                    bool ispick = get_potechi_picked(ctable, cselected);    // ポテチを掴んでるか？
                    showDebugText("state=" + state + "\nmogupoint=" + mogupoint + " pick=" + ispick + " ctable=" + ctable + " cselected=" + cselected);
                    yodoroot = _ObonRoots[ctable].transform.Find("Yodo_PotatoChips").gameObject;
                    if (yodoroot == null) break;
                    yodochips = yodoroot.transform.Find("Chips").gameObject;
                    if (yodochips == null) break;
                    if (!ispick)   // ポテチから手を離したら
                    {
                        WDCstop();
                        turnremain = 0f;
                        int effidx = get_potechi_effidx(ctable, cselected); // ポテチの対応エフェクト
                        if (effidx >= 0)
                        {
                            yodoeffects = yodoroot.transform.Find("Effects").gameObject;
                            if (yodoeffects == null) break;
                            effobj = yodoeffects.transform.GetChild(effidx).gameObject;
                            if (effobj != null)
                            {
                                float distance = Vector3.Distance(_rusk_eat_point.transform.position, effobj.transform.position);
                                if (distance < 0.2) // 口の前で離した場合
                                {
                                    // ポテチを掴んだ本人なら同期指示を出す（アニメーション再生）
                                    if (Networking.LocalPlayer == null) break;
                                    if (Networking.IsOwner(Networking.LocalPlayer, yodochips.transform.GetChild(cselected).gameObject))
                                    {
                                        state++;
                                        remain = 99f;   // dummy
                                        if (mogupoint > 2.0f) // 好感度によって表示アニメーションを切り替える
                                            SendCustomNetworkEvent(VRC.Udon.Common.Interfaces.NetworkEventTarget.All, nameof(update_state11_B)); // 同期
                                        else
                                            SendCustomNetworkEvent(VRC.Udon.Common.Interfaces.NetworkEventTarget.All, nameof(update_state11_A)); // 同期
                                    }
                                    break;
                                }
                                else // 口の前以外で離した場合
                                {
                                    character.Emote(0); // 表情変更 目口戻す
                                    character.Move3(Vector3.zero, 0);
                                    remain = 0.2f;
                                    state = 8;  // 終了へ
                                }
                            }
                        }
                        else
                        {
                            character.Emote(0); // 表情変更 目口戻す
                            character.Move3(Vector3.zero, 0);
                            remain = 0.2f;
                            state = 8;  // 終了へ
                        }
                    }
                    else   // ポテチをまだ持ってたら
                    {
                        // ポテチを離すまでの間はちょっと近づく
                        chips = yodochips.transform.GetChild(cselected).gameObject;
                        if (chips == null) break;
                        targetpos = chips.transform.position;
                        agent.SetDestination(targetpos);   // 目標をセット
                        if (exec_once)
                        {
                            agent.stoppingDistance = pstop_distance;  //0.5;
                            exec_once = false;
                            break;
                        }
                        WDCstart(30f);
                        // 到着判定 
                        if (agent.remainingDistance > agent.stoppingDistance)
                        {
                            // 目標までの距離がある場合
                            character.Move3(agent.desiredVelocity, 0);
                            ashioto(true);
                        }
                        else
                        {   // 停止範囲内に入った場合、ちょっと近づいて離れたらまた近づく
                            ashioto(false);
                            character.Move3(Vector3.zero, 0);   // アニメーション停止
                            agent.speed = agent_speed_far;
                            if (agent.stoppingDistance == pstop_distance && agent.remainingDistance <= pstop_distance + 0.2f)
                                agent.stoppingDistance = prun_distance;   //1.6
                            else if (agent.stoppingDistance == prun_distance && agent.remainingDistance > prun_distance - 0.2f)
                                agent.stoppingDistance = pstop_distance;  //0.8
                            else if (agent.stoppingDistance != pstop_distance && agent.stoppingDistance != brun_distance)
                                agent.stoppingDistance = prun_distance;   //1.6
                            // 目標まで90°以上ずれ且つ50cm以上離れていたら方向を回転する
                            float tgtangle = get2DSignedAngle(this.transform.position, targetpos);
                            float tgtdistance = get2DDistance(this.transform.position, targetpos);
                            if (turnremain == 0f && Mathf.Abs(tgtangle) >= 90f && tgtdistance > 0.5f) turnremain = -tgtangle;
                            if (turnremain != 0f)
                            {
                                float descangle = 0f;
                                float yy = this.transform.rotation.y;
                                if (Mathf.Abs(turnremain) < 10f) descangle = -turnremain;
                                else descangle = (turnremain < 0f) ? 10f : -10f;
                                this.transform.Rotate(0, descangle, 0);
                                turnremain += descangle;
                                if (Mathf.Abs(turnremain) < 1f) turnremain = 0f;   // 念のため（精度誤差の無限ループ防止） 
                            }
                        }
                    }
                }
                mogupoint -= Time.deltaTime / mogupoint_div;   // 好感度減衰
                if (mogupoint < 0f) mogupoint = 0f;
                break;

            //【食べる】
            case 11:
                // ポテチを食べるアニメーションが終わるまで待つ
                remain -= Time.deltaTime;
                agent.enabled = false;  // アニメーション再生中の微動防止
                if (remain < 0f)
                {
                    character.Move3(Vector3.zero, 0);
                    remain = 0.2f;
                    state = 8;  // 終了へ
                    agent.enabled = true;
                }
                break;
        }
    }

    // 同期対応 state=11 への遷移（SendCustomNetworkEvent()から呼ばれる）
    public void update_state11_A() { update_state11(0); }
    public void update_state11_B() { update_state11(1); }
    private void update_state11(int moguselect)
    {
        mogupoint += 1f;
        WDCstop();
        switch (moguselect) {
            case 0: // おいちい
                remain = 7.0f - 0.1f;
                break;
            case 1: // うんまー！
                mogupoint = 0f;
                remain = 5.5f - 0.1f;
                break;
            default:
                remain = 0f;
                break;
        }
        character.PresetAnimationValue("MoguSelect", moguselect);   // アニメ事前選択
        if (state == 2 || state == 10 || state == 11)
        {
            state = 11;
            //agent.SetDestination(agent.transform.position); // その場に留まる
            character.Emote(0); // 表情変更 目口戻す
            character.Move3(Vector3.zero, 4);   // 食べるアニメーション
        }
    }

    // 掴んだポテチのテーブルと番号を返す（foundtable,cselected=0スタート、-1は未選択）
    private int scan_potechi_all()
    {
        int no = -1;
        int foundtable = -1;
        for (var j = 0; j < _ObonRoots.Length; j++)
        {
            int no_tmp = scan_potechi(j);
            if (no_tmp >= 0)
            {
                no = no_tmp;
                foundtable = j;
                break;
            }
        }
        ctable_return = foundtable; // UdonSharpはoutもタプルも使えないのでグローバル変数で渡す
        return no;
    }
    // 掴んだポテチの番号を返す
    private int scan_potechi(int tbl)
    {
        int no = -1;
        GameObject yodoroot = _ObonRoots[tbl].transform.Find("Yodo_PotatoChips").gameObject;
        if (yodoroot == null) return -1;
        GameObject yodochips = yodoroot.transform.Find("Chips").gameObject;
        if (yodochips == null) return -1;
        //float distance = 999f;
        for (var i = 0; i < yodochips.transform.childCount; i++)
        {
            GameObject chips = yodochips.transform.GetChild(i).gameObject;
            if (chips != null)
            {
                UdonBehaviour udon = (UdonBehaviour)chips.GetComponent(typeof(UdonBehaviour));
                if (udon != null && (bool)udon.GetProgramVariable("ispick"))    // ポテチを持ったら
                {
                    //float tmpdistance = Vector3.Distance(this.transform.position, chips.transform.position);
                    //if (tmpdistance < distance)
                    //{
                        no = i;
                    //    distance = tmpdistance;
                    //}
                    break;
                }
            }
        }
        return no;
    }

    // 特定のポテチを掴んでいるか状態を返す
    private bool get_potechi_picked(int tbl, int no)
    {
        bool pick = false;
        GameObject yodoroot = _ObonRoots[tbl].transform.Find("Yodo_PotatoChips").gameObject;
        if (yodoroot == null) return false;
        GameObject yodochips = yodoroot.transform.Find("Chips").gameObject;
        if (yodochips == null) return false;
        if (no < 0) return false;
        GameObject chips = yodochips.transform.GetChild(no).gameObject;
        if (chips != null)
        {
            UdonBehaviour udon = (UdonBehaviour)chips.GetComponent(typeof(UdonBehaviour));
            if (udon != null)
            {
                pick = (bool)udon.GetProgramVariable("ispick");
            }
        }
        return pick;
    }

    // 掴んだ後のポテチに対応するエフェクト番号を返す
    private int get_potechi_effidx(int tbl, int no)
    {
        int idx = -1;
        if (tbl < 0 || no < 0) return -1;
        GameObject yodoroot = _ObonRoots[tbl].transform.Find("Yodo_PotatoChips").gameObject;
        if (yodoroot == null) return -1;
        GameObject yodochips = yodoroot.transform.Find("Chips").gameObject;
        if (yodochips == null) return -1;
        GameObject chips = yodochips.transform.GetChild(no).gameObject;
        if (chips != null)
        {
            UdonBehaviour udon = (UdonBehaviour)chips.GetComponent(typeof(UdonBehaviour));
            if (udon != null)
            {
                idx = (int)udon.GetProgramVariable("effidx");
            }
        }
        return idx;
    }

    // 正面方向から見た任意点の角度（上から見た2D）
    private float get2DSignedAngle(Vector3 homepos, Vector3 targetpos)
    {
        targetpos.y = 0f;
        homepos.y = 0f;
        Vector3 targetdir = targetpos - homepos;
        Vector3 homefwddir = this.transform.forward;
        homefwddir.y = 0f;
        float angle = Vector3.Angle(homefwddir, targetdir.normalized);
        if (Vector3.Cross(homefwddir, targetdir).y < 0) angle = -angle;
        return angle;
    }

    // 2点間の距離（上から見た2D）
    private float get2DDistance(Vector3 homepos, Vector3 targetpos)
    {
        targetpos.y = 0f;
        homepos.y = 0f;
        Vector3 targetdir = targetpos - homepos;
        Vector3 ruskfwddir = this.transform.forward;
        ruskfwddir.y = 0f;
        float distance = Vector3.Distance(homepos, targetpos);
        return distance;
    }

    // 目標が視界内の一定距離 & 高さにあるか？（0=範囲外 1=高い位置 2=低い位置）
    private int check_target_near()
    {
        int nyaa = 0;
        Vector3 eyeDir = this.transform.forward; // ラスクちゃんの視線ベクトル
        Vector3 playerPos = this.transform.position; // ラスクちゃんの位置
        Vector3 targetPos = _tgroot.transform.GetChild(selected - 1).transform.position;    // 目標の位置（遅延なし版）
        float height = targetPos.y - playerPos.y;   // 目標の高さ
        eyeDir.y = 0;
        playerPos.y = 0;
        targetPos.y = 0;
        float distance = Vector3.Distance(playerPos, targetPos);
        if (distance < nyaa_distance && height <= nyaa_low) nyaa = 2;   // 低い位置にある
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
    Vector3 last_lookpos = Vector3.zero;
    private void OnAnimatorIK(int layerIndex)
    {
        if (!targetik) return;
        Vector3 lookpos = Vector3.zero;
        float ikbody_new = ikbody;
        float ikhead_new = ikhead;
        if (selected > 0 && state >= 2 && state <= 3) // ボールを拾いに行くまで
        {
            // ボールを見る
            if (_tgroot.transform.GetChild(selected - 1) != null && agent.remainingDistance < 3.0f)  //目標がセットされ距離が3m以内
                lookpos = _tgroot.transform.GetChild(selected - 1).transform.position;
        }
        if (selected > 0 && state >= 6 && state <= 8) // ボールを渡すとき、なでなで、終了
        {
            // ボールを投げた人(Owner)の頭を見る
            GameObject ball = _tgroot.transform.GetChild(selected - 1).gameObject;
            VRCPlayerApi owner = Networking.GetOwner(ball);
            if (owner != null)
                lookpos = owner.GetTrackingData(VRCPlayerApi.TrackingDataType.Head).position; // オーナーの頭位置
            ikbody_new = 0.1f;  // 体は曲げない
            if (state == 8) // 終了時（なでなで有効時間内）
            {
                ikbody_new = 0f;
                ikhead_new = 0f;
            }
        }
        if (ctable >= 0 && cselected >= 0 && state == 10) // ポテチを持ったとき
        {

            GameObject yodoroot = _ObonRoots[ctable].transform.Find("Yodo_PotatoChips").gameObject;
            if (yodoroot == null) return;
            GameObject yodochips = yodoroot.transform.Find("Chips").gameObject;
            if (yodochips == null) return;
            GameObject chips = yodochips.transform.GetChild(cselected).gameObject;
            if (chips != null)
            {
                UdonBehaviour udon = (UdonBehaviour)chips.GetComponent(typeof(UdonBehaviour));
                if (udon != null)
                {
                    if ((bool)udon.GetProgramVariable("ispick")) // ポテチを持っている間
                    {
                        // ポテチを見る
                        if (Vector3.Distance(this.transform.position, chips.transform.position) < 3.0f)  // 3m以内
                        {
                            lookpos = chips.transform.position;
                            lookpos.y += 0.05f;  // 目線少し上
                        }
                    }
                    else // ポテチを離した直後
                    {
                        // ポテチを離した場所を見る
                        int effidx = (int)udon.GetProgramVariable("effidx");
                        if (effidx >= 0)
                        {
                            GameObject yodoeffects = yodoroot.transform.Find("Effects").gameObject;
                            if (yodoeffects == null) return;
                            GameObject effobj = yodoeffects.transform.GetChild(effidx).gameObject;
                            if (effobj != null && Vector3.Distance(this.transform.position, effobj.transform.position) < 3.0f)  // 3m以内
                            {
                                lookpos = effobj.transform.position;
                                lookpos.y += 0.05f;  // 目線少し上
                            }
                        }
                    }
                }
            }
        }
        // 向く方向と強さの指示
        if (lookpos != Vector3.zero)
        {
            // ボールやポテチを見る
            //animator.SetLookAtWeight(ikall, ikbody_new, ikhead, ikeye, ikmotion); // 全体,体,頭,目,モーション
            animator.SetLookAtWeight(Soft(ikall, 0), Soft(ikbody_new, 1), Soft(ikhead_new, 2), Soft(ikeye, 3), Soft(ikmotion, 4));
            animator.SetLookAtPosition(lookpos);
            last_lookpos = lookpos;
        }
        else
        {
            // 向きを無効（ゆっくり戻す）
            animator.SetLookAtWeight(Soft(0f, 0), Soft(0f, 1), Soft(0f, 2), Soft(0f, 3), Soft(0f, 4));
            animator.SetLookAtPosition(last_lookpos);
        }
    }

    // 切り替え時の急激な視線変更を緩和させる（これもっと簡単なやり方ないんかな？）
    private float[] lastfloats = new float[5];
    private void ResetOldLookAtWeight(int[] idxs)
    {
        foreach (int idx in idxs) lastfloats[idx] = 0f;
    }
    private float Soft(float targetf, int idx)
    {
        float newf = 0f;
        if (lastfloats[idx] == targetf) newf = targetf;
        else
        {
            float sa = (lastfloats[idx] - targetf);
            float pm = (sa < 0) ? 1f : -1f;
            float step = Time.deltaTime / ((targetf == 0f) ? 3f : 1f);  // 無効化時はさらにゆっくり
            float add = (Mathf.Abs(sa) < 0.05f) ? -sa : step / pm;
            if (Mathf.Abs(add) > Mathf.Abs(sa)) add = sa;
            newf = lastfloats[idx] + add;
            if (newf > 1f) newf = 1f;
            if (newf < 0f) newf = 0f;
        }
        lastfloats[idx] = newf;
        return newf;
    }

    // なでなでされているかチェック
    private bool CheckNadenade()
    {
        float distance_r;
        float distance_l;
        // ラスクちゃんのなでなでポイント（頭）とプレイヤーの手の距離を計算する
        if (selected > 0)   // ボールを投げた後は投げた人をチェックする
        {
            GameObject ball = _tgroot.transform.GetChild(selected - 1).gameObject;
            VRCPlayerApi owner = Networking.GetOwner(ball);
            if (owner != null)
            {
                distance_r = Vector3.Distance(owner.GetBonePosition(HumanBodyBones.RightHand), _nadePosition.position);
                distance_l = Vector3.Distance(owner.GetBonePosition(HumanBodyBones.LeftHand), _nadePosition.position);
                if (Mathf.Min(distance_r, distance_l) < nadenade_distance) naderemain = nadenade_time;
            }
        }
        else // それ以外はローカルユーザーを見る
        {
            if (Networking.LocalPlayer != null)
            {
                distance_r = Vector3.Distance(Networking.LocalPlayer.GetBonePosition(HumanBodyBones.RightHand), _nadePosition.position);
                distance_l = Vector3.Distance(Networking.LocalPlayer.GetBonePosition(HumanBodyBones.LeftHand), _nadePosition.position);
                if (Mathf.Min(distance_r, distance_l) < nadenade_distance) naderemain = nadenade_time;
            }
        }

        // なでなでされたら一定時間有効にする
        if (naderemain > 0f)
        {
            naderemain -= Time.deltaTime;
            nadenaded = (naderemain > 0f);
        }
        return nadenaded;   // 同期変数
    }

    // Watch Dog(Cat?) Timer 行動不能に陥ったラスクちゃんを救助
    private void WDCcheck()
    {
        if (!wdc_on) return;
        wdctimer -= Time.deltaTime;
        if (wdctimer < 0f)
        {
            wdc_on = false;
            Debug.Log("[Watch Dog Timer] Auto Reset!!");
            Reset();
        }
    }
    private void WDCstart(float wdcsec)
    {
        wdc_on = true;
        wdctimer = wdcsec;
    }
    private void WDCstop()
    {
        wdc_on = false;
    }

    // リセット
    public void Reset()
    {
        selected = 0;
        cselected = -1;
        ctable = -1;
        state = 0;
        targetik = true;

        character.Emote(0);    // 表情変更 なし
        character.Move3(Vector3.zero, 0);   // アニメーション停止
        agent.transform.position = _rusk_home_point.position;  // おうちに瞬間移動
        agent.SetDestination(agent.transform.position); // その場に留まる
        ashioto(false);

        for (var i = 0; i < _tgroot.transform.childCount; i++)
        {
            GameObject ball = _tgroot.transform.GetChild(i).gameObject;
            if (ball != null && ball.activeSelf)
            {
                UdonBehaviour udon = (UdonBehaviour)ball.GetComponent(typeof(UdonBehaviour));
                if (udon != null)
                {
                    udon.SendCustomEvent("BallReset");    // ボール状態リセット
                }
            }
        }
    }

}
