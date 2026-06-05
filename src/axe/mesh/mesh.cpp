#include "mesh.hpp"

#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"


namespace axe
{
	Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices)
	{
		m_VertexArray = VertexArray::Create();

		m_VertexBuffer = VertexBuffer::Create(
			vertices.data(),
			static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex))
		);

		BufferLayout layout =
		{
			{axe::ShaderDataType::Float3, sizeof(float) * 3, false}, //Position
			{axe::ShaderDataType::Float3, sizeof(float) * 3, false}, //Normal
			{axe::ShaderDataType::Float2, sizeof(float) * 2, false}, //TextCoord
			{axe::ShaderDataType::Float3, sizeof(float) * 3, false}, // Tangent   ← location 3
			{axe::ShaderDataType::Float3, sizeof(float) * 3, false}  // Bitangent ← location 4	
		};

		m_VertexArray->AddVertexBuffer(m_VertexBuffer, layout);

		m_IndexBuffer = IndexBuffer::Create(
			indices.data(),
			static_cast<std::uint32_t>(indices.size())
		);

		m_VertexArray->SetIndexBuffer(m_IndexBuffer);

		m_IndexCount = static_cast<std::uint32_t>(indices.size());

		// Guarda cópia para uso em physics collision
		m_Vertices = vertices;
		m_Indices = indices;
	}
}