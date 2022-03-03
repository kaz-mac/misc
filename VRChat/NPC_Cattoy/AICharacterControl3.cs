//
// 移動先の指定、目標の位置による動作の決定、視線追従
//
// 参考 NavMesh設定
// Base 0, Speed 3, Angluer 360, Accelaration 16, Stopping 1.0, AutoBreak Y, Radius 0.3, Height 1.2 

using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

public class AICharacterControl3 : UdonSharpBehaviour
{
    public UnityEngine.AI.NavMeshAgent agent { get; private set; }
    public ThirdPersonCharacter3 character { get; private set; }
    public Animator animator { get { return GetComponent<Animator>(); } }

    // オブジェクトの指定
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

    // その他
    [SerializeField, Range(0f, 5f)] float agent_speed_far = 3.0f; // 目標を追いかけるスピード
    [SerializeField, Range(0f, 30f)] float rusk_timaout = 10.0f;   // ボールを渡すのを諦める時間
    [SerializeField] float houchi_max = 30f;   // 放置されたら帰る時間

    // なでなで
    [SerializeField] public Transform _nadePosition;  // なでなでの位置オブジェクト（必須）
    [SerializeField, Range(0f, 0.5f)] float nadenade_distance = 0.2f; // なでなでの最大距離
    [SerializeField, Range(0f, 3f)] float nadenade_time = 2.5f;   // 一度のなでなでで有効になる時間

    // 座標情報
    [SerializeField] public Transform _rusk_spawn_point; // ラスクちゃんのスポーン地点
    [SerializeField] public Transform _rusk_home_point; // ラスクちゃんのホーム地点
    [SerializeField] public Transform _ball_spawn_point; // ボールのスポーン地点

    // 同期する変数
    [UdonSynced(UdonSyncMode.None)] public bool nadenaded = false;  // なでなでしてる

    // 視線追従の設定（0～1.0）
    float ikall = 1.00f;
    float ikbody = 0.36f;
    float ikhead = 0.55f;
    float ikeye = 1.0f;
    float ikmotion = 1.0f;

    // 変数
    private int state = 0;  // 現在の状態（0=待機、1=追いかける、2=拾う、3=持ってくる、4=渡す、5=配達完了）
    private int selected = 0;  // 選択中の目標
    private Vector3 targetpos;  // 目標の座標
    private float remain = 0f;
    private float naderemain = 0f;
    private float nadeweight = 0f;
    private float wdctimer = 0f;
    private bool wdc_on = false;
    private bool exec_once = false;
    private bool targetik = true;
    private float houchitime;

    // 初期設定
    private void Start()
    {
        agent = GetComponentInChildren<UnityEngine.AI.NavMeshAgent>();
        character = GetComponent<ThirdPersonCharacter3>();
        agent.updateRotation = false;
        agent.updatePosition = true;
    }

