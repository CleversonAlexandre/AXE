#include "anim_graph.hpp"
#include "axe/log/log.hpp"

#include <cmath>

namespace axe
{
	bool AnimCondition::Evaluate(const AnimParameters& params) const
	{
		switch (Op)
		{
		case AnimCompare::Greater:      return params.GetFloat(Parameter) > Value;
		case AnimCompare::GreaterEqual: return params.GetFloat(Parameter) >= Value;
		case AnimCompare::Less:         return params.GetFloat(Parameter) < Value;
		case AnimCompare::LessEqual:    return params.GetFloat(Parameter) <= Value;

			// Igualdade de float com tolerância. `x == 0.5f` exato quase
			// nunca é verdade quando o valor vem de uma conta de física —
			// uma condição assim ficaria "quebrada" sem nenhum erro visível.
		case AnimCompare::Equal:        return std::fabs(params.GetFloat(Parameter) - Value) < 1e-4f;
		case AnimCompare::NotEqual:     return std::fabs(params.GetFloat(Parameter) - Value) >= 1e-4f;

		case AnimCompare::IsTrue:       return params.GetBool(Parameter);
		case AnimCompare::IsFalse:      return !params.GetBool(Parameter);

		case AnimCompare::TriggerSet:   return params.IsTriggerSet(Parameter);
		}

		return false;
	}

	bool AnimTransition::ConditionsMet(const AnimParameters& params) const
	{
		// Lista vazia = sempre verdadeira. Combinado com HasExitTime, é como
		// se escreve "quando o clipe terminar, volte pro idle".
		for (const auto& c : Conditions)
			if (!c.Evaluate(params))
				return false;

		return true;
	}

	float AnimState::GetDuration(const AnimParameters& params) const
	{
		if (BlendSpace && !BlendSpace->IsEmpty())
		{
			// Num blend space a duração VARIA com o parâmetro: quanto mais
			// rápido o personagem, mais curto o ciclo da passada.
			return BlendSpace->GetDurationAt(params.GetFloat(BlendParameter));
		}

		return Clip ? Clip->GetDuration() : 0.0f;
	}

	int AnimGraph::AddState(const AnimState& state)
	{
		m_States.push_back(state);
		return static_cast<int>(m_States.size()) - 1;
	}

	int AnimGraph::FindState(const std::string& name) const
	{
		for (std::size_t i = 0; i < m_States.size(); ++i)
			if (m_States[i].Name == name)
				return static_cast<int>(i);

		return -1;
	}

	void AnimGraph::AddTransition(const AnimTransition& transition)
	{
		m_Transitions.push_back(transition);
	}

	bool AnimGraph::SetEntryState(const std::string& name)
	{
		const int i = FindState(name);
		if (i < 0)
			return false;

		m_EntryState = i;
		return true;
	}

