

//*
//A pilha tem uma característica importante — ela é dividida em duas zonas:
//┌─────────────────────────┐
//│      ImGui Layer        │  ← Overlays(índice alto)
//│      Debug Layer        │  ← Overlays ficam sempre no topo
//├─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─┤  ← m_LayerInsertIndex
//│      Editor Layer       │  ← Layers normais
//│      Game Layer         │  ← Layers normais(índice baixo)
//└─────────────────────────┘
//Layers normais são inseridas abaixo do índice.Overlays são sempre inseridos no topo.O ImGui é um overlay — ele sempre renderiza por cima de tudo. *//


#pragma once

#include "axe/core/types.hpp"
#include "layer.hpp"
#include <vector>

namespace axe
{
	class AXE_API LayerStack
	{
	public:
		LayerStack();
			~LayerStack();

			//PushLayer - insere na zona de layers normais
			void PushLayer(Layer* layer);

			// PushOverlay - insere sempre no topo da pilha
			void PushOverlay(Layer* overlay);

			//PopLayer / PopOverlay - remove da pilha (não deleta)
			void PopLayer(Layer* layer);
			void PopOverlay(Layer* overlay);

			//Iteradores - permitem usar LayerStack num range-for
			//begin/end normal = ordem de baixo para cima (update/render)
			std::vector<Layer*>::iterator begin() { return m_Layers.begin(); }
			std::vector<Layer*>::iterator end() { return m_Layers.end(); }

			//Iteradores reversos - ordem de cima para baixo (eventos)
			//Eventos chegam primeiro nas layers do topo 
			std::vector<Layer*>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
			std::vector<Layer*>::reverse_iterator rend() { return m_Layers.rend(); }

	private:
		std::vector<Layer*> m_Layers;
		uint32_t m_LayerInsertIndex = 0;
		//std::vector<Layer*>::iterator m_LayerInsertIndex;
	};
}

