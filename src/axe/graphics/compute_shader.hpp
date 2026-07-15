#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <string>
#include <cstdint>

namespace axe
{
	// Compute shader — primeira peça de GPGPU do engine.
	//
	// Existe pro Skin Cache: deformar os vértices de um personagem UMA vez
	// por frame, num buffer com layout de mesh estática. Daí em diante o
	// personagem é uma mesh comum, e TODOS os passes (sombra, G-Buffer,
	// forward, outline, picking, probe bake) o desenham sem saber que ele é
	// animado — e o MaterialCompiler não precisa gerar variante nenhuma.
	//
	// Mesmo contrato do resto de axe/graphics: interface abstrata aqui,
	// implementação em graphics/opengl/. Nada de GL vaza pro axe.dll.
	class AXE_API ComputeShader
	{
	public:
		virtual ~ComputeShader() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		// Lança os work groups. groupsX = ceil(itens / local_size_x).
		virtual void Dispatch(std::uint32_t groupsX,
			std::uint32_t groupsY = 1,
			std::uint32_t groupsZ = 1) const = 0;

		// OBRIGATÓRIO entre o dispatch e o draw que consome o resultado.
		// Sem esta barreira, a GPU pode começar a desenhar com os vértices
		// do frame ANTERIOR (ou meio-escritos): o personagem pisca, treme,
		// ou aparece deformado só em algumas máquinas. É o bug mais cruel
		// desse sistema, porque some quando você tenta debugar.
		virtual void BarrierForVertexRead() const = 0;

		virtual void SetInt(const std::string& name, int value) = 0;
		virtual void SetUint(const std::string& name, std::uint32_t value) = 0;

		// Retorna nullptr se a compilação falhar (o log vai pro console).
		static std::shared_ptr<ComputeShader> Create(const std::string& computeSource);

		// false se o driver/GPU não suportar compute (GL < 4.3).
		// A RX 580 tem GL 4.6, mas máquinas antigas do jogador podem não ter
		// — o SceneRenderer usa isso pra decidir se cai num fallback.
		static bool IsSupported();
	};

} // namespace axe