#include "opengl_texture3d.hpp"
#include <glad/glad.h>

namespace axe
{
	OpenGLTexture3D::OpenGLTexture3D(std::uint32_t width, std::uint32_t height,
		std::uint32_t depth, const float* rgbaData)
		: m_Width(width), m_Height(height), m_Depth(depth)
	{
		glCreateTextures(GL_TEXTURE_3D, 1, &m_RendererID);
		glTextureStorage3D(m_RendererID, 1, GL_RGBA16F,
			(GLsizei)width, (GLsizei)height, (GLsizei)depth);

		if (rgbaData)
			glTextureSubImage3D(m_RendererID, 0, 0, 0, 0,
				(GLsizei)width, (GLsizei)height, (GLsizei)depth,
				GL_RGBA, GL_FLOAT, rgbaData);

		// Linear + clamp: a interpolação trilinear entre probes vizinhas é
		// exatamente o filtro do hardware; clamp evita que a borda do grid
		// "enrole" pro lado oposto.
		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	}

	OpenGLTexture3D::~OpenGLTexture3D()
	{
		if (m_RendererID) glDeleteTextures(1, &m_RendererID);
	}

	void OpenGLTexture3D::Bind(std::uint32_t slot) const
	{
		glBindTextureUnit(slot, m_RendererID);
	}
}