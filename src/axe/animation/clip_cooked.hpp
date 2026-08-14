#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/animation_clip.hpp"
#include "axe/animation/skeleton.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace axe
{
    // ── Clipes de animacao cozidos (.axeclipbin) — B2.3 ──────────────────────
    //
    // Terceira e ultima fase de dados do B2. Cobre o que sobrou depois do
    // `.axemesh` (malha estatica) e do `.axeskelbin` (malha skinada +
    // esqueleto): as curvas.
    //
    // ── DECISAO: OS CANAIS SAO GRAVADOS POR NOME DE OSSO, NAO POR INDICE ─────
    //
    //   Em memoria, `BoneChannel::BoneIndex` e o indice no esqueleto ALVO — o
    //   religamento acontece no LoadClips, casando por nome via
    //   Skeleton::FindBone.
    //
    //   Cozinhar por indice seria mais rapido e menor, e e o que a palavra
    //   "cozinhar" sugere (pre-resolver tudo). Mas amarraria o arquivo AQUELE
    //   esqueleto: trocar o personagem, ou usar o mesmo `idle.fbx` num segundo
    //   personagem, invalidaria o cozido — e o pior, silenciosamente, porque os
    //   indices continuariam validos e apontariam para os ossos ERRADOS.
    //
    //   Gravando por nome, o cozido tem exatamente a mesma propriedade do FBX
    //   que ele substitui: serve a qualquer esqueleto compativel. O custo e uma
    //   busca por nome por canal no load — desprezivel perto de abrir um FBX.
    //
    // ── O QUE NAO ENTRA ──────────────────────────────────────────────────────
    //
    //   Notifies, RateScale, RootMotion, NotifyTrackCount e Looping sao dados
    //   de AUTORIA e vivem no `.axeskel` (bloco `clip_meta`), aplicados depois
    //   do load. Duplica-los aqui criaria dois lugares de verdade para a mesma
    //   informacao — e o binario, sendo derivado, perderia a disputa toda vez
    //   que o usuario editasse um notify.
    //
    //   `Looping` e gravado mesmo assim, por ser um byte e completar o clipe
    //   para quem ler o arquivo sem o `.axeskel`; o asset sobrescreve com o
    //   valor de autoria.

    struct AXE_API AxeClipHeader
    {
        char          Magic[4];      // 'A','X','C','L'
        std::uint32_t Version;

        // Um arquivo de animacao pode trazer varios takes.
        std::uint32_t ClipCount;

        std::uint32_t Reserved[5];
    };

    static constexpr std::uint32_t kAxeClipVersion = 1;

    class AXE_API ClipCooked
    {
    public:
        // "idle.fbx" -> "idle.axeclipbin".
        //
        // Um arquivo por FBX de animacao, e nao um por personagem: e o mesmo
        // recorte do fonte, entao a invalidacao por data funciona arquivo a
        // arquivo. Um cozido unico por personagem obrigaria a regravar TODAS as
        // animacoes quando o artista mexesse em UMA.
        static std::filesystem::path PathFor(const std::filesystem::path& source);

        static bool IsUpToDate(const std::filesystem::path& source);

        static bool Write(const std::filesystem::path& outPath,
            const std::vector<std::shared_ptr<AnimationClip>>& clips,
            const Skeleton& skeleton);

        // Le e RELIGA ao esqueleto dado. Canais cujo osso nao existe no alvo
        // sao descartados — mesma regra do LoadClips, e o que permite tocar um
        // clipe exportado com ossos extras num esqueleto mais enxuto.
        //
        // Vetor vazio se o arquivo nao existe ou nao serve.
        static std::vector<std::shared_ptr<AnimationClip>> Read(
            const std::filesystem::path& path,
            const Skeleton& targetSkeleton);

        // Atalho: cozido de `source` se estiver em dia, senao vazio.
        static std::vector<std::shared_ptr<AnimationClip>> TryLoadFor(
            const std::filesystem::path& source,
            const Skeleton& targetSkeleton);
    };

} // namespace axe