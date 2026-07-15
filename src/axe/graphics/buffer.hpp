#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include "cstdint"

namespace axe
{
	class AXE_API  VertexBuffer
	{
	public:
		virtual ~VertexBuffer() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;
		virtual void SetData(const void* data, std::uint32_t size) = 0;

		// ID opaco do buffer na GPU. Mesmo padrão de Texture/Framebuffer —
		// o axe.dll continua sem incluir GL; é só um handle.
		virtual std::uint32_t GetRendererID() const = 0;

		// Liga este buffer como STORAGE BUFFER num ponto de binding.
		// É assim que o compute shader do Skin Cache lê os vértices de
		// entrada e escreve os deformados: o MESMO buffer serve como
		// storage (pro compute) e como vertex buffer (pro draw), sem cópia
		// nenhuma de ida e volta pela CPU.
		virtual void BindAsStorage(std::uint32_t binding) const = 0;

		static std::shared_ptr<VertexBuffer> Create(const void* data, std::uint32_t size);

		// Buffer VAZIO e dinâmico — destino do Skin Cache (a GPU escreve,
		// a CPU nunca toca).
		static std::shared_ptr<VertexBuffer> Create(std::uint32_t size);
	};

	class AXE_API  IndexBuffer
	{
	public:
		virtual ~IndexBuffer() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual std::uint32_t GetCount() const = 0;

		static std::shared_ptr<IndexBuffer> Create(const std::uint32_t* data, std::uint32_t count);
	};
}