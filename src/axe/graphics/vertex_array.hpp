#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <vector>
#include <cstdint>

namespace axe
{
	enum class AXE_API ShaderDataType
	{
		Float,
		Float2,
		Float3,
		Float4,

		// Tipos inteiros — enviados ao shader via glVertexAttribIPointer
		// (NÃO glVertexAttribPointer, que converteria para float e
		// corromperia os índices). Usados pelos BoneIDs do SkinnedVertex.
		Int,
		Int2,
		Int3,
		Int4
	};

	struct BufferElement
	{
		ShaderDataType Type;
		std::uint32_t Size;
		std::uint32_t Offset;
		bool Normalized;


		BufferElement(ShaderDataType type, std::uint32_t size, bool normalized = false)
			: Type(type), Size(size), Offset(0), Normalized(normalized)
		{}
	};

	class AXE_API BufferLayout
	{
	public:
		BufferLayout() = default;
		BufferLayout(std::initializer_list<BufferElement> elements)
			: m_Elements(elements)
		{
			CalculateOffsetsAndStride();
		}

		const std::vector<BufferElement>& GetElements() const { return m_Elements; }
		std::uint32_t GetStride() const { return m_Stride; }

	private:
		void CalculateOffsetsAndStride()
		{
			std::uint32_t offset = 0;
			m_Stride = 0;

			for (auto& element : m_Elements)
			{
				element.Offset = offset;
				offset += element.Size;
				m_Stride += element.Size;
			}

		}

	private:
		std::vector<BufferElement> m_Elements;
		std::uint32_t m_Stride = 0;
	};

	class VertexBuffer;
	class IndexBuffer;

	class AXE_API VertexArray
	{
	public:
		virtual ~VertexArray() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual void AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer,
			const BufferLayout& layout) = 0;

		virtual void SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer) = 0;

		virtual const std::shared_ptr<IndexBuffer>& GetIndexBuffer() const = 0;

		static std::shared_ptr<VertexArray> Create();
	};
}