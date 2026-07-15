#include "skinned_mesh.hpp"

#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"

#include <cmath>

namespace axe
{
	bool SkinnedVertex::AddBoneInfluence(int boneID, float weight)
	{
		// O Assimp emite pesos irrelevantes (1e-7) que só gastam slot.
		if (weight <= 1e-5f)
			return true;

		for (int i = 0; i < AXE_MAX_BONE_INFLUENCE; ++i)
		{
			if (Weights[i] <= 0.0f)
			{
				BoneIDs[i] = boneID;
				Weights[i] = weight;
				return true;
			}
		}

		// 4 slots cheios. O loader trata (substitui a menor influência).
		return false;
	}

	void SkinnedVertex::NormalizeWeights()
	{
		const float sum = Weights[0] + Weights[1] + Weights[2] + Weights[3];

		if (sum <= 1e-5f)
		{
			// Vértice sem NENHUM peso (acontece: malha com partes não
			// riggadas). Sem tratamento, a matriz final vira zero e o
			// vértice colapsa na origem — a mesh aparece "sugada" pro chão.
			// Amarra 100% no bone raiz: a parte fica rígida, mas VISÍVEL.
			BoneIDs = glm::ivec4(0, -1, -1, -1);
			Weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
			return;
		}

		Weights /= sum;
	}

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