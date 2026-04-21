#include "axe/graphics/shader.hpp"
#include "axe/graphics/opengl/opengl_shader.hpp"

namespace axe
{
	std::shared_ptr<Shader> Shader::Create(const std::string& vertexSource,
		const std::string& fragmentSource)
	{
		return std::make_shared<OpenGLShader>(vertexSource, fragmentSource);
	}
}