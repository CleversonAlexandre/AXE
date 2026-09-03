#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <functional>
#include <memory>
#include <string>
#include <cstdint>

namespace axe
{
    class AXE_API CubemapTexture
    {
    public:
        virtual ~CubemapTexture() = default;

        // ── SKY_IBL_V1 ───────────────────────────────────────────────────────
        //
        // Callback que desenha UMA face do cubemap. Recebe a view e a
        // projection da face (90 graus, a partir do centro) e desenha o que
        // quiser, com o framebuffer JA bindado e o viewport JA ajustado.
        //
        // Existe para que o CEU PROCEDURAL possa virar cubemap sem que o GLSL
        // dele seja copiado para ca. Quem sabe desenhar o ceu e o
        // SkyboxRenderer, e ele continua sendo o unico dono daquele shader —
        // o callback e literalmente o DrawSky dele. Se o ceu mudar, o cubemap
        // muda junto, porque e o MESMO codigo. Duplicar o shader aqui seria
        // garantir que um dia os dois divergissem.
        using FaceDrawFn = std::function<void(const glm::mat4& view,
            const glm::mat4& projection)>;

        virtual void Bind(uint32_t slot = 0) const = 0;

        // IBL
        virtual void BindIrradiance(uint32_t slot) const = 0;
        virtual void BindPrefiltered(uint32_t slot) const = 0;
        virtual void BindBRDFLut(uint32_t slot) const = 0;
        virtual bool HasIBL() const = 0;

        virtual uint32_t GetRendererID() const = 0;
        virtual bool     IsLoaded()      const = 0;

        // SKY_IBL_V1 — re-renderiza as 6 faces e REFAZ irradiance e
        // prefiltered. O BRDF LUT nao e refeito: ele so depende da formula da
        // BRDF, nao do conteudo do cubemap — regerar seria trabalho puro.
        //
        // Chamado quando o sol se move: o OBJETO de cubemap continua o mesmo,
        // so o conteudo muda. Recriar o objeto a cada rebake vazaria textura
        // de GPU e invalidaria os ponteiros que os passes ja seguram.
        virtual bool UpdateFromFaces(const FaceDrawFn& drawFace) = 0;

        static std::shared_ptr<CubemapTexture> CreateFromHDRI(const std::string& filepath);

        // SKY_IBL_V1 — cria um cubemap desenhado pelo callback e ja gera os
        // tres mapas de IBL. `faceSize` pequeno de proposito: irradiancia e
        // ultra-baixa frequencia, e 64 por face ja e mais detalhe do que a
        // convolucao cosseno consegue carregar.
        static std::shared_ptr<CubemapTexture> CreateFromFaces(uint32_t faceSize,
            const FaceDrawFn& drawFace);
    };
}