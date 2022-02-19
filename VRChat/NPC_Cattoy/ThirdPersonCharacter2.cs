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

public class ThirdPersonCharacter2 : UdonSharpBehaviour
{
	// 変更できるパラメーター
	[SerializeField] float _MovingTurnSpeed = 720;
	[SerializeField] float _StationaryTurnSpeed = 720;
	[SerializeField] float _MoveSpeedMultiplier = 1f;
	[SerializeField] float _AnimSpeedMultiplier = 1f;
	[SerializeField] private SkinnedMeshRenderer m_Face; // シェイプキーが入ってるオブジェクト

	private Rigidbody m_Rigidbody { get { return GetComponent<Rigidbody>(); } }
	private Animator m_Animator { get { return GetComponent<Animator>(); } }

	// 設定
	const float _GroundCheckDistance = 0.2f;

	// 変数
	float _TurnAmount;
	float _ForwardAmount;
	int _Nyaa;
	Vector3 m_GroundNormal;
	float _Mabataki_Weight;
	float _pastsecm = 0.0f;
	float _nextsecm = 0.0f;
	float _Kuchi_Weight;
	float _pastseck = 0.0f;
	float _nextseck = 0.0f;
	bool _mabataki_on = true;
	float pastnyaatime = 0f;
	float nextnyaatime = 0f;
	int _nyaa_point = 0;

	// 使用するシェイプキー
	string _mabataki_shapeky = "blink"; // まばたき
	string _kuchiake_shapekey = "kuchi_pokan";	// ランダム口あけ
	string[] _nyaa1_shapekeys = new string[] { "kuchi_△_2", "kuchi_yaeba_×", "surprise", "shiitake" };	// じゃれるとき有効
	string[] _nyaa1_shapekeys_rev = new string[] { "kuchi_pokan" }; // じゃれるとき無効
	string[] _nyaa2_shapekeys = new string[] { "><", "kuchi_wa", "kuchi_smile_1" };   // 興奮時有効
	string[] _nyaa2_shapekeys_rev = new string[] { "kuchi_pokan" }; // 興奮時無効


	void Start()
	{
		m_Rigidbody.constraints = RigidbodyConstraints.FreezeRotation;  // udonsharpエラー対策
	}

	// 定期的に実行（0.02秒毎）
	private void FixedUpdate()
    {
		BlendShapeRandomMabataki();
		BlendShapeRandomKuchiake(_Nyaa);
	}

	// シェイプキーのインデックスを取り出す
	private int s2i(string shapekeyname)
    {
		int idx = m_Face.sharedMesh.GetBlendShapeIndex(shapekeyname);
		if (idx < 0) idx = 0;	// エラー対策
		return idx;
	}

	// ランダムまばたき
	private void BlendShapeRandomMabataki()
	{
		if (_mabataki_on)
		{
			_pastsecm += Time.deltaTime;
			if (_pastsecm > _nextsecm)
			{
				_nextsecm = Random.Range(4.0f, 7.5f);    // 4～7.5秒ごとにまばたきする
				_pastsecm = 0.0f;
			}
			if (_pastsecm < 0.2f)
			{
				if (_pastsecm < 0.1f)
				{
					_Mabataki_Weight += (Time.deltaTime / 0.1f) * 100f;
					if (_Mabataki_Weight > 99.9f) _Mabataki_Weight = 100f;
					m_Face.SetBlendShapeWeight(s2i(_mabataki_shapeky), _Mabataki_Weight);
				}
				else if (_pastsecm < 0.2f)
				{
					_Mabataki_Weight -= (Time.deltaTime / 0.1f) * 100f;
					if (_Mabataki_Weight < 0.01f) _Mabataki_Weight = 0.0f;
					m_Face.SetBlendShapeWeight(s2i(_mabataki_shapeky), _Mabataki_Weight);
				}
			}
		} else
        {
			m_Face.SetBlendShapeWeight(s2i(_mabataki_shapeky), 0f);
		}
	}

	// ランダム口あけ
	private void BlendShapeRandomKuchiake(int nyaa)
	{
		_pastseck += Time.deltaTime;
		if (nyaa != 0)
        {
			_nextseck = _pastseck + 5.0f;	// じゃれた後は次のあーんまで5秒おく
			return;
        }
		if (_pastseck > _nextseck)
		{
			_nextseck = Random.Range(10.0f, 20.0f);  // 10～20秒ごとにあーんする
			_pastseck = 0.0f;
		}
		if (_pastseck < 0.5f)
		{
			_Kuchi_Weight += (Time.deltaTime / 0.4f) * 100f;
			if (_Kuchi_Weight > 99.9f) _Kuchi_Weight = 100f;
			m_Face.SetBlendShapeWeight(s2i(_kuchiake_shapekey), _Kuchi_Weight);
		}
		else if (_pastseck > 2.0f && _pastseck <= 2.5f)
		{
			_Kuchi_Weight -= (Time.deltaTime / 0.4f) * 100f;
			if (_Kuchi_Weight < 0.01f) _Kuchi_Weight = 0f;
			m_Face.SetBlendShapeWeight(s2i(_kuchiake_shapekey), _Kuchi_Weight);
		}
	}

