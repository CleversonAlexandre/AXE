#pragma once
#include "axe/graphics/renderer/scene_height_pass.hpp"
#include <cstdint>
#include <memory>

namespace axe
{
    class Shader;

    class OpenGLSceneHeightPass : public SceneHeightPass
    {
    public:
        OpenGLSceneHeightPass() = default;
        ~OpenGLSceneHeightPass() override;

        void Initialize(uint32_t resolution = 1024) override;
        void SetSlice(float y) override { m_SliceY = y; }
        void Begin(const glm::mat4& topDownMatrix)  override;
        void DrawMesh(const Mesh& mesh, const glm::mat4& model) override;
        void End()                                  override;

        uint32_t         GetHeightMapID() const override { return m_HeightTex; }
        uint32_t         GetSeedMapID()   const override { return m_SeedTex[m_SeedFront]; }
        const glm::mat4& GetMatrix()      const override { return m_Matrix; }
        bool             IsInitialized()  const override { return m_Initialized; }

    private:
        void RunJumpFlood();

        uint32_t  m_HeightFBO = 0;
        uint32_t  m_HeightTex = 0;   // R32F — Y de mundo do TOPO da coluna
        uint32_t  m_BottomTex = 0;   // R32F — Y de mundo do FUNDO da coluna
        uint32_t  m_AboveTex = 0;    // R32F — superficies acima da fatia (paridade)

        // SCENE_HEIGHT_V3 — nao ha mais depth buffer aqui. Os dois extremos
        // saem por equacao de blend (GL_MAX no topo, GL_MIN no fundo), e o
        // depth test so sabia resolver um deles.

        // Ping-pong do jump flood. RG32F guardando o XZ de mundo da semente.
        //
        // Dois de proposito, e nao um: um passe de jump flood LE os vizinhos e
        // ESCREVE no proprio texel. Ler e escrever a mesma textura no mesmo
        // draw e comportamento indefinido — o mesmo motivo que o bloom desta
        // engine ja tem dois alvos.
        uint32_t  m_SeedFBO[2] = { 0, 0 };
        uint32_t  m_SeedTex[2] = { 0, 0 };
        int       m_SeedFront = 0;

        // VAO vazio para os passes de tela cheia. O triangulo e gerado por
        // gl_VertexID no vertex shader, sem VBO — mesmo truque do skybox e do
        // probe bake. Mas o perfil core exige ALGUM VAO ligado no glDrawArrays,
        // e e so para isso que ele existe.
        uint32_t  m_EmptyVAO = 0;

        uint32_t  m_Resolution = 1024;

        // SCENE_HEIGHT_V6 — a altura da superficie que pediu este mapa. Vem do
        // proprio draw call, e nao de um ajuste de cena.
        float     m_SliceY = 0.0f;
        glm::mat4 m_Matrix{ 1.0f };
        glm::mat4 m_InvMatrix{ 1.0f };
        bool      m_Initialized = false;

        int       m_SavedFBO = 0;
        int       m_SavedViewport[4] = {};

        std::shared_ptr<Shader> m_HeightShader;
        std::shared_ptr<Shader> m_SeedInitShader;
        std::shared_ptr<Shader> m_JumpFloodShader;
    };
}