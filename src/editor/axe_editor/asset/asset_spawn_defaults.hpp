#pragma once

#include "axe/asset/asset.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"
#include "axe/physics/physics_components.hpp"

#include <entt/entt.hpp>
#include <memory>
#include <string>

namespace axe
{
    // ═══════════════════════════════════════════════════════════════════════
    //  ASSET_DEFAULTS_V1 — o que um asset leva consigo para a cena
    //
    //  ── O PROBLEMA ────────────────────────────────────────────────────────
    //
    //  Existem OITO lugares no editor que criam um MeshComponent a partir de
    //  um asset: arrastar para o viewport, arrastar para a hierarquia, duplo
    //  clique no Asset Browser, os caminhos de prefab e de anexo, e por aí.
    //  Aplicar "material padrao e collider" em cada um deles seria escrever a
    //  mesma regra oito vezes — e o compilador nao confere nenhuma delas.
    //
    //  Essa armadilha ja mordeu esta engine mais de uma vez (as listas de tipo
    //  do snapshot, a conversao de camera dos previews). O sintoma e sempre o
    //  mesmo e sempre horrivel de achar: funciona quando voce arrasta de um
    //  jeito e nao funciona de outro, sem erro nenhum no meio.
    //
    //  ── O DESENHO ─────────────────────────────────────────────────────────
    //
    //  A regra mora aqui, uma vez. Cada ponto de spawn chama UMA linha, depois
    //  de preencher o MeshComponent. Um ponto de spawn novo que esqueca a
    //  chamada perde o padrao — mas nao diverge em silencio, porque nao ha
    //  segunda copia da regra para discordar da primeira.
    //
    //  ── O QUE ELE NAO FAZ ─────────────────────────────────────────────────
    //
    //  Nao toca em nada que ja esta na cena. As settings sao um PADRAO DE
    //  CRIACAO: o que nasce e um MaterialComponent e um ColliderComponent
    //  comuns, que o Inspector edita como sempre. Mudar o meta de um asset nao
    //  reescreve uma cena salva — se reescrevesse, uma configuracao de
    //  importacao poderia alterar o trabalho de alguem pelas costas.
    // ═══════════════════════════════════════════════════════════════════════
    class AssetSpawnDefaults
    {
    public:
        // Aplica os padroes do asset a uma entidade recem-criada.
        //
        // Chamar DEPOIS de preencher o MeshComponent: o collider e derivado
        // dos bounds da malha, e sem ela nao ha de onde tirar tamanho.
        //
        // Silenciosa e idempotente: asset sem UUID, sem registro ou sem nada
        // configurado nao faz coisa alguma. Isso e o que permite chamar dos
        // oito pontos sem cada um precisar saber se ha o que aplicar.
        static void Apply(entt::registry& registry, entt::entity entity,
            const std::string& assetUUID);

        // Carrega o material de um asset `.axemat` pelo UUID.
        //
        // ── POR QUE ESTA FUNCAO EXISTE ────────────────────────────────────
        //
        // Carregar um material de asset nao e "ler o arquivo": e ler o
        // `.axemat`, disparar o callback de recompilacao do shader, e ainda
        // buscar no `.axegraph` irmao a textura do primeiro Texture Sample
        // conectado, porque o `.axemat` sozinho nao a carrega.
        //
        // Essa sequencia ja existia dentro do callback de instanciacao do
        // EditorLayer, escrita inline. Agora ela mora aqui e aquele callback
        // chama daqui — uma verdade so. Ate porque um material que carrega
        // diferente conforme o CAMINHO que o carregou e exatamente a forma de
        // bug que estamos caçando na build empacotada.
        static std::shared_ptr<Material> ResolveMaterial(const std::string& materialUUID);

        // Monta um collider a partir dos bounds da malha.
        //
        // Separada do Apply para o Asset Viewer poder MOSTRAR o collider no
        // preview sem criar entidade nenhuma — ver o volume antes de aceita-lo
        // e metade do valor de configurar colisao por asset.
        //
        // Devolve false quando nao ha o que montar (forma -1, malha sem
        // vertices em CPU).
        static bool BuildCollider(const Mesh& mesh, const AssetImportSettings& imp,
            ColliderComponent& out);
    };
}
