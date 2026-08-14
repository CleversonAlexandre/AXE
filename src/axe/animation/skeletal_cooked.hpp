#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/skinned_mesh.hpp"
#include "axe/animation/skeleton.hpp"

#include <filesystem>
#include <memory>

namespace axe
{
    // ── Malha skinada + esqueleto cozidos (.axeskelbin) — B2.2 ───────────────
    //
    // Segunda fase do B2. O B2.1 cozinhou a malha ESTATICA; esta cozinha o que
    // o personagem precisa: vertices com pesos, indices e o esqueleto inteiro.
    //
    // POR QUE UM ARQUIVO SEPARADO, E NAO DENTRO DO .axeskel
    //
    //   O `.axeskel` e JSON, e binario nao entra em JSON sem base64 — que
    //   incharia ~33% e ainda teria que ser decodificado no load. A alternativa
    //   seria transformar o `.axeskel` num container (header binario + JSON
    //   embutido), o que resolveria em um arquivo so mas custaria a
    //   legibilidade: hoje da para abrir um `.axeskel` no bloco de notas e ver
    //   os sockets e os notifies, e isso ja salvou depuracao mais de uma vez.
    //
    //   Entao: `.axeskel` continua sendo o JSON de autoria (sockets, ClipMeta,
    //   lista de animacoes), e o `.axeskelbin` ao lado carrega os dados
    //   pesados. Mesma logica do `.axemesh` ficar ao lado do `.fbx`.
    //
    // O QUE VAI E O QUE NAO VAI
    //
    //   VAI: SkinnedMesh (vertices, indices) e Skeleton (bones em ordem
    //   topologica, inverse/local bind pose, global inverse transform, unit
    //   scale, nome).
    //
    //   NAO VAI: os AnimationClips. Eles sao o B2.3, e sao dados de outra
    //   natureza — vem de ARQUIVOS SEPARADOS (idle.fbx, run.fbx), religados
    //   por nome de osso. Misturar os dois neste arquivo obrigaria a
    //   regravar a malha inteira toda vez que uma animacao mudasse.

    struct AXE_API AxeSkelHeader
    {
        char          Magic[4];        // 'A','X','S','K'
        std::uint32_t Version;
        std::uint32_t VertexCount;
        std::uint32_t IndexCount;

        // sizeof(SkinnedVertex) na gravacao. Mesma protecao do .axemesh:
        // sem ela, acrescentar um campo em SkinnedVertex faria a leitura ter
        // SUCESSO e produzir um personagem deformado — falha que parece
        // problema de rigging, e nao de formato.
        std::uint32_t VertexStride;

        std::uint32_t BoneCount;
        std::uint32_t Reserved[3];
    };

    static constexpr std::uint32_t kAxeSkelVersion = 1;

    class AXE_API SkeletalCooked
    {
    public:
        // "Char.axeskel" -> "Char.axeskelbin".
        //
        // Derivado do .axeskel, e nao do FBX: um mesmo FBX pode originar mais
        // de um .axeskel (variacoes de socket, por exemplo), e os cozidos
        // colidiriam.
        static std::filesystem::path PathFor(const std::filesystem::path& axeskel);

        // true se ha cozido e ele nao e mais velho que o FONTE (o FBX).
        //
        // Compara com o FBX, nao com o .axeskel: editar um socket regrava o
        // .axeskel e nao muda um vertice sequer. Invalidar ali forcaria um
        // reimport de FBX a cada mexida na janela de sockets.
        static bool IsUpToDate(const std::filesystem::path& axeskel,
            const std::filesystem::path& source);

        static bool Write(const std::filesystem::path& outPath,
            const SkinnedMesh& mesh,
            const Skeleton& skeleton);

        // Devolve mesh e esqueleto, ou {nullptr, nullptr}.
        //
        // Exige contexto OpenGL: o construtor de SkinnedMesh sobe os dados e
        // cria o VAO, igual ao caminho do importador.
        struct Result
        {
            std::shared_ptr<SkinnedMesh> Mesh;
            std::shared_ptr<Skeleton>    Skeleton;

            bool IsValid() const
            {
                return Mesh && Skeleton && !Skeleton->IsEmpty();
            }
        };

        static Result Read(const std::filesystem::path& path);
    };

} // namespace axe