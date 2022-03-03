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
	float mabataki_weight;
	float pastsecm = 0.0f;
	float nextsecm = 0.0f;
	float kuchi_weight;
	float pastseck = 0.0f;
	float nextseck = 0.0f;
	bool mabataki_on = true;
	bool kuchi_on = true;

	// 使用するシェイプキー
	string shapeky_mabataki = "blink"; // まばたき
	string shapekey_kuchiake = "kuchi_pokan";  // ランダム口あけ
	string[] shapekeys_me1 = new string[] { "surprise", "shiitake" };	// 目（しいたけ）
	string[] shapekeys_me1_rev = new string[] { };
	string[] shapekeys_kuchi1 = new string[] { "kuchi_△_2", "kuchi_yaeba_×" };	// 口（△）
	string[] shapekeys_kuchi1_rev = new string[] { "kuchi_pokan" };
	string[] shapekeys_nico = new string[] { "smile", "mayu_down", "kuchi_wa", "cheek" };   // 目（ニコニコ）口（ワ）
	string[] shapekeys_nico_rev = new string[] { "kuchi_pokan" };
	string[] shapekeys_ratio_name = new string[] { "mayu_down", "kuchi_wa" };    // シェイプキーの適用比率調整
	float[] shapekeys_ratio_value = new float[] { 0.5f, 0.8f };    // その倍率

	void Start()
	{
		_Rigidbody.constraints = RigidbodyConstraints.FreezeRotation;  // udonsharpエラー対策
	}

	// 定期的に実行（0.02秒毎）
	private void FixedUpdate()
	{
		BlendShapeRandomMabataki();
		BlendShapeRandomKuchiake(Ball);
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

	// アニメーターの更新
	void UpdateAnimator(Vector3 move)
	{
		SoftUpdateAnimatorFloat();  // _ForwardAmount,_TurnAmountを更新
		if (!debug) 
		{
			_Animator.SetInteger("Nyaa", Ball);
			_Animator.SetInteger("Ball", Ball); 
		}
		_Animator.speed = (move.magnitude > 0) ? AnimSpeedMultiplier : 1f;
	}

	// アニメーター変数の更新を自動的に繰り返す（DampTimeを0.1にしてるのに0.1秒で完了しないのなぜ？）
	void SoftUpdateAnimatorFloat()
	{
		if (!debug)
		{
			if (Mathf.Abs(_Animator.GetFloat("Forward") - ForwardAmount) > 0.01f)
				_Animator.SetFloat("Forward", ForwardAmount, 0.1f, Time.deltaTime);
			if (Mathf.Abs(_Animator.GetFloat("Turn") - TurnAmount) > 0.01f)
				_Animator.SetFloat("Turn", TurnAmount, 0.1f, Time.deltaTime);
		}
	}

	// 表情を更新する
	public void Emote(int place, float weight)
	{
		mabataki_on = true;
		kuchi_on = true;
		switch (place)
		{
			case 0: // なし
				foreach (string name in shapekeys_me1) SetShapekey(name, 0f);
				foreach (string name in shapekeys_kuchi1) SetShapekey(name, 0f);
				foreach (string name in shapekeys_nico) SetShapekey(name, 0f);
				break;
			case 1: // 目（しいたけ）
				foreach (string name in shapekeys_me1) SetShapekey(name, weight);
				foreach (string name in shapekeys_me1_rev) SetShapekey(name, 0f);
				break;
			case 2: // 口（△）
				foreach (string name in shapekeys_kuchi1) SetShapekey(name, weight);
				foreach (string name in shapekeys_kuchi1_rev) SetShapekey(name, 0f);
				kuchi_on = false;
				break;
			case 3: // 目（ニコニコ）口（ワ）
				foreach (string name in shapekeys_nico) SetShapekey(name, weight);
				foreach (string name in shapekeys_nico_rev) SetShapekey(name, 0f);
				mabataki_on = (weight <= 0f);
				kuchi_on = false;
				break;
		}
	}

	// シェイプキーを設定する
	private void SetShapekey(string shapekeyname, float weight)
	{
		int idx = _Face.sharedMesh.GetBlendShapeIndex(shapekeyname);
		if (idx >= 0)
		{
			for (var i = 0; i < shapekeys_ratio_name.Length; i++)
				if (shapekeyname == shapekeys_ratio_name[i]) weight *= shapekeys_ratio_value[i];
			_Face.SetBlendShapeWeight(idx, weight);
		}
	}

	// ランダムまばたき
	private void BlendShapeRandomMabataki()
	{
		if (mabataki_on)
		{
			pastsecm += Time.deltaTime;
			if (pastsecm > nextsecm)
			{
				nextsecm = Random.Range(4.0f, 7.5f);    // 4～7.5秒ごとにまばたきする
				pastsecm = 0.0f;
			}
			if (pastsecm < 0.2f)
			{
				if (pastsecm < 0.1f)
				{
					mabataki_weight += (Time.deltaTime / 0.1f) * 100f;
					if (mabataki_weight > 99.9f) mabataki_weight = 100f;
					SetShapekey(shapeky_mabataki, mabataki_weight);
				}
				else if (pastsecm < 0.2f)
				{
					mabataki_weight -= (Time.deltaTime / 0.1f) * 100f;
					if (mabataki_weight < 0.01f) mabataki_weight = 0.0f;
					SetShapekey(shapeky_mabataki, mabataki_weight);
				}
			}
		}
		else
		{
			SetShapekey(shapeky_mabataki, 0f);
		}
	}

	// ランダム口あけ
	private void BlendShapeRandomKuchiake(int ball)
	{
		pastseck += Time.deltaTime;
		if (! kuchi_on)
		{
			nextseck = pastseck + 8.0f;   // 他で口を開いた後は次のあーんまで時間をおく
			return;
		}
		if (pastseck > nextseck)
		{
			nextseck = Random.Range(10.0f, 20.0f);  // 10～20秒ごとにあーんする
			pastseck = 0.0f;
		}
		if (pastseck < 0.5f)
		{
			kuchi_weight += (Time.deltaTime / 0.4f) * 100f;
			if (kuchi_weight > 99.9f) kuchi_weight = 100f;
			SetShapekey(shapekey_kuchiake, kuchi_weight);
		}
		else if (pastseck > 2.0f && pastseck <= 2.5f)
		{
			kuchi_weight -= (Time.deltaTime / 0.4f) * 100f;
			if (kuchi_weight < 0.01f) kuchi_weight = 0f;
			SetShapekey(shapekey_kuchiake, kuchi_weight);
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
