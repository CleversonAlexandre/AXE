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

		static std::shared_ptr<VertexBuffer> Create(const void* data, std::uint32_t size);
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
