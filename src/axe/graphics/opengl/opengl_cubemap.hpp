#pragma once
#include "axe/graphics/cubemap_texture.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
namespace axe
{
    class Shader;

    class OpenGLCubemap : public CubemapTexture
    {
    public:
        OpenGLCubemap() = default;
        ~OpenGLCubemap();

        bool LoadFromHDRI(const std::string& filepath);

        // SKY_IBL_V1 — desenha as 6 faces via callback e gera os mapas de IBL.
        // `generateBRDF` so na primeira vez: a LUT depende da formula da BRDF,
        // nao do conteudo do cubemap.
        bool CaptureFaces(uint32_t faceSize, const FaceDrawFn& drawFace,
            bool generateBRDF);

        bool UpdateFromFaces(const FaceDrawFn& drawFace) override
        {
            return CaptureFaces(m_FaceSize, drawFace, /*generateBRDF*/ false);
        }

        void Bind(uint32_t slot) const override;
        void BindIrradiance(uint32_t slot) const override;
        void BindPrefiltered(uint32_t slot) const override;
        void BindBRDFLut(uint32_t slot) const override;
        bool HasIBL()              const override;

        uint32_t GetRendererID() const override { return m_RendererID; }
        bool     IsLoaded()      const override { return m_Loaded; }

    private:
        void GenerateIrradianceMap(uint32_t captureFBO, uint32_t captureRBO,
            uint32_t cubeVAO, glm::mat4* views,
            const glm::mat4& proj);
        void GeneratePrefilteredMap(uint32_t captureFBO, uint32_t captureRBO,
            uint32_t cubeVAO, glm::mat4* views,
            const glm::mat4& proj);
        void GenerateBRDFLut(uint32_t captureFBO, uint32_t captureRBO);

        uint32_t m_RendererID = 0;
        uint32_t m_IrradianceID = 0;
        uint32_t m_PrefilteredID = 0;
        uint32_t m_BRDFLutID = 0;
        bool     m_Loaded = false;

        // SKY_IBL_V1 — tamanho de face com que este cubemap foi criado, para o
        // rebake reusar exatamente o mesmo: um glTexImage2D com outro tamanho
        // realocaria a textura, e os passes ja seguram o handle dela.
        uint32_t m_FaceSize = 512;

        // SKY_IBL_V1 — shaders de convolucao guardados como MEMBRO, e nao
        // recriados a cada chamada.
        //
        // Antes, cada Generate* chamava Shader::Create. Isso era irrelevante
        // quando o IBL nascia uma vez, no load de um HDRI. Com o ceu
        // procedural o rebake acontece toda vez que o sol se move o bastante —
        // compilar dois GLSL por rebake seria engasgo visivel ao arrastar o
        // sol.
        //
        // Membro, e nao static de funcao, de proposito: morrem junto com o
        // cubemap, enquanto o contexto GL ainda existe.
        std::shared_ptr<Shader> m_IrradianceShader;
        std::shared_ptr<Shader> m_PrefilterShader;
    };
}