    // フレームごとの処理
    private void Update()
    {
        GameObject ball;    // 追いかける対象のボール
        UdonBehaviour udon; // そのボールに付いてるUdon
        VRCPlayerApi owner; // そのボールのオーナー

        WDCcheck();
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
                            state++;
                            exec_once = true;
                        }
                    }
                }
                // 目標が未設定のときはその場で停止
                if (selected == 0)
                {
                    // なでた場合は7に飛ぶ
                    if (CheckNadenade())
                    {
                        nadeweight = 0f;
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
                    character.Emote(0, 0f);    // 表情変更 なし
                    ashioto(false);
                }
                break;

            //【追いかける】
            case 3:
                // 投げたボールのところまで取りに行く
                ball = _tgroot.transform.GetChild(selected - 1).gameObject;
                targetpos = ball.transform.position;
                udon = (UdonBehaviour)ball.GetComponent(typeof(UdonBehaviour));
                if (udon != null && !(bool)udon.GetProgramVariable("ispick"))   // ボールから手を離したら
                {
                    character.Emote(1, 100f);    // 表情変更 目
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
                    float kweight = (((rusk_timaout - 0.5f) - remain) / 0.3f) * 100f;
                    if (kweight > 100f) kweight = 100f;
                    character.Emote(2, kweight);    // 表情変更 口
                    targetik = true;    // 視線追従開始
                }
                // なでるか、ボールを受け取るか、スイッチがOFFか、一定時間経ったら抜ける
                if (CheckNadenade() || (udon != null && (bool)udon.GetProgramVariable("ispick")) || !_switch.activeSelf || remain < 0f)
                {
                    _myita.SetActive(false); // 見えない板をオフ
                    ball.GetComponent<Rigidbody>().AddForce(Vector3.down);  // ボールが落ちないバグ対策
                    character.Emote(0, 0f);    // 表情変更 なし
                    character.Move3(Vector3.zero, 0);   // アニメーション停止
                    nadeweight = 0f;
                    state++;
                }
                break;

            //【配達完了＆なでなで】
            case 7:
                if (CheckNadenade())  //（なでるとnade=trueが2秒維持される）
                {
                    // なでなでした場合
                    nadeweight += (Time.deltaTime / 0.2f) * 100f;
                    if (nadeweight > 100f) nadeweight = 100f;
                    character.Emote(3, nadeweight);    // 表情変更 ニコニコ
                }
                else
                {
                    // なで終わった場合、またはなで以外の場合
                    if (nadeweight > 0f)
                    {
                        nadeweight -= (Time.deltaTime / 0.15f) * 100f;
                        if (nadeweight < 0f) nadeweight = 0f;
                        character.Emote(3, nadeweight);    // 表情変更 ニコニコ　戻す
                    }
                    if (nadeweight <= 0f)
                    {
                        remain = 0.2f;
                        state++;
                    }
                }
                break;

            //【終了】
            case 8:
                remain -= Time.deltaTime;
                if (remain < 0f)
                {
                    selected = 0;
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
                    state = 0;  // 初期状態に移行
                }
                break;
        }
    }

    // 目標が視界内の一定距離 & 高さにあるか？（0=範囲外 1=高い位置 2=低い位置）
    private int check_target_near()
    {
        int nyaa = 0;
        Vector3 eyeDir = this.transform.forward; // プレイヤーの視線ベクトル
        Vector3 playerPos = this.transform.position; // プレイヤーの位置
        Vector3 targetPos = _tgroot.transform.GetChild(selected-1).transform.position;    // 目標の位置（遅延なし）
        float height = targetPos.y - playerPos.y;   // 目標の高さ
        eyeDir.y = 0;
        playerPos.y = 0;
        targetPos.y = 0;
        float angle = Vector3.Angle((targetPos - playerPos).normalized, eyeDir);
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
    private void OnAnimatorIK(int layerIndex)
    {
        if (!targetik) return;
        Vector3 lookpos = Vector3.zero;
        float ikbody_new = ikbody;
        float ikhead_new = ikhead;
        if (selected > 0 && state >= 2 && state <= 3) // 拾いに行くまで
        {
            // ボールを見る
            if (_tgroot.transform.GetChild(selected - 1) != null && agent.remainingDistance < 3.0f)  //目標がセットされ距離が3m以内
                lookpos = _tgroot.transform.GetChild(selected - 1).transform.position;
        }
        if (selected > 0 && state >= 6 && state <= 8) // 渡すとき、なでなで、終了
        {
            // ボールを投げた人(Owner)の頭を見る
            GameObject  ball = _tgroot.transform.GetChild(selected - 1).gameObject;
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
        if (lookpos != Vector3.zero)
        {
            //animator.SetLookAtWeight(ikall, ikbody_new, ikhead, ikeye, ikmotion); // 全体,体,頭,目,モーション
            animator.SetLookAtWeight(Soft(ikall,0), Soft(ikbody_new,1), Soft(ikhead_new, 2), Soft(ikeye,3), Soft(ikmotion,4));
            animator.SetLookAtPosition(lookpos);
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
            float add = (Mathf.Abs(sa) < 0.05f) ? -sa : Time.deltaTime / pm;
            if (Mathf.Abs(add) > Mathf.Abs(sa)) add = sa;
            newf = lastfloats[idx] + add; 
            if (newf > 1f) newf = 1f;
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
            distance_r = Vector3.Distance(Networking.LocalPlayer.GetBonePosition(HumanBodyBones.RightHand), _nadePosition.position);
            distance_l = Vector3.Distance(Networking.LocalPlayer.GetBonePosition(HumanBodyBones.LeftHand), _nadePosition.position);
            if (Mathf.Min(distance_r, distance_l) < nadenade_distance) naderemain = nadenade_time;
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
        state = 0;
        targetik = true;

        character.Emote(0, 0f);    // 表情変更 なし
        character.Move3(Vector3.zero, 0);   // アニメーション停止
        agent.transform.position = _rusk_home_point.position;  // おうちに瞬間移動
        agent.SetDestination(agent.transform.position);
        ashioto(false);

        for (var i = 0; i < _tgroot.transform.childCount; i++)
        {
            GameObject ball = _tgroot.transform.GetChild(i).gameObject;
            if (ball != null && ball.activeSelf)
            {
                UdonBehaviour udon = (UdonBehaviour)ball.GetComponent(typeof(UdonBehaviour));
                if (udon != null) {
                    udon.SendCustomEvent("BallReset");    // ボール状態リセット
                }
            }
        }
    }


}
