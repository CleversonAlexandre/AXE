#include "anim_parameters.hpp"

namespace axe
{
	void AnimParameters::SetFloat(const std::string& name, float v)
	{
		auto& p = m_Values[name];
		p.Type = AnimParamType::Float;
		p.F = v;
	}

	void AnimParameters::SetInt(const std::string& name, int v)
	{
		auto& p = m_Values[name];
		p.Type = AnimParamType::Int;
		p.I = v;
	}

	void AnimParameters::SetBool(const std::string& name, bool v)
	{
		auto& p = m_Values[name];
		p.Type = AnimParamType::Bool;
		p.B = v;
	}

	void AnimParameters::SetTrigger(const std::string& name)
	{
		auto& p = m_Values[name];
		p.Type = AnimParamType::Trigger;
		p.B = true;
	}

	void AnimParameters::ConsumeTrigger(const std::string& name)
	{
		auto it = m_Values.find(name);
		if (it != m_Values.end() && it->second.Type == AnimParamType::Trigger)
			it->second.B = false;
	}

	float AnimParameters::GetFloat(const std::string& name) const
	{
		auto it = m_Values.find(name);
		if (it == m_Values.end())
			return 0.0f;

		// Conversão implícita: gameplay pode ter setado um Int e a condição
		// comparar como float. Devolver 0 nesse caso seria um bug silencioso.
		switch (it->second.Type)
		{
		case AnimParamType::Float:   return it->second.F;
		case AnimParamType::Int:     return static_cast<float>(it->second.I);
		case AnimParamType::Bool:
		case AnimParamType::Trigger: return it->second.B ? 1.0f : 0.0f;
		}

		return 0.0f;
	}

	int AnimParameters::GetInt(const std::string& name) const
	{
		auto it = m_Values.find(name);
		if (it == m_Values.end())
			return 0;

		switch (it->second.Type)
		{
		case AnimParamType::Int:     return it->second.I;
		case AnimParamType::Float:   return static_cast<int>(it->second.F);
		case AnimParamType::Bool:
		case AnimParamType::Trigger: return it->second.B ? 1 : 0;
		}

		return 0;
	}

	bool AnimParameters::GetBool(const std::string& name) const
	{
		auto it = m_Values.find(name);
		if (it == m_Values.end())
			return false;

		switch (it->second.Type)
		{
		case AnimParamType::Bool:
		case AnimParamType::Trigger: return it->second.B;
		case AnimParamType::Float:   return it->second.F != 0.0f;
		case AnimParamType::Int:     return it->second.I != 0;
		}

		return false;
	}

	bool AnimParameters::IsTriggerSet(const std::string& name) const
	{
		auto it = m_Values.find(name);
		return it != m_Values.end()
			&& it->second.Type == AnimParamType::Trigger
			&& it->second.B;
	}

	bool AnimParameters::Has(const std::string& name) const
	{
		return m_Values.find(name) != m_Values.end();
	}

	AnimParamType AnimParameters::GetType(const std::string& name) const
	{
		auto it = m_Values.find(name);
		return it != m_Values.end() ? it->second.Type : AnimParamType::Float;
	}


} // namespace axe
