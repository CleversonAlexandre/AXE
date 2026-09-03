#pragma once
// material_shader_cache.hpp — shader + samplers de um material, resolvido UMA
// vez por UUID.
//
// ── O PROBLEMA QUE ELE RESOLVE ───────────────────────────────────────────────
//
// Resolver um material de light function ou de particula custa caro: no editor
// e ler o `.axegraph`, rodar o MaterialCompiler e chamar `Shader::Create`; no
// jogo e ler o `.axeshader` e chamar `Shader::Create`. Nos dois casos, um
// compile de GLSL.
//
// No load de cena isso acontece uma vez e ninguem sente. Mas ha dois lugares
// que criam ParticleSystemComponent EM TEMPO DE JOGO:
//
//   ParticleWorld  — spawn de sub-emissores (pode nascer a cada poucos frames)
//   AnimationWorld — FX de AnimNotify (a cada passo do personagem)
//
// Sem cache, resolver o material nesses pontos significaria um compile de
// shader por spawn. Era exatamente por isso que os dois NAO resolviam material
// nenhum e caiam no shader padrao do ParticleRenderer — no editor e no jogo.
//
// ── COMO FUNCIONA ────────────────────────────────────────────────────────────
//
// `Resolve` responde a mesma pergunta que os callbacks do SceneSerializer, com
// a mesma regra de quem manda:
//
//   EDITOR — o callback de recompile existe: compila do grafo (fonte da
//            verdade, porque o grafo pode ter mudado).
//   JOGO   — callback nulo: le o `.axeshader` cozido (PKG9).
//
// A diferenca e que o resultado fica guardado por (UUID, dominio). O segundo
// spawn nao paga nada.
//
// ── INVALIDACAO ──────────────────────────────────────────────────────────────
//
// Um cache que nunca esquece mente no editor: recompilar um material e nao ver
// a mudanca no proximo spawn seria pior que o custo que ele evita. Por isso:
//
//   Invalidate(uuid) — o Material Editor chama ao compilar/salvar.
//   Clear()          — o SceneRuntime chama no OnStart, para uma sessao de Play
//                      nunca comecar com shader de uma sessao anterior.
//
// No jogo nada invalida, e esta certo: o cozido nao muda enquanto o jogo roda.
//
// ── FRONTEIRA ────────────────────────────────────────────────────────────────
//
// Runtime puro. Consulta o callback (que so o editor registra) atraves do
// SceneSerializer, exatamente como o SceneSerializer ja fazia — nenhum header
// de `src/editor/` entra aqui.

#include "axe/core/types.hpp"
#include "axe/material/material_cooked.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace axe
{
    class Shader;
    class Texture2D;

    class AXE_API MaterialShaderCache
    {
    public:
        // false = nao foi possivel resolver (sem callback e sem cozido). O
        // chamador decide o que fazer — no caso das particulas, cair no shader
        // padrao, que e o comportamento de sempre.
        static bool Resolve(const std::string& materialAssetUUID,
            CookedMaterialDomain domain,
            std::shared_ptr<Shader>& outShader,
            std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers);

        static void Invalidate(const std::string& materialAssetUUID);
        static void Clear();

        // POSTPROCESS_DOMAIN_V1 — contador que sobe a cada Invalidate/Clear.
        //
        // Existe porque este cache passou a ter um SEGUNDO nivel de cache em
        // cima dele: o OpenGLPostProcessPass guarda o shader do efeito por
        // UUID, para nao refazer a busca todo frame. Sem um jeito de saber que
        // algo foi invalidado, esse cache de cima continuaria devolvendo o
        // shader ANTIGO — o usuario recompilaria o material e nao veria
        // diferenca nenhuma ate reabrir a cena.
        //
        // Comparar dois inteiros e o preco de manter os dois em dia.
        static std::uint64_t Generation();
    };

} // namespace axe