#include "axe/graphics/opengl/opengl_shader.hpp"
#include "axe/log/log.hpp"

#include <glad/glad.h>
#include <stdexcept>
#include <vector>

namespace axe
{
	static unsigned int CompileShader(unsigned int type, const std::string& source)
	{
		unsigned int shader = glCreateShader(type);
		const char* src = source.c_str();
		glShaderSource(shader, 1, &src, nullptr);
		glCompileShader(shader);

		int isCompiled = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);

		if (isCompiled == GL_FALSE)
		{
			int maxLength = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

			std::vector<char> infoLog(maxLength);
			glGetShaderInfoLog(shader, maxLength, &maxLength, infoLog.data());

			glDeleteShader(shader);
			throw std::runtime_error(infoLog.data());
		}

		return shader;
	}

	OpenGLShader::OpenGLShader(const std::string& vertexSource, const std::string& fragmentSource)
	{
		unsigned int program = glCreateProgram();
		unsigned int vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource);
		unsigned int fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);

		glAttachShader(program, vertexShader);
		glAttachShader(program, fragmentShader);
		glLinkProgram(program);

		int isLinked = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &isLinked);

		if (isLinked == GL_FALSE)
		{
			int maxLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

			std::vector<char> infoLog(maxLength);
			glGetProgramInfoLog(program, maxLength, &maxLength, infoLog.data());

			glDeleteProgram(program);
			glDeleteShader(vertexShader);
			glDeleteShader(fragmentShader);

			throw std::runtime_error(infoLog.data());
		}

		glDetachShader(program, vertexShader);
		glDetachShader(program, fragmentShader);
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);

		m_RendererID = program;
	}

	OpenGLShader::~OpenGLShader()
	{
		glDeleteProgram(m_RendererID);
	}

	void OpenGLShader::Bind() const
	{
		glUseProgram(m_RendererID);
	}

	void OpenGLShader::Unbind() const
	{
		glUseProgram(0);
	}

	int OpenGLShader::GetUniformLocation(const std::string& name)
	{
		if (auto it = m_UniformLocationCache.find(name); it != m_UniformLocationCache.end())
			return it->second;

		int location = glGetUniformLocation(m_RendererID, name.c_str());

		if (location == -1)
		{
			AXE_CORE_INFO("Uniform '{}' not found in shader", name);
		}

		m_UniformLocationCache[name] = location;
		return location;
	}

	void OpenGLShader::SetFloat4(const std::string& name, float x, float y, float z, float w)
	{
		Bind();
		glUniform4f(GetUniformLocation(name), x, y, z, w);
	}

	void OpenGLShader::SetMat4(const std::string& name, const float* value)
	{
		Bind();
		int location = GetUniformLocation(name);
		glUniformMatrix4fv(
			location,
			1,          //Quandtidade de matrizes
			GL_FALSE,   // Não transpor
			value);		// pontreiro para os dados 	
	}
}