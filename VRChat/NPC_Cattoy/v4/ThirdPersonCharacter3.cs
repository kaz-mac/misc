//
// アニメーション・シェイプキーの制御
//
using UdonSharp;
using UnityEngine;
using VRC.SDKBase;
using VRC.Udon;

//[RequireComponent(typeof(UnityEngine.AI.NavMeshAgent))]
//[RequireComponent(typeof(Rigidbody))]
//[RequireComponent(typeof(Animator))]

public class ThirdPersonCharacter3 : UdonSharpBehaviour
{
	// 変更できるパラメーター
	[SerializeField] float MovingTurnSpeed = 720;
	[SerializeField] float StationaryTurnSpeed = 720;
	[SerializeField] float MoveSpeedMultiplier = 1f;
	[SerializeField] float AnimSpeedMultiplier = 1f;
	[SerializeField] private SkinnedMeshRenderer _Face; // シェイプキーが入ってるオブジェクト
	[SerializeField] bool debug;

	public UnityEngine.AI.NavMeshAgent agent { get; private set; }
	private Rigidbody _Rigidbody { get { return GetComponent<Rigidbody>(); } }
	private Animator _Animator { get { return GetComponent<Animator>(); } }

	// 設定
	const float GroundCheckDistance = 0.2f;

	// 変数
	float TurnAmount;
	private float ForwardAmount;
	int Ball;
	Vector3 GroundNormal;

	void Start()
	{
		_Rigidbody.constraints = RigidbodyConstraints.FreezeRotation;  // udonsharpエラー対策
	}

	// 定期的に実行（0.02秒毎）
	private void FixedUpdate()
	{
	}

	// 定期的に実行（フレーム毎）
	private void Update()
	{
		SoftUpdateAnimatorFloat();
	}

	// 移動アニメーションを更新
	public void Move3(Vector3 move, int ball)
	{
		if (move.magnitude > 1f) move.Normalize();
		move = transform.InverseTransformDirection(move);
		CheckGroundStatus();
		move = Vector3.ProjectOnPlane(move, GroundNormal);
		TurnAmount = Mathf.Atan2(move.x, move.z);
		ForwardAmount = move.z;
		ApplyExtraTurnRotation();
		Ball = ball;

		UpdateAnimator(move);
	}

	// アニメーションのオプション指定
	public void PresetAnimationValue(string paraname, int value)
    {
		switch (paraname)
        {
			case "MoguSelect" :
				_Animator.SetInteger(paraname, value);	// もぐもぐアニメーション A or B
				break;
		}
    }

	// アニメーターの更新
	void UpdateAnimator(Vector3 move)
	{
		SoftUpdateAnimatorFloat();  // _ForwardAmount,_TurnAmountを更新
		if (!debug)
		{
			_Animator.SetInteger("Ball", Ball);
		}
		_Animator.speed = (move.magnitude > 0) ? AnimSpeedMultiplier : 1f;
	}

	// アニメーター変数の更新を自動的に繰り返す（DampTimeを0.1にしてるのに0.1秒で完了しないのなぜ？）
	void SoftUpdateAnimatorFloat()
	{
		if (debug) return;
		if (Mathf.Abs(_Animator.GetFloat("Forward") - ForwardAmount) > 0.01f)
			_Animator.SetFloat("Forward", ForwardAmount, 0.1f, Time.deltaTime);
		if (Mathf.Abs(_Animator.GetFloat("Turn") - TurnAmount) > 0.01f)
			_Animator.SetFloat("Turn", TurnAmount, 0.1f, Time.deltaTime);
	}

	// 表情を更新する
	public void Emote(int place)
	{
		if (debug) return;
		switch (place)
		{
			case 0: // なし
				_Animator.SetInteger("SK_me", 0);
				_Animator.SetInteger("SK_kuchi", 0);
				_Animator.SetInteger("SK_hoppe", 0);
				break;
			case 1: // 目（しいたけ）
				_Animator.SetInteger("SK_me", 2);
				break;
			case 2: // 口（△）
				_Animator.SetInteger("SK_kuchi", 3);
				break;
			case 3: // 目（ニコニコ）口（ワ）
				_Animator.SetInteger("SK_me", 1);
				_Animator.SetInteger("SK_kuchi", 1);
				_Animator.SetInteger("SK_hoppe", 1);
				break;
		}
	}

	// 以降、よくわからん
	void ApplyExtraTurnRotation()
	{
		float turnSpeed = Mathf.Lerp(StationaryTurnSpeed, MovingTurnSpeed, ForwardAmount);
		transform.Rotate(0, TurnAmount * turnSpeed * Time.deltaTime, 0);
	}

	public void OnAnimatorMove()
	{
		if (Time.deltaTime > 0)
		{
			Vector3 v = (_Animator.deltaPosition * MoveSpeedMultiplier) / Time.deltaTime;
			v.y = _Rigidbody.velocity.y;
			_Rigidbody.velocity = v;
		}
	}

	void CheckGroundStatus()
	{
		RaycastHit hitInfo;
#if UNITY_EDITOR
		Debug.DrawLine(transform.position + (Vector3.up * 0.1f), transform.position + (Vector3.up * 0.1f) + (Vector3.down * GroundCheckDistance));
#endif
		if (Physics.Raycast(transform.position + (Vector3.up * 0.1f), Vector3.down, out hitInfo, GroundCheckDistance))
		{
			GroundNormal = hitInfo.normal;
			//m_IsGrounded = true;
			//m_Animator.applyRootMotion = true;
		}
		else
		{
			GroundNormal = Vector3.up;
			//m_IsGrounded = false;
			//m_Animator.applyRootMotion = false;
		}
	}
}