	void AnimGraph::RemoveState(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_States.size()))
			return;

		// 1) Fora as transicoes que TOCAM este estado (entrando ou saindo).
		for (int t = static_cast<int>(m_Transitions.size()) - 1; t >= 0; --t)
		{
			const auto& tr = m_Transitions[t];
			if (tr.From == index || tr.To == index)
				m_Transitions.erase(m_Transitions.begin() + t);
		}

		m_States.erase(m_States.begin() + index);

		// 2) REINDEXA as que sobraram.
		//
		// Este passo e o que separa um grafo correto de um grafo
		// silenciosamente podre. Sem ele, remover o estado 2 faz toda
		// transicao que apontava pro 3 continuar apontando pro indice 3 —
		// que agora e OUTRO estado. O grafo continua "valido", carrega sem
		// erro, e o personagem simplesmente vai pro lugar errado.
		for (auto& tr : m_Transitions)
		{
			if (tr.From > index) --tr.From;   // From == -1 (Any State) fica intacto
			if (tr.To > index)   --tr.To;
		}

		if (m_EntryState == index)
			m_EntryState = m_States.empty() ? 0 : 0;
		else if (m_EntryState > index)
			--m_EntryState;
	}

	void AnimGraph::RemoveTransition(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_Transitions.size()))
			return;

		m_Transitions.erase(m_Transitions.begin() + index);
	}

	void AnimGraph::AddParameter(const AnimParamDecl& decl)
	{
		if (decl.Name.empty() || FindParameter(decl.Name) >= 0)
			return;   // nome vazio ou duplicado

		m_Parameters.push_back(decl);
	}

	void AnimGraph::RemoveParameter(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_Parameters.size()))
			return;

		m_Parameters.erase(m_Parameters.begin() + index);
	}

	int AnimGraph::FindParameter(const std::string& name) const
	{
		for (std::size_t i = 0; i < m_Parameters.size(); ++i)
			if (m_Parameters[i].Name == name)
				return static_cast<int>(i);

		return -1;
	}

	void AnimGraph::SeedParameters(AnimParameters& params) const
	{
		for (const auto& d : m_Parameters)
		{
			switch (d.Type)
			{
			case AnimParamType::Float:   params.SetFloat(d.Name, d.DefaultF); break;
			case AnimParamType::Int:     params.SetInt(d.Name, d.DefaultI);   break;
			case AnimParamType::Bool:    params.SetBool(d.Name, d.DefaultB);  break;

				// Trigger NAO recebe o default: um trigger "armado" no primeiro
				// frame dispararia sua transicao antes do gameplay existir.
				// Triggers nascem sempre desarmados.
			case AnimParamType::Trigger: break;
			}
		}
	}

	bool AnimGraph::Validate() const
	{
		bool ok = true;

		if (m_States.empty())
		{
			AXE_CORE_ERROR("AnimGraph '{}': nenhum estado.", m_Name);
			return false;
		}

		if (m_EntryState < 0 || m_EntryState >= static_cast<int>(m_States.size()))
		{
			AXE_CORE_ERROR("AnimGraph '{}': estado de entrada invalido ({}).", m_Name, m_EntryState);
			ok = false;
		}

		const int count = static_cast<int>(m_States.size());

		for (std::size_t t = 0; t < m_Transitions.size(); ++t)
		{
			const auto& tr = m_Transitions[t];

			// From == -1 é Any-State (legítimo). To == -1 nunca é.
			if (tr.From < -1 || tr.From >= count)
			{
				AXE_CORE_ERROR("AnimGraph '{}': transicao {} com From invalido ({}).", m_Name, t, tr.From);
				ok = false;
			}

			if (tr.To < 0 || tr.To >= count)
			{
				AXE_CORE_ERROR("AnimGraph '{}': transicao {} com To invalido ({}).", m_Name, t, tr.To);
				ok = false;
			}

			// Uma transição sem condição E sem exit time dispara no primeiro
			// frame, sempre — o estado de origem se torna inalcançável. É um
			// erro de autoria comum e silencioso; melhor gritar.
			if (tr.Conditions.empty() && !tr.HasExitTime && tr.From != -1)
			{
				AXE_CORE_WARN("AnimGraph '{}': transicao {} ({} -> {}) nao tem condicao nem exit time — "
					"vai disparar imediatamente e o estado de origem nunca sera visto.",
					m_Name, t,
					tr.From >= 0 && tr.From < count ? m_States[tr.From].Name : "?",
					tr.To >= 0 && tr.To < count ? m_States[tr.To].Name : "?");
			}
		}

		for (const auto& s : m_States)
		{
			if (!s.Clip && !s.BlendSpace)
			{
				AXE_CORE_WARN("AnimGraph '{}': estado '{}' nao toca nada (sem clipe e sem blend space) — "
					"vai renderizar em bind pose.", m_Name, s.Name);
			}

			if (s.BlendSpace && s.BlendParameter.empty())
			{
				AXE_CORE_WARN("AnimGraph '{}': estado '{}' tem blend space mas nenhum parametro o dirige — "
					"vai ficar preso no valor 0.", m_Name, s.Name);
			}
		}

		return ok;
	}

} // namespace axe
