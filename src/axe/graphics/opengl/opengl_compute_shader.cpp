#include "axe/graphics/opengl/opengl_compute_shader.hpp"
#include "axe/log/log.hpp"

#include <glad/glad.h>
#include <vector>

namespace axe
{
	bool OpenGLComputeShader::IsComputeAvailable()
	{
		// Se o glad não resolveu o ponteiro, o driver não expõe compute.
		return glDispatchCompute != nullptr;
	}

	OpenGLComputeShader::OpenGLComputeShader(const std::string& source)
	{
		const GLuint shader = glCreateShader(GL_COMPUTE_SHADER);

		const char* src = source.c_str();
		glShaderSource(shader, 1, &src, nullptr);
		glCompileShader(shader);

		GLint ok = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
		if (ok == GL_FALSE)
		{
			GLint len = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
			std::vector<char> log(len > 0 ? len : 1);
			glGetShaderInfoLog(shader, len, &len, log.data());

			AXE_CORE_ERROR("ComputeShader: falha na compilacao:\n{}", log.data());

			glDeleteShader(shader);
			return; // m_RendererID fica 0 -> IsValid() == false
		}

		m_RendererID = glCreateProgram();
		glAttachShader(m_RendererID, shader);
		glLinkProgram(m_RendererID);

		glGetProgramiv(m_RendererID, GL_LINK_STATUS, &ok);
		if (ok == GL_FALSE)
		{
			GLint len = 0;
			glGetProgramiv(m_RendererID, GL_INFO_LOG_LENGTH, &len);
			std::vector<char> log(len > 0 ? len : 1);
			glGetProgramInfoLog(m_RendererID, len, &len, log.data());

			AXE_CORE_ERROR("ComputeShader: falha no link:\n{}", log.data());

			glDeleteProgram(m_RendererID);
			m_RendererID = 0;
		}

		glDetachShader(m_RendererID ? m_RendererID : 0, shader);
		glDeleteShader(shader);
	}

	OpenGLComputeShader::~OpenGLComputeShader()
	{
		if (m_RendererID)
			glDeleteProgram(m_RendererID);
	}

	void OpenGLComputeShader::Bind() const
	{
		glUseProgram(m_RendererID);
	}

	void OpenGLComputeShader::Unbind() const
	{
		glUseProgram(0);
	}

	void OpenGLComputeShader::Dispatch(std::uint32_t groupsX,
		std::uint32_t groupsY,
		std::uint32_t groupsZ) const
	{
		glDispatchCompute(groupsX, groupsY, groupsZ);
	}

	void OpenGLComputeShader::BarrierForVertexRead() const
	{
		// Dois bits, e ambos importam:
		//
		//   VERTEX_ATTRIB_ARRAY_BARRIER_BIT — o buffer de saída vai ser lido
		//     como VERTEX BUFFER pelos draws seguintes.
		//   SHADER_STORAGE_BARRIER_BIT — e tambem como SSBO, se outro
		//     compute encadear.
		//
		// Sem isso o driver NAO garante que as escritas do compute estejam
		// visiveis pro draw. Na pratica, em GPU AMD costuma "funcionar" por
		// sorte em cena leve e quebrar sob carga — o pior tipo de bug.
		glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
	}

	int OpenGLComputeShader::GetUniformLocation(const std::string& name)
	{
		auto it = m_UniformCache.find(name);
		if (it != m_UniformCache.end())
			return it->second;

		const int loc = glGetUniformLocation(m_RendererID, name.c_str());
		m_UniformCache[name] = loc;
		return loc;
	}

	void OpenGLComputeShader::SetInt(const std::string& name, int value)
	{
		glUniform1i(GetUniformLocation(name), value);
	}

	void OpenGLComputeShader::SetUint(const std::string& name, std::uint32_t value)
	{
		glUniform1ui(GetUniformLocation(name), value);
	}

} // namespace axe