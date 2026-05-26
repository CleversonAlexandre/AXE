#pragma once
#include "axe/graphics/cubemap_texture.hpp"
#include "axe/utils/glm_config.hpp"
namespace axe
{
    class OpenGLCubemap : public CubemapTexture
    {
    public:
        OpenGLCubemap() = default;
        ~OpenGLCubemap();

        bool LoadFromHDRI(const std::string& filepath);

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
    };
}