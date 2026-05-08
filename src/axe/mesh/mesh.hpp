#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <vector>
#include "axe/utils/glm_config.hpp"

#include "axe/graphics/vertex_array.hpp"
namespace axe
{
		class VertexArray;
		class VertexBuffer;
		class IndexBuffer;

		struct Vertex
		{
			glm::vec3 Position;
			glm::vec3 Normal;
			glm::vec2 TexCoord;
		};

		class AXE_API Mesh
		{
		public:
			Mesh(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);

			std::shared_ptr<VertexArray> GetVertexArray() const { return m_VertexArray; }
			std::uint32_t GetIndexCount() const { return m_IndexCount; }

		private:
			std::shared_ptr<VertexArray> m_VertexArray;
			std::shared_ptr<VertexBuffer> m_VertexBuffer;
			std::shared_ptr<IndexBuffer> m_IndexBuffer;

			std::uint32_t m_IndexCount = 0;
		};
	
}