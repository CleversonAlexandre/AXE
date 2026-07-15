#include "skinned_mesh_instance.hpp"

#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/log/log.hpp"

namespace axe
{
	// ─────────────────────────────────────────────────────────────────────
	// REDE DE SEGURANÇA DO LAYOUT
	//
	// Os structs do compute shader (opengl_skinning_pass.cpp) são declarados
	// como escalares soltos justamente pra bater byte a byte com estes aqui.
	// Se alguém adicionar um campo ao Vertex ou ao SkinnedVertex e esquecer
	// do GLSL, o resultado seria vértices lidos com offset errado — a mesh
	// vira uma explosão de triângulos, e NADA no build acusa.
	//
	// Estes três asserts transformam esse pesadelo num erro de compilação.
	// Se um deles disparar: você mexeu num vértice — vá atualizar os structs
	// SkinnedVertexIn / StaticVertexOut no GLSL do skinning pass.
	// ─────────────────────────────────────────────────────────────────────
	static_assert(sizeof(SkinnedVertex) == 88,
		"SkinnedVertex mudou de tamanho — atualize o struct SkinnedVertexIn no compute shader (opengl_skinning_pass.cpp).");

	static_assert(sizeof(Vertex) == 56,
		"Vertex mudou de tamanho — atualize o struct StaticVertexOut no compute shader (opengl_skinning_pass.cpp).");

	static_assert(sizeof(glm::mat4) == 64,
		"mat4 precisa ter 64 bytes pra bater com o layout std430 do SSBO de bones.");

	SkinnedMeshInstance::SkinnedMeshInstance(const std::shared_ptr<SkinnedMesh>& source)
		: m_Source(source)
	{
		m_VertexCount = source->GetVertexCount();

		// Buffer de saída: mesmo número de vértices, mas com o layout do
		// Vertex ESTÁTICO (56 bytes, sem BoneIDs/Weights — depois do
		// skinning eles não servem mais pra nada).
		m_OutputBuffer = VertexBuffer::Create(m_VertexCount * sizeof(Vertex));

		m_OutputArray = VertexArray::Create();

		// EXATAMENTE o mesmo layout do Mesh estático (mesh.cpp). Tem que
		// ser: é o que permite os shaders existentes desenharem isto sem
		// alteração nenhuma.
		BufferLayout layout =
		{
			{ ShaderDataType::Float3, sizeof(float) * 3, false }, // 0 Position
			{ ShaderDataType::Float3, sizeof(float) * 3, false }, // 1 Normal
			{ ShaderDataType::Float2, sizeof(float) * 2, false }, // 2 TexCoord
			{ ShaderDataType::Float3, sizeof(float) * 3, false }, // 3 Tangent
			{ ShaderDataType::Float3, sizeof(float) * 3, false }  // 4 Bitangent
		};

		m_OutputArray->AddVertexBuffer(m_OutputBuffer, layout);

		// IBO COMPARTILHADO com o SkinnedMesh de origem: a animação move
		// vértices, nunca muda a topologia. Duplicar o index buffer por
		// personagem seria desperdício puro.
		m_OutputArray->SetIndexBuffer(source->GetIndexBuffer());

		m_DeformedMesh = std::make_unique<Mesh>(m_OutputArray, source->GetIndexCount());

		const std::size_t boneCount = source->GetSkeleton()
			? source->GetSkeleton()->GetBoneCount() : 0;

		m_BoneCapacity = static_cast<std::uint32_t>(boneCount);
		m_BoneBuffer = VertexBuffer::Create(
			static_cast<std::uint32_t>(std::max<std::size_t>(boneCount, 1) * sizeof(glm::mat4)));
	}

	void SkinnedMeshInstance::UploadPalette(const std::vector<glm::mat4>& palette)
	{
		if (palette.empty())
			return;

		// O rig cresceu (troca de asset em runtime). Realoca em vez de
		// escrever fora do buffer.
		if (palette.size() > m_BoneCapacity)
		{
			m_BoneCapacity = static_cast<std::uint32_t>(palette.size());
			m_BoneBuffer = VertexBuffer::Create(m_BoneCapacity * sizeof(glm::mat4));
		}

		m_BoneBuffer->SetData(
			palette.data(),
			static_cast<std::uint32_t>(palette.size() * sizeof(glm::mat4)));

		m_PaletteUploaded = true;
	}

} // namespace axe