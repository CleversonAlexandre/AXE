#pragma once
#include "axe/core/types.hpp"
#include "axe/asset/asset_dependency_graph.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace axe
{
    // ── GamePackager — PKG1 ──────────────────────────────────────────────────
    //
    // Gera a pasta do jogo: binarios, assets alcancaveis e um `.axeproject`
    // reduzido.
    //
    // ── O QUE ELE E, E O QUE NAO E ───────────────────────────────────────────
    //
    // E uma COPIA SELETIVA. Nao converte nada, nao gera formato novo, nao
    // otimiza. Tudo o que o jogo precisa ler ja esta no formato final desde o
    // B2 — os `.axemesh`, `.axeskelbin` e `.axeclipbin` sao gerados no import,
    // nao aqui.
    //
    // Isso foi decidido no B2 e continua valendo: cozinhar no empacotamento
    // deixaria um caminho que so e exercitado no dia do empacotamento. Aqui,
    // se um asset chega sem cozido, isso e ERRO reportado — nao um passo de
    // conversao escondido.
    //
    // ── A ESTRUTURA DE DESTINO ESPELHA A ORIGEM ──────────────────────────────
    //
    //   <destino>/game.exe, axe.dll, ...
    //   <destino>/MeuJogo.axeproject
    //   <destino>/axe_assets.json        <- o manifesto
    //   <destino>/Assets/...             <- mesma arvore relativa
    //
    // Espelhar nao e preguica. Varios assets se referenciam por caminho
    // RELATIVO — o `.axeskel` aponta para o `.fbx`, o material para o
    // `.axegraph`. Reorganizar a arvore (achatar, agrupar por tipo) exigiria
    // reescrever esses caminhos dentro dos arquivos, e cada um esquecido seria
    // um asset faltando no jogo.
    class AXE_API GamePackager
    {
    public:
        struct Options
        {
            std::filesystem::path OutputDir;

            // Pasta com `game.exe` e `axe.dll` (tipicamente `bin/<cfg>/game`).
            // Vazia = so os assets sao copiados.
            std::filesystem::path BinariesDir;

            // Apaga a pasta de destino antes de copiar.
            //
            // Padrao DESLIGADO: apagar arvore de diretorio a partir de um
            // caminho digitado pelo usuario e a operacao mais destrutiva que
            // esta janela pode fazer, e ela nao deve acontecer por descuido.
            bool CleanOutput = false;

            // UUIDs a incluir mesmo sem ninguem referenciar — o asset que um
            // script resolve por nome em tempo de execucao.
            std::vector<std::string> ForcedUUIDs;
        };

        struct Result
        {
            bool Success = false;

            std::size_t FilesCopied = 0;
            std::uint64_t BytesCopied = 0;

            // Impediram o empacotamento. Nao sao avisos.
            std::vector<std::string> Errors;

            std::vector<std::string> Warnings;

            AssetDependencyGraph::Result Graph;
        };

        // Nao apaga nada alem do que `CleanOutput` autoriza, e para no primeiro
        // erro estrutural (sem cena inicial, sem projeto). Um pacote incompleto
        // que PARECE pronto e pior que um erro.
        static Result Package(const Options& options);
    };

} // namespace axe