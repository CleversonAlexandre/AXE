#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/graphics/cubemap_texture.hpp"
#include <memory>
#include <glm/glm.hpp>

namespace axe
{
    class Shader;
    class VertexArray;

    class AXE_API SkyboxRenderer
    {
    public:
        void Initialize();
        bool HasCubemap() const { return m_Cubemap != nullptr; }
        void SetCubemap(std::shared_ptr<CubemapTexture> cubemap) { m_Cubemap = cubemap; }

        // ── Céu procedural ───────────────────────────────────────────────────
        void SetProceduralSky(bool enabled) { m_UseProceduralSky = enabled; }
        bool IsProceduralSky() const { return m_UseProceduralSky; }

        void SetSunDirection(const glm::vec3& dir) { m_SunDirection = glm::normalize(dir); }
        void SetTurbidity(float t) { m_Turbidity = t; }
        void SetCloudCoverage(float c) { m_CloudCoverage = c; }
        void SetCloudSpeed(float s) { m_CloudSpeed = s; }
        void SetCloudColor(const glm::vec3& c) { m_CloudColor = c; }
        void SetNightColor(const glm::vec3& c) { m_NightColor = c; }
        void Tick(float dt) { m_Time += dt; }

        void Render(const glm::mat4& view, const glm::mat4& projection);
        void RenderDeferred(const glm::mat4& view, const glm::mat4& projection);

    private:
        void DrawSky(const glm::mat4& view, const glm::mat4& proj);

        std::shared_ptr<Shader>         m_Shader;      // HDRI
        std::shared_ptr<Shader>         m_ProcShader;  // procedural
        std::shared_ptr<VertexArray>    m_VertexArray;
        std::shared_ptr<CubemapTexture> m_Cubemap;

        bool      m_UseProceduralSky = false;
        glm::vec3 m_SunDirection{ 0.5f, 0.5f, 0.5f };
        float     m_Turbidity = 2.5f;
        float     m_CloudCoverage = 0.5f;
        float     m_CloudSpeed = 0.02f;
        glm::vec3 m_CloudColor{ 1.f, 1.f, 1.f };
        glm::vec3 m_NightColor{ 0.01f, 0.01f, 0.03f };
        float     m_Time = 0.f;
    };

} // namespace axe