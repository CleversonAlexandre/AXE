#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <filesystem>
#include <string>
#include <memory>
#include <vector>
#include <map>

namespace axe
{
    class Shader;
    class Texture2D;

    // Um disparo pontual de partículas — complementa (ou substitui) o
    // EmissionRate contínuo. Pode ser repetido N vezes ou infinitamente.
    struct ParticleBurstDef
    {
        float Time = 0.0f;  // segundos desde o início do emitter
        int   Count = 10;    // partículas por disparo
        int   Cycles = 1;     // 1 = uma vez; -1 = infinito
        float Interval = 1.0f;  // segundos entre ciclos (se Cycles != 1)
    };

    // =========================================================================
    // Keyframes para curvas de cor e tamanho ao longo da vida da partícula
    // =========================================================================
    struct ColorKey { float Time = 0.0f; glm::vec4 Color{ 1.0f }; };
    struct SizeKey { float Time = 0.0f; float Size = 1.0f; };

    // =========================================================================
    // ParticleEmitterDef — parâmetros de UM emissor dentro do sistema.
    // =========================================================================
    struct AXE_API ParticleEmitterDef
    {
        std::string Name = "Emitter";
        bool        Enabled = true;

        float Duration = -1.0f;
        bool  AutoDestroy = false;
        float WarmupTime = 0.0f;

        // ── LOD ───────────────────────────────────────────────────
        bool  LODEnabled = false;
        float LODDistanceFull = 20.0f;  // dist <= Full → emission rate 100%
        float LODDistanceZero = 60.0f;  // dist >= Zero → emission rate 0%

        // ── Emissão ───────────────────────────────────────────────
        float EmissionRate = 30.0f;
        int   MaxParticles = 500;

        // ── Vida ──────────────────────────────────────────────────
        float Lifetime = 2.0f;
        float LifetimeVariation = 0.3f;

        // ── Movimento ─────────────────────────────────────────────
        glm::vec3 StartVelocity{ 0.0f, 2.5f, 0.0f };
        glm::vec3 VelocityVariation{ 1.0f, 0.5f, 1.0f };
        glm::vec3 Gravity{ 0.0f, -1.0f, 0.0f };

        // ── Spawn Shape ───────────────────────────────────────────
        int   SpawnShape = 0;   // 0=Point 1=Sphere 2=Ring 3=Cylinder 4=Helix 5=Cone
        float SpawnRadius = 0.5f;
        float SpawnHeight = 2.0f;
        float SpawnHelixTurns = 3.0f;
        float SpawnConeAngle = 25.0f; // half-angle em graus (Shape=5)
        glm::vec3 SpawnOffset{ 0.0f }; // deslocamento local da origem (eixo Y = ao longo do beam)
        bool  SpawnOnSurface = true;
        bool  VelocityFollowsShape = false;

        // ── Aparência ─────────────────────────────────────────────
        glm::vec4 ColorStart{ 1.0f, 0.6f, 0.15f, 1.0f };
        glm::vec4 ColorEnd{ 0.6f, 0.0f, 0.0f,  0.0f };
        float SizeStart = 0.4f;
        float SizeEnd = 0.0f;
        float SizeVariation = 0.2f;
        float StretchAmount = 0.0f;

        // ── Velocity Limit ────────────────────────────────────────
        float VelocityLimit = 0.0f;  // 0 = sem limite

        // ── Rotação inicial ───────────────────────────────────────
        float RotationMin = 0.0f;
        float RotationMax = 6.2831853f; // 2π
        float RotationSpeedMin = -1.5f;
        float RotationSpeedMax = 1.5f;

        // ── Color over Lifetime (curva) ───────────────────────────
        // Se vazio, usa ColorStart→ColorEnd linear (comportamento atual).
        std::vector<ColorKey> ColorCurve;

        // ── Size over Lifetime (curva) ────────────────────────────
        // Se vazio, usa SizeStart→SizeEnd linear (comportamento atual).
        std::vector<SizeKey> SizeCurve;

        // ── Material ──────────────────────────────────────────────
        std::string ParticleMaterialUUID;
        int BlendMode = 1;  // 0 = alpha, 1 = additive

