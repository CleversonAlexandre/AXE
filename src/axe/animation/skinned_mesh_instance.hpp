#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/animation/skinned_mesh.hpp"
#include "axe/mesh/mesh.hpp"

#include <memory>
#include <vector>

namespace axe
{
	class VertexBuffer;
	class VertexArray;

	// Estado de GPU de UM personagem em cena.
	//
	// Por que por INSTÂNCIA e não por malha: dez inimigos compartilham o
	// mesmo SkinnedMesh (mesmos vértices em bind pose, mesmo IBO), mas cada
	// um está numa pose diferente no mesmo frame. Logo, cada um precisa do
	// seu próprio buffer de saída deformado.
	//
	// O que é compartilhado (não duplicado):
	//   - o SkinnedMesh de origem (vértices em bind pose)  → shared_ptr
	//   - o IndexBuffer (topologia não muda com a animação) → shared_ptr
	//
	// O que é por instância:
	//   - o VBO de saída (posições deformadas deste frame)
	//   - o VBO da palette de bones
	//   - o VAO que amarra os dois acima ao layout de Vertex ESTÁTICO
	//
	// Custo de memória: ~56 bytes por vértice por personagem. Um personagem
	// de 30k vértices = ~1.7 MB. Dez = 17 MB. É o preço da Rota B, e é o
	// mesmo que a Unreal paga.
	class AXE_API SkinnedMeshInstance
	{
	public:
		explicit SkinnedMeshInstance(const std::shared_ptr<SkinnedMesh>& source);

		// Envia a palette deste frame pra GPU.
		void UploadPalette(const std::vector<glm::mat4>& palette);

		const SkinnedMesh* GetSource() const { return m_Source.get(); }
		std::uint32_t GetVertexCount() const { return m_VertexCount; }

		const std::shared_ptr<VertexBuffer>& GetOutputBuffer() const { return m_OutputBuffer; }
		const std::shared_ptr<VertexBuffer>& GetBoneBuffer() const { return m_BoneBuffer; }

		// A mesh ESTÁTICA que os passes de render consomem. Depois do
		// compute rodar, este é o personagem já deformado — e nenhum pass
		// precisa saber disso.
		const Mesh& GetDeformedMesh() const { return *m_DeformedMesh; }

		// Palette nunca enviada = compute nunca rodou. O SceneRenderer usa
		// isso pra não desenhar lixo no primeiro frame.
		bool HasPalette() const { return m_PaletteUploaded; }

	private:
		std::shared_ptr<SkinnedMesh>  m_Source;

		std::shared_ptr<VertexBuffer> m_OutputBuffer;  // vértices deformados
		std::shared_ptr<VertexBuffer> m_BoneBuffer;    // palette (SSBO)
		std::shared_ptr<VertexArray>  m_OutputArray;

		std::unique_ptr<Mesh>         m_DeformedMesh;

		std::uint32_t m_VertexCount = 0;
		std::uint32_t m_BoneCapacity = 0;   // pra realocar se o rig mudar
		bool          m_PaletteUploaded = false;
	};

} // namespace axe