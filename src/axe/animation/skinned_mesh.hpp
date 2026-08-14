#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/animation/skeleton.hpp"

#include <memory>
#include <vector>
#include <cstdint>

namespace axe
{
	class VertexArray;
	class VertexBuffer;
	class IndexBuffer;

	// DECISÃO DE ARQUITETURA: SkinnedVertex é um tipo SEPARADO de Vertex.
	//
	// A alternativa — enfiar BoneIDs+Weights no Vertex existente — custaria
	// +32 bytes em TODO vértice de TODA malha estática do jogo (paredes,
	// props, terreno), mexeria no BufferLayout do qual mesh_factory e a
	// física dependem, e forçaria todo shader a declarar atributos que nunca
	// usa. É a mesma razão pela qual a Unreal separa StaticMesh de
	// SkeletalMesh.
	//
	// Layout de atributos (locations no vertex shader):
	//   0 Position   1 Normal   2 TexCoord   3 Tangent   4 Bitangent
	//   5 BoneIDs (ivec4)   6 Weights (vec4)
	//
	// As locations 0..4 são IDÊNTICAS às da Mesh estática de propósito: um
	// shader skinned é o shader estático + 2 atributos + a multiplicação da
	// palette. O MaterialCompiler vai gerar as duas variantes do mesmo grafo.
	struct SkinnedVertex
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f };
		glm::vec2 TexCoord{ 0.0f };
		glm::vec3 Tangent{ 1.0f, 0.0f, 0.0f };
		glm::vec3 Bitangent{ 0.0f, 1.0f, 0.0f };

		// -1 = slot vazio. O shader ignora slots com peso 0, mas manter -1
		// (em vez de 0) torna um bug de indexação visível em vez de silencioso
		// — sem isto, um slot vazio "aponta" pro bone 0 (a raiz) e o vértice
		// é arrastado sutilmente pelo quadril.
		glm::ivec4 BoneIDs{ -1 };
		glm::vec4  Weights{ 0.0f };

		// ── B2.4: estas duas viraram INLINE, e vieram do .cpp para ca ────────
		//
		// O importador de FBX mudou para o editor.exe, e passou a ser o unico
		// chamador delas — de fora da DLL. Como estavam definidas no
		// skinned_mesh.cpp e `SkinnedVertex` nao e marcada com AXE_API, os
		// simbolos nao eram exportados e o link do editor quebrava.
		//
		// Duas saidas: marcar a struct com AXE_API, ou trazer as definicoes
		// para o header. Escolhida a segunda, porque:
		//
		//   - sao dez linhas sem nenhuma dependencia alem de glm, que o header
		//     ja inclui;
		//   - sao chamadas UMA VEZ POR VERTICE durante a importacao, entao
		//     inline elimina a chamada num laco que roda centenas de milhares
		//     de vezes;
		//   - exportar a struct so para duas funcoes triviais aumentaria a
		//     superficie da ABI da DLL sem ganho nenhum.

		// Ocupa o primeiro slot livre. Ignora peso ~0 (Assimp emite muitos).
		// Retorna false se os 4 slots já estiverem cheios — o chamador decide
		// se descarta ou substitui a menor influência.
		bool AddBoneInfluence(int boneID, float weight)
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

		// Faz os pesos somarem 1.0. OBRIGATÓRIO: com aiProcess_LimitBoneWeights
		// o Assimp corta influências além de 4 e o que sobra soma < 1 — sem
		// renormalizar, o vértice encolhe em direção à origem do modelo.
		void NormalizeWeights()
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
	};

	// enable_shared_from_this: o SceneRenderer só tem um `const SkinnedMesh*`
	// vindo do SkinnedDrawCall, mas a SkinnedMeshInstance precisa MANTER a
	// malha viva (ela sobrevive ao frame, guardada no skin cache). Sem isto,
	// destruir a entidade liberaria a malha enquanto o cache ainda aponta
	// pra ela — use-after-free na próxima vez que o compute rodar.
	class AXE_API SkinnedMesh : public std::enable_shared_from_this<SkinnedMesh>
	{
	public:
		SkinnedMesh(const std::vector<SkinnedVertex>& vertices,
			const std::vector<std::uint32_t>& indices,
			const std::shared_ptr<Skeleton>& skeleton);

		std::shared_ptr<VertexArray> GetVertexArray() const { return m_VertexArray; }
		std::uint32_t GetIndexCount() const { return m_IndexCount; }

		// O SkinningPass liga este buffer como SSBO de ENTRADA do compute.
		// O mesmo buffer serve de vertex buffer e de storage buffer — em GL
		// um buffer object não tem "tipo", só pontos de bind.
		const std::shared_ptr<VertexBuffer>& GetVertexBuffer() const { return m_VertexBuffer; }

		std::uint32_t GetVertexCount() const { return static_cast<std::uint32_t>(m_Vertices.size()); }

		// Compartilhado por TODAS as instâncias desta malha — o IBO nunca
		// muda com a animação (a topologia é fixa; só as posições mudam).
		const std::shared_ptr<IndexBuffer>& GetIndexBuffer() const { return m_IndexBuffer; }

		const std::shared_ptr<Skeleton>& GetSkeleton() const { return m_Skeleton; }

		// Dados na CPU — mantidos pelos mesmos motivos que a Mesh estática
		// (física, raycast, futuros cálculos de bounds animados).
		const std::vector<SkinnedVertex>& GetVertices() const { return m_Vertices; }
		const std::vector<std::uint32_t>& GetIndices()  const { return m_Indices; }

	private:
		std::shared_ptr<VertexArray>  m_VertexArray;
		std::shared_ptr<VertexBuffer> m_VertexBuffer;
		std::shared_ptr<IndexBuffer>  m_IndexBuffer;

		std::shared_ptr<Skeleton>     m_Skeleton;

		std::vector<SkinnedVertex>    m_Vertices;
		std::vector<std::uint32_t>    m_Indices;

		std::uint32_t m_IndexCount = 0;
	};

} // namespace axe