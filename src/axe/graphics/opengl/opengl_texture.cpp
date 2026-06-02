#include "opengl_texture.hpp"
#include "glad/glad.h"
#include <stb_image.h>
#include "axe/log/log.hpp"

namespace axe
{
    OpenGLTexture2D::OpenGLTexture2D(std::uint32_t width, std::uint32_t height)
        : m_Width(width), m_Height(height), m_RendererID(0), m_Loaded(false)
    {
        glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
        glTextureStorage2D(m_RendererID, 1, GL_RGBA8, m_Width, m_Height);

        glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);

        m_Loaded = true;
    }

    OpenGLTexture2D::OpenGLTexture2D(const std::string& filepath)
        : m_Width(0), m_Height(0), m_RendererID(0), m_Loaded(false)
    {
        // Carrega a imagem
        //stbi_set_flip_vertically_on_load(true);

        int width, height, channels;
        stbi_uc* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);

        if (!data)
        {
            AXE_CORE_ERROR("Texture2D: stbi_load falhou para '{}'", filepath);
            return;
        }

        m_Width = static_cast<uint32_t>(width);
        m_Height = static_cast<uint32_t>(height);

        // Determina formato
        GLenum internalFormat = GL_RGB8;
        GLenum dataFormat = GL_RGB;

        if (channels == 4)
        {
            internalFormat = GL_RGBA8;
            dataFormat = GL_RGBA;
        }
        else if (channels == 3)
        {
            internalFormat = GL_RGB8;
            dataFormat = GL_RGB;
        }
        else if (channels == 1)
        {
            internalFormat = GL_R8;
            dataFormat = GL_RED;
        }

        AXE_CORE_INFO("Texture2D: criando textura OpenGL ({}x{}, {} canais)...",
            m_Width, m_Height, channels);

        // Limpa erros GL pendentes de operações anteriores para não
        // confundir erros reais de criação da textura
        while (glGetError() != GL_NO_ERROR) {}

        // Garante alinhamento correto — texturas de 1 canal (GL_RED)
        // precisam de alinhamento 1 em vez do padrão 4
        glPixelStorei(GL_UNPACK_ALIGNMENT, channels == 1 ? 1 : 4);

        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);

        // Upload direto com glTexImage2D
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Width, m_Height,
            0, dataFormat, GL_UNSIGNED_BYTE, data);

        // Parâmetros
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindTexture(GL_TEXTURE_2D, 0);

        // Restaura alinhamento padrão
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

        // Verifica erros
        GLenum error = glGetError();
        if (error != GL_NO_ERROR)
        {
            AXE_CORE_ERROR("Texture2D: erro OpenGL: 0x{:X}", error);
            glDeleteTextures(1, &m_RendererID);
            m_RendererID = 0;
            stbi_image_free(data);
            return;
        }

        stbi_image_free(data);
        m_Loaded = true;

        AXE_CORE_INFO("Texture2D: '{}' carregada com SUCESSO!", filepath);
    }

    OpenGLTexture2D::~OpenGLTexture2D()
    {
        if (m_RendererID != 0)
            glDeleteTextures(1, &m_RendererID);
    }

    void OpenGLTexture2D::Bind(std::uint32_t slot) const
    {
        glBindTextureUnit(slot, m_RendererID);
    }

    void OpenGLTexture2D::Unbind() const
    {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}