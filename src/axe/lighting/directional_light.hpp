#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <string>
#include <map>

namespace axe
{
    class Texture2D;
    class Shader;

    struct AXE_API DirectionalLight
    {
        glm::vec3 Direction{ -0.3f, -1.0f, -0.3f };
        glm::vec3 Color{ 1.0f,  1.0f,  1.0f };

        float Intensity = 1.0f;
        float AmbientStrength = 0.15f;
        float IBLIntensity = 1.0f;   // multiplicador global do IBL difuso + especular
        float SpecularStrength = 0.5f;

        // 0 = ambient respeitado por sombras (interior escuro)
        // 1 = ambient livre, ilumina tudo (padrão, exterior)
        float AmbientShadowFactor = 1.0f;
        float Shininess = 32.0f;

        // Shadow mapping
        bool  CastShadows = true;
        float ShadowDistance = 50.0f; // tamanho do frustum ortográfico
        float ShadowBias = 0.005f;

        // Cookie — textura projetada num plano perpendicular à direção da
        // luz (paralela/infinita, como o sol, então a projeção é por
        // tiling, não por cone). CookieScale é o tamanho de um "tile" da
        // textura, em unidades de mundo.
        std::shared_ptr<Texture2D> CookieTexture;
        std::string CookieTextureUUID;
        float CookieScale = 5.0f;

        // Light Material — ver comentário equivalente em PointLight.
        std::string LightMaterialUUID;
        std::shared_ptr<Shader> LightMaterialShader;
        std::map<std::string, std::shared_ptr<Texture2D>> LightMaterialSamplers;

        // ── Céu Procedural + Ciclo Dia/Noite ─────────────────────────────────
        // O Directional Light é o sol — faz sentido controlar o céu aqui.
        // Quando ProceduralSky = true, o skybox usa atmosfera procedural
        // com a direção desta luz como posição do sol.
        bool  ProceduralSky = false;

        bool  TimeOfDayEnabled = false;
        float Hour = 12.0f;  // 0=meia-noite, 12=meio-dia, 24=meia-noite
        float DaySpeed = 1.0f;   // 1=tempo real, 60=1min/seg
        float SunLatitude = -23.0f;

        float Turbidity = 2.5f;
        float CloudCoverage = 0.4f;
        float CloudSpeed = 0.015f;
        glm::vec3 CloudColor{ 1.f, 1.f, 1.f };
        glm::vec3 NightColor{ 0.005f, 0.005f, 0.02f };

        // Resultado da última avaliação do Light Material — TRANSITÓRIO,
        // recalculado do zero a cada frame (nunca serializado, nunca
        // multiplicado em si mesmo). Necessário porque, diferente da Point
        // Light, esta struct é referenciada por ponteiro direto pelo
        // SceneCollector (não copiada por frame) — multiplicar Color
        // direto aqui composto a cada frame seria um bug (a cor iria pra
        // preto ou explodiria com o tempo). O Lighting Pass multiplica
        // Color por este valor na hora de fazer upload do uniform.
        glm::vec3 LightMaterialResult{ 1.0f, 1.0f, 1.0f };
    };
}