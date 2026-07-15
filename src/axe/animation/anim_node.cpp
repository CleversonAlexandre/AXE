#include "anim_node.hpp"

namespace axe
{
	Pose& PosePool::Acquire(const Skeleton& skeleton)
	{
		if (m_Used >= m_Buffers.size())
			m_Buffers.push_back(std::make_unique<Pose>());

		Pose& p = *m_Buffers[m_Used++];

		// Redimensiona só quando muda de esqueleto — o caso comum é no-op.
		if (p.Size() != skeleton.GetBoneCount())
			p.Resize(skeleton.GetBoneCount());

		return p;
	}

	void AnimNode::EvalInput(AnimEvalContext& ctx, int pin, Pose& out) const
	{
		if (pin >= 0 && pin < (int)Inputs.size() && Inputs[pin])
		{
			Inputs[pin]->Evaluate(ctx, out);
			return;
		}

		// Pino solto -> bind pose. NUNCA deixar a pose vazia: o skin cache
		// leria ossos inexistentes e o personagem colapsaria. Um grafo em
		// construção, com metade dos links feitos, tem que continuar rodando.
		if (ctx.Skel)
			Pose::FromBindPose(*ctx.Skel, out);
	}

	void AnimNode::UpdateInput(AnimEvalContext& ctx, int pin) const
	{
		if (pin >= 0 && pin < (int)Inputs.size() && Inputs[pin])
			Inputs[pin]->Update(ctx);
	}

	// ── Pinos de dado ────────────────────────────────────────────────────────

	float AnimNode::ReadFloat(AnimEvalContext& ctx, int pin) const
	{
		if (pin < 0 || pin >= (int)DataInputs.size())
			return 0.0f;

		const AnimDataPin& p = DataInputs[pin];

		// Link tem precedência sobre o inline. É o que faz o campo editável do
		// pino "sumir" quando voce liga um fio nele.
		if (p.Link)
			return p.Link->EvaluateFloat(ctx);

		return p.InlineFloat;
	}

	bool AnimNode::ReadBool(AnimEvalContext& ctx, int pin) const
	{
		if (pin < 0 || pin >= (int)DataInputs.size())
			return false;

		const AnimDataPin& p = DataInputs[pin];

		if (p.Link)
			return p.Link->EvaluateBool(ctx);

		return p.InlineBool;
	}

	void AnimNode::AddFloatPin(const char* name, float inlineValue)
	{
		AnimDataPin p;
		p.Name = name;
		p.Type = AnimPinType::Float;
		p.InlineFloat = inlineValue;
		DataInputs.push_back(p);
	}

	void AnimNode::AddBoolPin(const char* name, bool inlineValue)
	{
		AnimDataPin p;
		p.Name = name;
		p.Type = AnimPinType::Bool;
		p.InlineBool = inlineValue;
		DataInputs.push_back(p);
	}

} // namespace axe
