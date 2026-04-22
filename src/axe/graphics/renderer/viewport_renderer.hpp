#pragma once
#include "axe/core/types.hpp"

#include "axe/utils/glm_config.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/renderer/cube_renderer.hpp"

#include <memory>
#include <cstdint>

namespace axe
{

class Framebuffer;
class CubeRenderer;
class EditorCamera;

class AXE_API ViewportRenderer
	{
		
	public:
		void Initialize();
		void RenderToFramebuffer(Framebuffer& framebuffer, std::uint32_t width, std::uint32_t height, float timeSeconds);

		void OnMouseRotate(const glm::vec2& delta);
		void OnMousePan(const glm::vec2& delta);
		void OnMouseZoom(float delta);




	private:
		
		std::unique_ptr<CubeRenderer> m_CubeRenderer;
		std::unique_ptr<EditorCamera> m_Camera;
	};
}