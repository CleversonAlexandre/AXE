#pragma once
#include "axe/core/types.hpp"
#include "axe/mesh/mesh.hpp"

#include <filesystem>
#include <memory>

namespace axe
{
    // ── Mesh cozida (.axemesh) — B2.1 ────────────────────────────────────────
    //
    // POR QUE ESTE FORMATO EXISTE
    //
    //   Hoje o runtime abre FBX. Isso obriga o `axe.dll` a linkar assimp — uma
    //   DLL de IMPORTACAO dentro do jogo, que le um formato de intercambio de
    //   ferramenta de modelagem toda vez que uma cena carrega.
    //
    //   E o bloqueio B2 do PACKAGING_READINESS, e ele nao se resolve limpando o
    //   link: o runtime GENUINAMENTE precisa do importador enquanto nao houver
    //   um formato proprio para ler.
    //
    //   Este e esse formato. O editor cozinha no import; o runtime le so isto.
    //
    // POR QUE COZINHAR NO IMPORT, E NAO NO EMPACOTAMENTO
    //
    //   Porque assim o caminho cozido e o caminho de TODO DIA. Cozinhar so na
    //   hora de empacotar deixaria um caminho que ninguem exercita ate o dia em
    //   que ele precisa funcionar — o mesmo erro que o SR1 e o SR2 vieram
    //   desfazer, um andar acima.
    //
    //   Efeito colateral bom: abrir uma cena deixa de rodar o importador de FBX.
    //
    // O QUE ELE NAO E
    //
    //   Nao e formato de intercambio nem de arquivo morto. Ele espelha a
    //   memoria do engine e pode mudar entre versoes; o FBX continua sendo a
    //   fonte de verdade, guardado no projeto. Perder um `.axemesh` custa um
    //   reimport, nao um asset.

    struct AXE_API AxeMeshHeader
    {
        char          Magic[4];        // 'A','X','E','M'
        std::uint32_t Version;
        std::uint32_t VertexCount;
        std::uint32_t IndexCount;

        // sizeof(Vertex) no momento da gravacao.
        //
        // ESTE CAMPO E A PROTECAO MAIS IMPORTANTE DO FORMATO. Os vertices sao
        // gravados como bloco cru; se alguem acrescentar um campo em `Vertex`,
        // todo `.axemesh` ja existente passa a ter um layout diferente do que o
        // codigo espera. Sem o stride, a leitura teria sucesso e produziria
        // geometria embaralhada — o pior tipo de falha, porque parece um bug de
        // arte. Com ele, o arquivo e recusado e o fallback reimporta o FBX.
        std::uint32_t VertexStride;

        std::uint32_t Reserved[4];
    };

    static constexpr std::uint32_t kAxeMeshVersion = 1;

    class AXE_API MeshCooked
    {
    public:
        // Extensao do arquivo cozido de um fonte: "foo.fbx" -> "foo.axemesh".
        //
        // Ao lado do fonte, e nao numa pasta de cache: mover ou renomear a
        // pasta de assets leva os dois juntos, e o par fica obvio para quem
        // olha o diretorio.
        static std::filesystem::path PathFor(const std::filesystem::path& source);

        // true se existe um cozido VALIDO e nao mais velho que o fonte.
        //
        // A comparacao de data e o que faz o fluxo continuar natural: o artista
        // substitui o FBX, e o cozido velho e ignorado sozinho.
        static bool IsUpToDate(const std::filesystem::path& source);

        // Grava. Chamado pelo EDITOR no import.
        static bool Write(const std::filesystem::path& outPath, const Mesh& mesh);

        // Le. Devolve nullptr se o arquivo nao existe, tem magic/versao/stride
        // errados, ou esta truncado.
        //
        // Exige contexto OpenGL: o construtor de Mesh sobe os dados para a GPU,
        // igual ao caminho do MeshLoader.
        static std::shared_ptr<Mesh> Read(const std::filesystem::path& path);

        // Atalho do runtime: tenta o cozido de `source`; devolve nullptr se ele
        // nao servir. Quem chama decide o fallback.
        static std::shared_ptr<Mesh> TryLoadFor(const std::filesystem::path& source);
    };

} // namespace axe