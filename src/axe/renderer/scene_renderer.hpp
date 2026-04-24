#pragma once
#include "axe/core/types.hpp"
#include "axe/scene/scene.hpp"
#include "axe/graphics/renderer/cube_renderer.hpp"
#include "axe/graphics/editor_camera.hpp"

#include "axe/graphics/renderer/line_renderer.hpp"

namespace  axe
{
	class AXE_API SceneRenderer
	{
	public:
		SceneRenderer();
		void RenderScene(const Scene& scene, const EditorCamera& camera, std::uint32_t selectedObjectID = 0);

	private:
		CubeRenderer m_CubeRenderer;

		LineRenderer m_LineRenderer;
	};
}