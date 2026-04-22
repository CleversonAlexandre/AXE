#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include "axe/graphics/editor_camera.hpp"

namespace axe
{
	class Shader;
	class VertexArray;
	class Pipeline;
	class Texture2D;
	class EditorCamera;

	class AXE_API CubeRenderer
	{
	public:
		CubeRenderer();

		void Render(float timeSeconds, const EditorCamera& camera);

	private:
		std::shared_ptr<Shader> m_Shader;
		std::shared_ptr<VertexArray> m_VertexArray;
		std::shared_ptr<Pipeline> m_Pipeline;
		std::shared_ptr<Texture2D> m_Texture;
	};
}