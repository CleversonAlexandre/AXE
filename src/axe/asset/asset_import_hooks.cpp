#include "axe/asset/asset_import_hooks.hpp"

namespace axe
{
    namespace
    {
        // Estado de PROCESSO, e nao de cena: quem importa nao muda ao longo da
        // execucao. No editor sao preenchidos uma vez no boot; no jogo, nunca.
        AssetImportHooks::LoadMeshFn     s_Mesh;
        AssetImportHooks::LoadSkeletalFn s_Skeletal;
        AssetImportHooks::LoadClipsFn    s_Clips;
    }

    void AssetImportHooks::SetMeshImporter(LoadMeshFn fn) { s_Mesh = std::move(fn); }
    void AssetImportHooks::SetSkeletalImporter(LoadSkeletalFn fn) { s_Skeletal = std::move(fn); }
    void AssetImportHooks::SetClipImporter(LoadClipsFn fn) { s_Clips = std::move(fn); }

    bool AssetImportHooks::HasMeshImporter() { return (bool)s_Mesh; }
    bool AssetImportHooks::HasSkeletalImporter() { return (bool)s_Skeletal; }
    bool AssetImportHooks::HasClipImporter() { return (bool)s_Clips; }

    std::shared_ptr<Mesh> AssetImportHooks::ImportMesh(const std::filesystem::path& p)
    {
        return s_Mesh ? s_Mesh(p) : nullptr;
    }

    AssetImportHooks::SkeletalResult AssetImportHooks::ImportSkeletal(
        const std::filesystem::path& p)
    {
        return s_Skeletal ? s_Skeletal(p) : SkeletalResult{};
    }

    std::vector<std::shared_ptr<AnimationClip>> AssetImportHooks::ImportClips(
        const std::filesystem::path& p, const Skeleton& target)
    {
        return s_Clips ? s_Clips(p, target) : std::vector<std::shared_ptr<AnimationClip>>{};
    }

} // namespace axe