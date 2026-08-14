#include "skinned_mesh.hpp"

#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"

#include <cmath>

namespace axe
{
	// B2.4 — AddBoneInfluence e NormalizeWeights viraram inline no header.
	// Ver a nota la: o importador saiu da DLL e precisava dos simbolos.

	SkinnedMesh::SkinnedMesh(const std::vector<SkinnedVertex>& vertices,
		const std::vector<std::uint32_t>& indices,
		const std::shared_ptr<Skeleton>& skeleton)
		: m_Skeleton(skeleton)
	{
		m_VertexArray = VertexArray::Create();

		m_VertexBuffer = VertexBuffer::Create(
			vertices.data(),
			static_cast<std::uint32_t>(vertices.size() * sizeof(SkinnedVertex))
		);

		// A ordem AQUI tem que bater byte a byte com a ordem dos campos em
		// SkinnedVertex — o BufferLayout calcula os offsets sequencialmente.
		BufferLayout layout =
		{
			{ ShaderDataType::Float3, sizeof(float) * 3, false }, // 0 Position
			{ ShaderDataType::Float3, sizeof(float) * 3, false }, // 1 Normal
			{ ShaderDataType::Float2, sizeof(float) * 2, false }, // 2 TexCoord
			{ ShaderDataType::Float3, sizeof(float) * 3, false }, // 3 Tangent
			{ ShaderDataType::Float3, sizeof(float) * 3, false }, // 4 Bitangent

			// Int4 (não Float4!) — roteia pra glVertexAttribIPointer.
			// Passar índices de bone como float é o bug clássico: o driver
			// normaliza/converte e o shader recebe índices errados.
			{ ShaderDataType::Int4,   sizeof(std::int32_t) * 4, false }, // 5 BoneIDs
			{ ShaderDataType::Float4, sizeof(float) * 4, false }         // 6 Weights
		};

		m_VertexArray->AddVertexBuffer(m_VertexBuffer, layout);

		m_IndexBuffer = IndexBuffer::Create(
			indices.data(),
			static_cast<std::uint32_t>(indices.size())
		);

		m_VertexArray->SetIndexBuffer(m_IndexBuffer);

		m_IndexCount = static_cast<std::uint32_t>(indices.size());

		m_Vertices = vertices;
		m_Indices = indices;
	}

} // namespace axe