#pragma once

#include "axe/core/types.hpp"
namespace axe
{

	class ViewportRenderer;

	class AXE_API HierarchyWindow
	{
	public:

		void SetViewportRenderer(ViewportRenderer* renderer)
		{
			m_ViewportRenderer = renderer;
		}

		void Draw();

		ViewportRenderer* m_ViewportRenderer = nullptr;

	private:
		
	};


}
