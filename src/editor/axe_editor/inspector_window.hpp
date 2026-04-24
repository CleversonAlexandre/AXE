#pragma once
#include "axe/core/types.hpp"
namespace axe
{
	class ViewportRenderer;
	class AXE_API InspectorWindow
	{
	public:
		void SetViewportRenderer(ViewportRenderer* renderer)
		{
			m_ViewportRenderer = renderer;
		}
		void Draw();

	private:
		ViewportRenderer* m_ViewportRenderer = nullptr;
	};
}