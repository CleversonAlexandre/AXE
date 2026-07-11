#pragma once
#include "axe/graphics/renderer/cascaded_shadow_pass.hpp"
#include <memory>
#include <array>

namespace axe
{
    class Shader;

    class OpenGLCascadedShadowPass final : public CascadedShadowPass
    {
    public:
        OpenGLCascadedShadowPass() = default;
        ~OpenGLCascadedShadowPass() override;

        void Initialize(uint32_t resolution = 2048) override;
        bool IsInitialized() const override { return m_Initialized; }

        void ComputeCascades(
            const glm::vec3& lightDir,
            const glm::mat4& cameraView,
            const glm::mat4& cameraProj,
            float cameraNear, float cameraFar) override;

        void Begin(int cascadeIndex) override;
        void DrawMesh(const Mesh& mesh, const glm::mat4& model) override;
        void End() override;

        int      GetCascadeCount() const override { return AXE_SHADOW_CASCADES; }
        uint32_t GetDepthArrayID() const override { return m_DepthArrayTex; }
        const std::array<CascadeData, AXE_SHADOW_CASCADES>& GetCascades() const override { return m_Cascades; }

    private:
        glm::mat4 ComputeCascadeMatrix(
            const glm::vec3& lightDir,
            const glm::mat4& cameraView,
            const glm::mat4& cameraProj,
            float nearSplit, float farSplit);

        std::shared_ptr<Shader> m_Shader;
        uint32_t m_FBO = 0;

        // Estado salvo pelo Begin() e restaurado pelo End() — mesmo padrão
        // do OpenGLShadowMapPass. Sem isso, o End() deixava FBO 0 bindado
        // e o viewport em 2048x2048: no deferred o Geometry Pass mascarava
        // (rebinda o G-Buffer logo depois), mas no FORWARD as meshes iam
        // parar no backbuffer — foi o bug que apagou TODOS os previews
        // (material, particle, script e thumbnail) de uma vez.
        int m_SavedFBO = 0;
        int m_SavedViewport[4] = { 0, 0, 0, 0 };
        uint32_t m_DepthArrayTex = 0; // GL_TEXTURE_2D_ARRAY com AXE_SHADOW_CASCADES layers
        uint32_t m_Resolution = 2048;
        bool     m_Initialized = false;

        std::array<CascadeData, AXE_SHADOW_CASCADES> m_Cascades;
        float m_CascadeSplitLambda = 0.75f; // mistura linear/log dos splits
    };

} // namespace axe