	// 移動アニメーションを更新
	public void Move2(Vector3 move, int nyaa)
	{
		if (move.magnitude > 1f) move.Normalize();
		move = transform.InverseTransformDirection(move);
		CheckGroundStatus();
		move = Vector3.ProjectOnPlane(move, m_GroundNormal);
		_TurnAmount = Mathf.Atan2(move.x, move.z);
		_ForwardAmount = move.z;
		ApplyExtraTurnRotation();
		_Nyaa = nyaa;

		UpdateAnimator(move);
	}
	
	// アニメーターの更新
	void UpdateAnimator(Vector3 move)
	{
		m_Animator.SetFloat("Forward", _ForwardAmount, 0.1f, Time.deltaTime);
		m_Animator.SetFloat("Turn", _TurnAmount, 0.1f, Time.deltaTime);
		m_Animator.SetInteger("Nyaa", _Nyaa);

		// じゃれるアニメーションの繰り返し間隔をランダムにする
		if (_Nyaa > 0)
        {
			pastnyaatime += Time.deltaTime;
			if (pastnyaatime > nextnyaatime)
            {
				pastnyaatime = 0f;
				nextnyaatime = Random.Range(0.5f, 2.0f);
				m_Animator.Play(m_Animator.GetCurrentAnimatorStateInfo(1).shortNameHash, 1, 0f); // Layer1、先頭に戻す 
			}
		}
		m_Animator.speed = (move.magnitude > 0) ? _AnimSpeedMultiplier : 1f;

		// じゃれた時の目と口のシェイプキーを変更する
		if (_Nyaa > 0)
		{
			_nyaa_point++;
			if (_nyaa_point < 1000)	// 目キラキラ☆
			{
				foreach (string name in _nyaa2_shapekeys) m_Face.SetBlendShapeWeight(s2i(name), 0f);
				foreach (string name in _nyaa1_shapekeys) m_Face.SetBlendShapeWeight(s2i(name), 100f);
				foreach (string name in _nyaa1_shapekeys_rev) m_Face.SetBlendShapeWeight(s2i(name), 0f);
				_mabataki_on = true;
			}
			else if (_nyaa_point < 1600)	// たくさん遊ぶと興奮＞＜
			{
				foreach (string name in _nyaa1_shapekeys) m_Face.SetBlendShapeWeight(s2i(name), 0f);
				foreach (string name in _nyaa2_shapekeys) m_Face.SetBlendShapeWeight(s2i(name), 100f);
				foreach (string name in _nyaa2_shapekeys_rev) m_Face.SetBlendShapeWeight(s2i(name), 0f);
				_mabataki_on = false;
			}
			else if (_nyaa_point >= 1600)
            {
				_nyaa_point = 0;
            }
		}
		else if (_Nyaa == 0)
		{
			foreach (string name in _nyaa1_shapekeys) m_Face.SetBlendShapeWeight(s2i(name), 0f);
			foreach (string name in _nyaa2_shapekeys) m_Face.SetBlendShapeWeight(s2i(name), 0f);
			_mabataki_on = true;
			_nyaa_point--;
			if (_nyaa_point < 0) _nyaa_point = 0;
		}
	}

	// 以降、よくわからん
	void ApplyExtraTurnRotation()
	{
		float turnSpeed = Mathf.Lerp(_StationaryTurnSpeed, _MovingTurnSpeed, _ForwardAmount);
		transform.Rotate(0, _TurnAmount * turnSpeed * Time.deltaTime, 0);
	}

	public void OnAnimatorMove()
	{
		if (Time.deltaTime > 0)
		{
			Vector3 v = (m_Animator.deltaPosition * _MoveSpeedMultiplier) / Time.deltaTime;
			v.y = m_Rigidbody.velocity.y;
			m_Rigidbody.velocity = v;
		}
	}
	
	void CheckGroundStatus()
	{
		RaycastHit hitInfo;
#if UNITY_EDITOR
		Debug.DrawLine(transform.position + (Vector3.up * 0.1f), transform.position + (Vector3.up * 0.1f) + (Vector3.down * _GroundCheckDistance));
#endif
		if (Physics.Raycast(transform.position + (Vector3.up * 0.1f), Vector3.down, out hitInfo, _GroundCheckDistance))
		{
			m_GroundNormal = hitInfo.normal;
			//m_IsGrounded = true;
			//m_Animator.applyRootMotion = true;
		}
		else
		{
			m_GroundNormal = Vector3.up;
			//m_IsGrounded = false;
			//m_Animator.applyRootMotion = false;
		}
	}
}
