#pragma once
#include "axe/core/types.hpp"
#include "axe/events/event.hpp"
#include <string>


/*Layer Stack Cada layer recebe eventos atualizados e renderiza. */
/*Se a layer do ImGui consumir um cliqeu do mouse (porque o ussário clicou num borão da UI),*/
/*ela marca o eventoo como Handled = true e as layers abaixo não recebem esse evento*/
namespace axe
{
	class AXE_API Layer
	{
	public:
		//Ajuda a identificar qual layer está causado problema nos logs
		Layer(const std::string& name = "Layer")
			: m_DebugName(name) {}

		virtual ~Layer() = default;

		//Chamado uma vez quando a layer entra na pilha 
		//Inicializa recursis - shaders, textures .etc
		virtual void OnAttach() {}

		//Chamdo uma ve quando a layer sai da pilha
		//Libera recursos
		virtual void OnDetach() {}

		//Chamado todo frame - lógica de atualização
		//deltaTime é quando tempo passosu desde o último frame
		virtual void OnUpdate(float deltaTime) {}

		//Chamado todo frame - renderização
		virtual void OnRender() {}

		//Chamado quando um evento chega - retorna true se consumiu
		virtual void OnEvent(Event& e) {}

		//Só para debug - saber o nome da layer nos logs
		const std::string& GetName() const { return m_DebugName; }

	protected:
		std::string m_DebugName;

	};
}

