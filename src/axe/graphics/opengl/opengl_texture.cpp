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

        // ASSET_VIEWER_V1 — guarda o que a janela de inspecao vai mostrar.
        m_Channels = static_cast<uint32_t>(channels);
        m_Path = filepath;

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

        //AXE_CORE_INFO("Texture2D: criando textura OpenGL ({}x{}, {} canais)...",
        //    m_Width, m_Height, channels);

        // Limpa erros GL pendentes de operações anteriores para não
        // confundir erros reais de criação da textura
        while (glGetError() != GL_NO_ERROR) {}

        // Garante alinhamento correto — texturas de 1 canal (GL_RED)
        // precisam de alinhamento 1 em vez do padrão 4
        //glPixelStorei(GL_UNPACK_ALIGNMENT, channels == 1 ? 1 : 4);

        if (channels == 1)
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        else if (channels == 3)
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        else
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);

        // Upload direto com glTexImage2D
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Width, m_Height,
            0, dataFormat, GL_UNSIGNED_BYTE, data);

        // ── SRGB_TEXTURES_V1 — MIPMAP ────────────────────────────────────────
        //
        // Ate aqui so existia o nivel 0, com MIN_FILTER = GL_LINEAR. Toda
        // textura vista de longe ou de raspao era reamostrada a partir da
        // resolucao cheia: uma superficie que ocupa 200 pixels na tela lendo
        // uma textura de 2048 pega texels espalhados e sem relacao entre si —
        // e isso CINTILA a cada frame em que a camera se move.
        //
        // Pior: o TAA entao tenta estabilizar esse ruido borrando, o que
        // troca cintilancia por imagem pastosa. Os dois somados sao boa parte
        // do "grafico basico".
        //
        // Com a cadeia de mips e LINEAR_MIPMAP_LINEAR (trilinear), o hardware
        // escolhe o nivel certo e a superficie fica ESTAVEL. Custa 33% de
        // memoria de textura — o negocio mais barato que existe em render.
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // ── ANISOTROPIA ──────────────────────────────────────────────────────
        //
        // O mipmap sozinho resolve a cintilancia mas erra em superficie vista
        // DE RASPAO — chao, parede, mesa: ali a compressao e diferente em cada
        // eixo, o mip e escolhido pelo eixo pior, e o resultado e um chao que
        // vira papinha a tres metros de distancia. A anisotropia amostra ao
        // longo do eixo comprimido e devolve o detalhe.
        //
        // E core desde o OpenGL 4.6 e extensao universal antes disso (a RX 580
        // suporta 16x). Consultado, e nao fixo em 16: pedir mais do que o
        // driver oferece e erro GL.
        {
            GLfloat maxAniso = 1.0f;
            glGetFloatv(0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY */, &maxAniso);
            if (maxAniso > 1.0f)
            {
                const GLfloat aniso = maxAniso < 8.0f ? maxAniso : 8.0f;
                glTexParameterf(GL_TEXTURE_2D, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, aniso);
            }
        }

        // ── WRAP: REPEAT, e nao CLAMP_TO_EDGE ────────────────────────────────
        //
        // Isto era uma DIVERGENCIA entre os dois construtores desta mesma
        // classe: o construtor vazio (linha ~16) sempre usou GL_REPEAT, o de
        // arquivo usava GL_CLAMP_TO_EDGE. Consequencia: qualquer material com
        // tiling de UV maior que 1 (node Multiply na UV — o jeito normal de
        // repetir um piso ou uma parede) nao repetia, ESTICAVA a ultima
        // fileira de texels ate a borda.
        //
        // ATENCAO ao testar: se algum sprite de particula ou cookie de luz
        // mostrar halo na borda, e este parametro — sprite quer CLAMP. A troca
        // e de uma linha, mas tiling quebrado e problema muito mais comum.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

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

        // AXE_CORE_INFO("Texture2D: '{}' carregada com SUCESSO!", filepath);
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

    // ═══════════════════════════════════════════════════════════════════════
    //  ASSET_VIEWER_V1 — filtro e wrap aplicados na hora
    //
    //  Sao parametros de AMOSTRAGEM, nao do conteudo: mudam como o driver le a
    //  textura, nao os pixels. Por isso nao precisam de reimportacao — e por
    //  isso o usuario consegue comparar Nearest e Linear vendo o resultado em
    //  vez de adivinhar.
    //
    //  glTextureParameteri (DSA) e nao glTexParameteri: nao mexe no que estiver
    //  bindado. Trocar o estado de bind aqui vazaria para o passe que estivesse
    //  no meio do trabalho — a mesma armadilha do glPolygonOffset do shadow
    //  pass, que ja mordeu nesta engine.
    // ═══════════════════════════════════════════════════════════════════════
    void OpenGLTexture2D::SetFilter(Filter f)
    {
        if (!m_Loaded || m_RendererID == 0) return;
        m_Filter = f;

        GLint minF = GL_LINEAR_MIPMAP_LINEAR;
        GLint magF = GL_LINEAR;

        switch (f)
        {
        case Filter::Nearest:   minF = GL_NEAREST; magF = GL_NEAREST; break;
        case Filter::Linear:    minF = GL_LINEAR;  magF = GL_LINEAR;  break;
        case Filter::Trilinear: default: break;   // usa os mips gerados no import
        }

        glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, minF);
        glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, magF);
    }

    void OpenGLTexture2D::SetWrap(Wrap w)
    {
        if (!m_Loaded || m_RendererID == 0) return;
        m_Wrap = w;

        GLint mode = GL_REPEAT;
        switch (w)
        {
        case Wrap::Clamp:  mode = GL_CLAMP_TO_EDGE;   break;
        case Wrap::Mirror: mode = GL_MIRRORED_REPEAT; break;
        case Wrap::Repeat: default: break;
        }

        glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, mode);
        glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, mode);
    }
}