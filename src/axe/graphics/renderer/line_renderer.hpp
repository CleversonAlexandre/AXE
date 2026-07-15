#pragma once
#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <glm/glm.hpp>

namespace axe
{
	class Shader;
	class VertexArray;
	class VertexBuffer;

	class AXE_API LineRenderer
	{
	public:
		LineRenderer();

		void Begin(const glm::mat4& viewProjection);
		void DrawBoundingBox(const glm::mat4& model, const glm::vec4& color);
		void End();

		// Era privado (so DrawBoundingBox usava). Agora e publico porque o
		// debug de esqueleto desenha linha a linha — e IK, sockets e hitboxes
		// vao querer o mesmo.
		void DrawLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);

	private:
		std::shared_ptr<Shader> m_Shader;
		std::shared_ptr<VertexArray> m_VertexArray;
		std::shared_ptr<VertexBuffer> m_VertexBuffer;

		glm::mat4 m_ViewProjection{ 1.0f };
	};
}