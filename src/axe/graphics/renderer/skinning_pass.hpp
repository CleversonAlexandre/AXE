#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

#include <memory>
#include <vector>

namespace axe
{
	class SkinnedMesh;
	class VertexBuffer;

	// SKIN CACHE
	//
	// Roda UMA vez por personagem, por frame, ANTES de qualquer pass de
	// render. Lê o SkinnedMesh (posições em bind pose + BoneIDs + Weights) e
	// a palette de matrizes, e escreve num buffer de saída cujo layout é
	// EXATAMENTE o do `struct Vertex` estático:
	//
	//     vec3 Position | vec3 Normal | vec2 TexCoord | vec3 Tangent | vec3 Bitangent
	//
	// Depois disso o personagem É uma mesh estática. Sombra, G-Buffer,
	// forward, outline, picking e probe bake o consomem com os shaders que
	// já existem — nenhum deles sabe que existe animação, e o
	// MaterialCompiler não precisa gerar variante skinned de nada.
	//
	// (É a mesma ideia do "GPU Skin Cache" da Unreal.)
	class AXE_API SkinningPass
	{
	public:
		virtual ~SkinningPass() = default;

		// Compila o compute shader. Retorna false se o driver não suportar
		// compute ou a compilação falhar — nesse caso o SceneRenderer
		// desenha o personagem na bind pose (rígido, mas VISÍVEL) em vez de
		// sumir com ele da tela.
		virtual bool Initialize() = 0;
		virtual bool IsInitialized() const = 0;

		// Deforma `source` com `boneBuffer` (palette já enviada pra GPU) e
		// escreve em `output`.
		//
		// boneBuffer/output são buffers da GPU: nada volta pra CPU.
		virtual void Execute(const SkinnedMesh& source,
			const VertexBuffer& boneBuffer,
			const VertexBuffer& output,
			std::uint32_t vertexCount) = 0;

		// Barreira de memória. Chamada UMA vez depois de despachar TODOS os
		// personagens do frame — não uma vez por personagem. Barreira por
		// personagem serializaria a GPU à toa; o objetivo é que os N
		// dispatches rodem em paralelo e só então o render espere.
		virtual void Flush() = 0;

		static std::shared_ptr<SkinningPass> Create();
	};

} // namespace axe