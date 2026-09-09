#pragma once
#include "axe/core/types.hpp"
#include "axe/renderer/volumetric_fog_pass.hpp"
#include "axe/graphics/renderer/taa_pass.hpp"
#include "axe/graphics/renderer/ssr_pass.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <string>
#include <cstdint>

namespace axe
{
    // ═════════════════════════════════════════════════════════════════════════
    //  POSTPROCESS_DOMAIN_V1 — ONDE o efeito do usuario entra na cadeia
    //
    //  A cadeia e: HDR linear -> bloom -> TONE MAPPING + gamma -> tela. Os dois
    //  lados dessa fronteira servem para coisas diferentes, e forcar um so
    //  deixaria metade dos efeitos impossiveis:
    //
    //  BeforeTonemap (HDR, linear) — a cor ainda tem faixa alem de 1.0 e ainda
    //  e fisicamente proporcional a luz. E onde faz sentido color grading que
    //  respeita exposicao, sujeira de lente, aberracao cromatica, brilho que
    //  estoura de verdade. Um efeito aqui SOBREVIVE ao tone mapping.
    //
    //  AfterTonemap (LDR, ja em gamma) — a cor e o pixel final, 0..1. E onde
    //  vive o estilizado: pixelizacao, posterizacao, scanline, vinheta, CRT,
    //  sharpen, LUT de cor. Um efeito destes feito em HDR daria resultado
    //  imprevisivel, porque estaria quantizando numeros que ainda vao passar
    //  por uma curva.
    //
    //  Regra pratica: se o efeito imita OTICA, e antes; se ele imita uma TELA,
    //  e depois.
    // ═════════════════════════════════════════════════════════════════════════
    enum class PostProcessBlendPoint : int
    {
        BeforeTonemap = 0,
        AfterTonemap = 1,
    };

    struct PostProcessSettings
    {
        // Tone Mapping
        float Exposure = 1.0f;
        int   ToneMapMode = 1;   // 0 = Reinhard, 1 = ACES

        // Bloom
        bool  BloomEnabled = false;
        float BloomThreshold = 0.7f;
        float BloomIntensity = 0.5f;
        int   BloomBlurPasses = 5;

        // TAA
        TAASettings TAA;

        // SSR — Screen Space Reflections
        SSRSettings SSR;

        // Volumetric Fog
        VolumetricFogSettings Fog;


        // ── POSTPROCESS_DOMAIN_V1 — material de efeito do usuario ────────────
        //
        // UUID de um `.axemat` com Material Domain = Post Process. Vazio = a
        // cadeia e exatamente a de antes desta rodada.
        //
        // Mora AQUI, e nao num campo novo do renderer, porque `PostProcessSettings`
        // ja e o que atravessa do PostProcessComponent ate o passe — e o editor
        // (viewport_renderer) e o jogo (world_renderer) ja copiam esta struct
        // inteira. Um campo aqui chega nos dois caminhos sem uma linha nova de
        // encanamento em nenhum dos dois.
        //
        // O SHADER nao viaja aqui de proposito: quem resolve UUID -> Shader e o
        // MaterialShaderCache, dentro do proprio passe. E o mesmo desenho ja
        // usado por Light Function e por Particle, e e o que faz o efeito
        // funcionar no jogo empacotado (onde nao ha editor para compilar o
        // grafo: le-se o `.axeshader` cozido).
        std::string UserMaterialUUID;

        PostProcessBlendPoint UserBlendPoint = PostProcessBlendPoint::AfterTonemap;

        // 0..1 — mistura entre a imagem original e a do efeito. Existe para que
        // o efeito seja AJUSTAVEL sem reescrever o shader, e para poder ser
        // animado pelo Sequencer como qualquer outro numero.
        float UserIntensity = 1.0f;
    };

    class AXE_API PostProcessPass
    {
    public:
        virtual ~PostProcessPass() = default;

        virtual void Initialize(uint32_t width, uint32_t height) = 0;
        virtual void Resize(uint32_t width, uint32_t height) = 0;

        // Recebe o color attachment HDR, escreve no framebuffer atual (0 = tela)
        virtual void Execute(uint32_t hdrColorID,
            const PostProcessSettings& settings) = 0;

        // ── POSTPROCESS_GBUFFER_V1 ───────────────────────────────────────────
        //
        // Texturas do G-Buffer que o material de efeito pode LER. Sem elas o
        // dominio Post Process so enxerga a cor final — e um efeito que so
        // enxerga cor nao consegue escrever contorno, profundidade de campo,
        // fog por distancia, mascara por tipo de material, nada disso.
        //
        // SETTER, e nao parametro do Execute, para que quem nao tem G-Buffer
        // (o preview do Material Editor roda forward) simplesmente nao chame:
        // as texturas ficam 0 e os nodes de cena devolvem valor neutro.
        //
        // Chamado pelo SceneRenderer, que e quem tem o G-Buffer em maos.
        virtual void SetSceneBuffers(uint32_t positionTex,
            uint32_t normalTex,
            uint32_t pbrTex) {}

        // ── POSTPROCESS_SKY_V1 ───────────────────────────────────────────────
        //
        // O que o efeito precisa saber sobre a CAMERA e sobre o SOL neste
        // frame. E o que falta para estilizar o CEU no grafo.
        //
        // O ceu nao esta no G-Buffer: ele e desenhado DEPOIS do lighting pass
        // (scene_renderer.cpp), entao aqueles pixels nao tem normal, nem
        // posicao, nem shading model. Detecta-los e facil (normal de
        // comprimento zero) — o que faltava era saber PARA ONDE cada um deles
        // esta olhando. Sem a direcao do raio, um efeito so consegue tratar o
        // ceu como uma mancha 2D: nada de banda por elevacao, brilho na
        // direcao do sol, ou gradiente horizonte-zenite.
        //
        // A inversa da view-projection e o que converte pixel -> raio do
        // mundo. Uma mat4 por frame.
        struct CameraParams
        {
            glm::mat4 InvViewProjection{ 1.0f };
            glm::vec3 CameraPosition{ 0.0f };
            float     TimeSeconds = 0.0f;

            glm::vec3 SunDirection{ 0.0f, -1.0f, 0.0f };  // aponta PARA baixo
            glm::vec3 SunColor{ 1.0f };
            float     SunIntensity = 1.0f;
        };

        virtual void SetCameraParams(const CameraParams&) {}

        virtual bool IsInitialized() const = 0;

        static std::shared_ptr<PostProcessPass> Create();
    };
}