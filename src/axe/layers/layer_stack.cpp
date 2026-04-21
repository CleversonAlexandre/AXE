
#include "layer_stack.hpp"
#include <algorithm>

namespace axe
{

	LayerStack::LayerStack() {
		m_LayerInsertIndex = m_Layers.begin();
	}

	LayerStack::~LayerStack()
	{
		//o LayerStack é o dono das layers - deleta todas ao ser destruido 
		for (Layer* layer : m_Layers)
		{
			layer->OnDetach();
			delete layer;
		}
	}
	void LayerStack::PushLayer(Layer* layer)
	{
	//	//Insere na posição do índice e avança o índice
	//	//Isso mantém layers normais abaixo dos overlays
		m_Layers.emplace(m_LayerInsertIndex, layer);				
		layer->OnAttach();
	}
	void LayerStack::PushOverlay(Layer* overlay)
	{
		// Overlay vai sempre para o final — não muda o índice
		m_Layers.emplace_back(overlay);
		overlay->OnAttach();
	}


	void LayerStack::PopLayer(Layer* layer)
	{
		auto it = std::find(m_Layers.begin(), m_LayerInsertIndex, layer);
		if (it != m_LayerInsertIndex)
		{
			layer->OnDetach();
			m_LayerInsertIndex = m_Layers.erase(it);
		}
	}

	void LayerStack::PopOverlay(Layer* overlay)
	{
		auto it = std::find(m_Layers.begin(), m_Layers.end(), overlay);

		if (it != m_Layers.end())
		{
			overlay->OnDetach();
			m_Layers.erase(it);
		}
	}
}
