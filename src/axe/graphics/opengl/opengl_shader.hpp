#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/shader.hpp"
#include <unordered_map>
#include <string>

namespace axe
{
	class AXE_API OpenGLShader final : public Shader
	{
	public:
		OpenGLShader(const std::string& vertexSource, const std::string& fragmentSource);
		~OpenGLShader() override;

		void Bind() const override;
		void Unbind() const override;

		void SetFloat4(const std::string& name, float x, float y, float z, float w) override;
		void SetMat4(const std::string& name, const float* value) override;

	private:
		int GetUniformLocation(const std::string& name);

	private:
		unsigned int m_RendererID = 0;
		std::unordered_map<std::string, int> m_UniformLocationCache;
	};
}