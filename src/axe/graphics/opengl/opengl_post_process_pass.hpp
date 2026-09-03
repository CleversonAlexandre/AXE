#pragma once
#include "axe/graphics/renderer/post_process_pass.hpp"
#include "axe/graphics/texture.hpp"

#include <map>
#include <string>

namespace axe
{
    class Shader;

    class OpenGLPostProcessPass final : public PostProcessPass
    {
    public:
        ~OpenGLPostProcessPass() override;

        void Initialize(uint32_t width, uint32_t height) override;
        void Resize(uint32_t width, uint32_t height)     override;
        void Execute(uint32_t hdrColorID,
            const PostProcessSettings& settings) override;

        // POSTPROCESS_GBUFFER_V1
        void SetSceneBuffers(uint32_t positionTex, uint32_t normalTex,
            uint32_t pbrTex) override
        {
            m_ScenePositionTex = positionTex;
            m_SceneNormalTex = normalTex;
            m_ScenePBRTex = pbrTex;
        }

        // POSTPROCESS_SKY_V1
        void SetCameraParams(const CameraParams& p) override { m_Camera = p; }

        bool IsInitialized() const override { return m_Initialized; }

    private:
        void SetupQuad();
        void SetupBloomBuffers(uint32_t width, uint32_t height);

        // ── POSTPROCESS_DOMAIN_V1 ────────────────────────────────────────────
        //
        // Alvo intermediario: o efeito do usuario le uma textura e escreve
        // noutra. Nao da para ler e escrever na MESMA (comportamento
        // indefinido no OpenGL), e o alvo final do tonemap e o framebuffer de
        // quem chamou — que pode nem ser textura.
        void SetupUserBuffer(uint32_t width, uint32_t height);

        // Resolve o `.axemat` do usuario em Shader, uma vez por UUID.
        //
        // No EDITOR compila o grafo; no JOGO le o `.axeshader` cozido. Quem
        // sabe essa diferenca e o MaterialShaderCache — o mesmo caminho de
        // Light Function e de Particle. Este passe so pergunta.
        //
        // A cache local por UUID existe porque Execute roda TODO FRAME: sem
        // ela, seria uma busca no mapa e seis copias de shared_ptr por frame
        // para um valor que quase nunca muda.
        std::shared_ptr<Shader> ResolveUserShader(const std::string& uuid);

        // Desenha o efeito do usuario de `srcTex` para o FBO ja bindado.
        // `isHDR` diz ao shader em que espaco a cor esta — a mesma informacao
        // que o autor precisa para decidir se pode assumir 0..1.
        void DrawUserEffect(uint32_t srcTex, const PostProcessSettings& s, bool isHDR);

        // Quad fullscreen
        uint32_t m_QuadVAO = 0;
        uint32_t m_QuadVBO = 0;

        // Bloom — dois ping-pong FBOs para blur gaussiano
        uint32_t m_BloomFBO[2] = {};
        uint32_t m_BloomColorTex[2] = {};

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        bool     m_Initialized = false;

        std::shared_ptr<Shader> m_TonemapShader;
        std::shared_ptr<Shader> m_BloomExtractShader;
        std::shared_ptr<Shader> m_BlurShader;
        std::shared_ptr<Shader> m_BloomCompositeShader;

        // POSTPROCESS_DOMAIN_V1 — alvo intermediario + cache do material.
        uint32_t m_UserFBO = 0;
        uint32_t m_UserTex = 0;

        // POSTPROCESS_GBUFFER_V1 — 0 = indisponivel (preview forward, por ex.)
        uint32_t m_ScenePositionTex = 0;
        uint32_t m_SceneNormalTex = 0;
        uint32_t m_ScenePBRTex = 0;

        // POSTPROCESS_SKY_V1 — camera e sol do frame corrente.
        CameraParams m_Camera;

        std::string             m_UserShaderUUID;   // qual UUID esta em cache
        std::uint64_t           m_UserShaderGen = 0; // geracao do MaterialShaderCache
        std::shared_ptr<Shader> m_UserShader;
        std::map<std::string, std::shared_ptr<Texture2D>> m_UserSamplers;
    };
}