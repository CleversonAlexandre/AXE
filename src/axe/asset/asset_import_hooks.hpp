#pragma once
#include "axe/core/types.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/animation/skinned_mesh.hpp"
#include "axe/animation/skeleton.hpp"
#include "axe/animation/animation_clip.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace axe
{
    // ── Hooks de importação — B2.4 ───────────────────────────────────────────
    //
    // A fronteira entre "ler um formato de intercâmbio" e "rodar o jogo".
    //
    // ── O PROBLEMA QUE ELES RESOLVEM ─────────────────────────────────────────
    //
    // O B2.1–B2.3 deu ao runtime formatos próprios (.axemesh, .axeskelbin,
    // .axeclipbin) e deixou um FALLBACK para FBX em cada ponto de load. Esse
    // fallback é o que fez a migração ser segura — mas é também o que ainda
    // obriga o `axe.dll` a linkar assimp.
    //
    // Tirar o fallback resolveria o link e quebraria o EDITOR: importar um FBX
    // novo deixaria de funcionar, porque é o mesmo caminho.
    //
    // ── A SAÍDA: INVERTER QUEM DEPENDE DE QUEM ───────────────────────────────
    //
    // O runtime deixa de CHAMAR o importador e passa a OFERECER um ponto onde
    // um importador pode se registrar. O editor registra os dele no boot; o
    // jogo não registra nada.
    //
    //   editor.exe  → hooks preenchidos → FBX abre, cozinha, funciona
    //   game.exe    → hooks vazios      → só cozido, e o assimp nem é linkado
    //
    // O runtime não sabe o que é assimp, nem que existe um importador. Ele sabe
    // que alguém PODE saber.
    //
    // ── POR QUE NÃO SIMPLESMENTE APAGAR O FALLBACK ───────────────────────────
    //
    // Porque o editor precisa dele, e duplicar os pontos de load (um caminho no
    // runtime, outro no editor) criaria duas implementações do mesmo fluxo —
    // com o do jogo sendo, de novo, o que ninguém exercita. É o erro que o SR1 e
    // o SR2 vieram desfazer.
    //
    // Com o hook, o caminho é UM SÓ. O que muda entre editor e jogo é apenas se
    // existe um plano B.
    struct AXE_API AssetImportHooks
    {
        // Malha estática a partir de um arquivo de intercâmbio (FBX, glTF...).
        // nullptr no retorno = não deu.
        using LoadMeshFn = std::function<std::shared_ptr<Mesh>(
            const std::filesystem::path&)>;

        struct SkeletalResult
        {
            std::shared_ptr<SkinnedMesh> Mesh;
            std::shared_ptr<Skeleton>    Skeleton;
            std::vector<std::shared_ptr<AnimationClip>> Clips;
        };

        using LoadSkeletalFn = std::function<SkeletalResult(
            const std::filesystem::path&)>;

        using LoadClipsFn = std::function<std::vector<std::shared_ptr<AnimationClip>>(
            const std::filesystem::path&, const Skeleton&)>;

        static void SetMeshImporter(LoadMeshFn fn);
        static void SetSkeletalImporter(LoadSkeletalFn fn);
        static void SetClipImporter(LoadClipsFn fn);

        // Vazios quando não há importador — o caso do jogo empacotado.
        static bool HasMeshImporter();
        static bool HasSkeletalImporter();
        static bool HasClipImporter();

        // Devolvem vazio/nullptr quando não há importador registrado.
        //
        // Silenciosamente: no jogo, um asset sem cozido é falha de
        // EMPACOTAMENTO, e quem reporta isso é o ponto de load com o nome do
        // asset em mãos — não este módulo, que só sabe que ninguém se
        // registrou.
        static std::shared_ptr<Mesh> ImportMesh(const std::filesystem::path& p);
        static SkeletalResult ImportSkeletal(const std::filesystem::path& p);
        static std::vector<std::shared_ptr<AnimationClip>> ImportClips(
            const std::filesystem::path& p, const Skeleton& target);
    };

} // namespace axe