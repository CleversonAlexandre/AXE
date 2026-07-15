#pragma once
#include "axe/core/types.hpp"

#include <string>
#include <unordered_map>

namespace axe
{
	enum class AnimParamType
	{
		Float,
		Int,
		Bool,

		// TRIGGER não é um bool. É um PULSO.
		//
		// Um bool "IsAttacking" fica ligado, e a transição que o observa
		// dispararia de novo a cada frame. Um trigger dispara UMA vez e é
		// consumido pela transição que o usou — o gameplay não precisa
		// lembrar de desligar a flag no frame seguinte.
		//
		// Esquecer essa distinção é um dos bugs mais chatos de state machine
		// feita à mão: o personagem entra em loop de ataque eterno.
		Trigger
	};

	struct AXE_API AnimParamValue
	{
		AnimParamType Type = AnimParamType::Float;

		float F = 0.0f;
		int   I = 0;
		bool  B = false;   // serve para Bool e para Trigger
	};

	// Blackboard do AnimGraph: os valores que o GAMEPLAY escreve e que as
	// transições LEEM.
	//
	// É a fronteira entre o Script Editor e a animação. O pawn não conhece
	// estados nem clipes — ele escreve "Speed = 320", "IsGrounded = false",
	// dispara "Attack", e o grafo decide o resto. É essa separação que
	// permite reeditar o AnimGraph sem tocar em uma linha de script.
	class AXE_API AnimParameters
	{
	public:
		void SetFloat(const std::string& name, float v);
		void SetInt(const std::string& name, int v);
		void SetBool(const std::string& name, bool v);
		void SetTrigger(const std::string& name);

		// Getters convertem entre tipos: se o gameplay setou um Int e a
		// condição compara como float, devolver 0 seria um bug silencioso.
		float GetFloat(const std::string& name) const;
		int   GetInt(const std::string& name) const;
		bool  GetBool(const std::string& name) const;
		bool  IsTriggerSet(const std::string& name) const;

		// Chamado SÓ quando a transição realmente dispara — nunca durante a
		// avaliação. Avaliar não pode consumir: duas transições podem checar
		// o mesmo trigger, e a primeira a olhar roubaria o pulso da que de
		// fato tem prioridade.
		void ConsumeTrigger(const std::string& name);

		bool Has(const std::string& name) const;
		AnimParamType GetType(const std::string& name) const;

		void Clear() { m_Values.clear(); }

		const std::unordered_map<std::string, AnimParamValue>& GetAll() const { return m_Values; }

	private:
		std::unordered_map<std::string, AnimParamValue> m_Values;
	};

} // namespace axe
