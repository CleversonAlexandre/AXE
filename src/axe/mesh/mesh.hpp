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
		glm::vec3 Tangent;
		glm::vec3 Bitangent;

	};

	class AXE_API Mesh
	{
	public:
		Mesh(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);

		// Envelopa um VAO que JÁ existe na GPU, sem upload e sem cópia na
		// CPU. É o que transforma a saída do Skin Cache numa Mesh comum:
		// a partir daqui, todo pass do engine (sombra, G-Buffer, forward,
		// outline, picking) desenha o personagem sem saber que ele é
		// animado.
		//
		// GetVertices()/GetIndices() vêm VAZIOS neste caminho — os dados
		// vivem só na GPU. Quem precisa deles (física) usa o SkinnedMesh
		// de origem, que mantém a cópia em bind pose.
		Mesh(const std::shared_ptr<VertexArray>& vertexArray, std::uint32_t indexCount);

		std::shared_ptr<VertexArray> GetVertexArray() const { return m_VertexArray; }
		std::uint32_t GetIndexCount() const { return m_IndexCount; }

		// Acesso aos dados para physics collision
		const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
		const std::vector<std::uint32_t>& GetIndices()  const { return m_Indices; }

	private:
		std::shared_ptr<VertexArray>  m_VertexArray;
		std::shared_ptr<VertexBuffer> m_VertexBuffer;
		std::shared_ptr<IndexBuffer>  m_IndexBuffer;

		std::vector<Vertex>     m_Vertices; // guardados para physics
		std::vector<uint32_t>   m_Indices;

		std::uint32_t m_IndexCount = 0;
	};

}