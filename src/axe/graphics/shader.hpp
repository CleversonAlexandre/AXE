#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <string>

namespace axe
{
	class AXE_API  Shader
	{
	public:
		virtual ~Shader() = default;
		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual void SetFloat4(const std::string& name, float x, float y, float z, float w) = 0;
		virtual void SetMat4(const std::string& name, const float* valeu) = 0;
		static std::shared_ptr<Shader> Create(const std::string& vertexSource,
											  const std::string& fragmentSource);

	};
}