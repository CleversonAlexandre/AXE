#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/buffer.hpp"

namespace axe
{
	class AXE_API OpenGLVertexBuffer final : public VertexBuffer
	{
	public:
		OpenGLVertexBuffer(const void* data, std::uint32_t size);

		// Buffer vazio e dinâmico — destino do Skin Cache.
		explicit OpenGLVertexBuffer(std::uint32_t size);

		~OpenGLVertexBuffer() override;

		void Bind() const override;
		void Unbind() const override;
		void SetData(const void* data, std::uint32_t size) override;

		std::uint32_t GetRendererID() const override { return m_RendererID; }
		void BindAsStorage(std::uint32_t binding) const override;

	private:
		unsigned int m_RendererID = 0;
	};

	class AXE_API OpenGLIndexBuffer final : public IndexBuffer
	{
	public:
		OpenGLIndexBuffer(const std::uint32_t* data, std::uint32_t count);
		~OpenGLIndexBuffer() override;

		void Bind() const override;
		void Unbind() const override;

		std::uint32_t GetCount() const override { return m_Count; }

	private:
		unsigned int m_RendererID = 0;
		std::uint32_t m_Count = 0;
	};
}