#pragma once

namespace axe
{
	// Timestep encapsula o delta time de forma segura.
	// Internamente armazena em segundos mas permite consultar
	// em milissegundos também — útil para debug e UI.

	class TimeStamp
	{
	public:
		//construtor recebe o tempo em segundos
		TimeStamp(float time = 0.0f) : m_Time(time) {}
		
		// Conversão implícita para float — permite usar Timestep
	   // diretamente onde um float é esperado:
	   // layer->OnUpdate(ts) funciona mesmo que OnUpdate receba float
		operator float() const { return m_Time; }

		float GetSeconds() const { return m_Time; }
		float GetMiliSeconds() const { return m_Time * 1000.0f; }

	private:
		float m_Time; //Sempre em segundos 
	};

	
}