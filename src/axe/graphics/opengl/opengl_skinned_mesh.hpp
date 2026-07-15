#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/renderer/skinning_pass.hpp"

#include <memory>

namespace axe
{
	class ComputeShader;

	class AXE_API OpenGLSkinningPass final : public SkinningPass
	{
	public:
		bool Initialize() override;
		bool IsInitialized() const override { return m_Shader != nullptr; }

		void Execute(const SkinnedMesh& source,
			const VertexBuffer& boneBuffer,
			const VertexBuffer& output,
			std::uint32_t vertexCount) override;

		void Flush() override;

	private:
		std::shared_ptr<ComputeShader> m_Shader;

		// Quantos personagens foram despachados neste frame. Se for 0, o
		// Flush() não emite barreira nenhuma — não paga o custo em cena
		// sem personagem.
		std::uint32_t m_DispatchesThisFrame = 0;
	};

} // namespace axe