        // ── Burst ─────────────────────────────────────────────────
        std::vector<ParticleBurstDef> Bursts;

        // ── Orbit Force ───────────────────────────────────────────
        // Empurra partículas em torno de um eixo (padrão Y = redemoinho XZ).
        float     OrbitStrength = 0.0f;
        glm::vec3 OrbitAxis{ 0.0f, 1.0f, 0.0f };

        // ── Turbulência ───────────────────────────────────────────
        // Campo de "ruído" aplicado como força por frame.
        // Frequency controla a escala espacial; Speed = quão rápido evolui.
        float TurbulenceStrength = 0.0f;
        float TurbulenceFrequency = 1.0f;
        float TurbulenceSpeed = 1.0f;

        // ── Simulation Space ──────────────────────────────────────
        // false (padrão) = World Space: partículas nascem e ficam no mundo.
        // true = Local Space: partículas seguem o emissor quando ele se move.
        bool LocalSpace = false;

        // ── Colisão com plano ─────────────────────────────────────
        bool  CollisionEnabled = false;
        float CollisionY = 0.0f;   // altura do plano
        float CollisionBounciness = 0.3f;   // 0 = para, 1 = ricochete total
        float CollisionFriction = 0.5f;   // amortece velocidade XZ no impacto
        // Divide a textura do material em uma grade de frames animados.
        // Cada partícula percorre os frames ao longo de sua vida.
        // Só faz sentido quando há um material com Texture Sample conectado.
        bool  FlipbookEnabled = false;
        int   FlipbookCols = 4;     // colunas do spritesheet
        int   FlipbookRows = 4;     // linhas do spritesheet
        float FlipbookCycles = 1.0f;  // voltas completas da animação por vida

        // ── Particle Light ────────────────────────────────────────────────────
        // Adiciona um Point Light dinâmico na origem do emitter.
        // Intensidade pode escalar pelo número de partículas vivas (mais fogo = mais brilho).
        bool      LightEnabled = false;
        glm::vec3 LightColor{ 1.0f, 0.6f, 0.2f };
        float     LightIntensity = 3.0f;
        float     LightRadius = 5.0f;
        bool      LightScaleByParticles = true;
        bool      LightFlicker = false;
        float     LightFlickerSpeed = 8.0f;  // frequência (sin)
        float     LightFlickerAmount = 0.4f;  // 0=sem flicker, 1=apaga completamente

        // ── Sub-emissores ─────────────────────────────────────────────────────
        // UUID de .axepart instanciado onde cada partícula morre.
        // Ex: fogo → fumaça, explosão → faíscas secundárias.
        std::string SubEmitterUUID;

        // Cache runtime — NÃO serializado
        std::shared_ptr<Shader> ParticleMaterialShader;
        std::map<std::string, std::shared_ptr<Texture2D>> ParticleMaterialSamplers;
    };

    // =========================================================================
    // ParticleSystemAsset — contém uma lista de emissores reutilizável.
    // Salvo como .axepart. Compatível com o formato antigo (campo raiz →
    // migrado automaticamente pra um único emitter ao carregar).
    // =========================================================================
    class AXE_API ParticleSystemAsset
    {
    public:
        ParticleSystemAsset() { Emitters.emplace_back(); } // começa com 1 emissor

        const std::string& GetName()     const { return m_Name; }
        void                        SetName(const std::string& n) { m_Name = n; }
        const std::filesystem::path& GetFilePath() const { return m_FilePath; }

        bool Save(const std::filesystem::path& filepath);
        bool Load(const std::filesystem::path& filepath);

        static std::shared_ptr<ParticleSystemAsset> Create(const std::string& name = "NewParticleSystem");
        static std::shared_ptr<ParticleSystemAsset> LoadFromFile(const std::filesystem::path& filepath);

        // ── Controle global ───────────────────────────────────────
        bool Looping = true;

        // ── Lista de emissores ────────────────────────────────────
        std::vector<ParticleEmitterDef> Emitters;

    private:
        std::string           m_Name = "NewParticleSystem";
        std::filesystem::path m_FilePath;
    };

} // namespace axe