#pragma once
#include "axe/core/types.hpp"

#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/renderer/cube_renderer.hpp"

namespace axe
{
;
class CubeRenderer;

class AXE_API ViewportRenderer
	{
		
	public:
		void Initialize();
		void RenderToFramebuffer(Framebuffer& framebuffer, std::uint32_t width, std::uint32_t height, float timeSeconds);

	private:
		
		std::unique_ptr<CubeRenderer> m_CubeRenderer;
	};
}