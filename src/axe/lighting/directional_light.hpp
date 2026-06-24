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