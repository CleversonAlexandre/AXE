#pragma once
#include "axe/core/types.hpp"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace axe
{
    // ── Grafo de dependencias de assets — B3.1 ───────────────────────────────
    //
    // Responde a pergunta do empacotamento: DE TUDO QUE ESTA EM Assets/, O QUE
    // O JOGO REALMENTE PRECISA?
    //
    // Sem isto, as duas saidas eram ruins: copiar a pasta inteira (e mandar
    // `dancing.fbx` de teste para o build final), ou confiar numa convencao de
    // pasta — que depende de o usuario lembrar, e uma importacao distraida
    // quebra em silencio.
    //
    // ── COMO AS REFERENCIAS SAO DESCOBERTAS ──────────────────────────────────
    //
    // Por VARREDURA GENERICA, e nao por coletores escritos a mao para cada tipo.
    //
    // Essa foi a decisao central. A alternativa obvia seria um
    // `CollectReferences(const MaterialAsset&)`, outro para cena, outro para
    // particula, e assim por diante. Levantei os campos: sao 26 nomes
    // diferentes de UUID espalhados pelo runtime — `AlbedoUUID`, `RigUUID`,
    // `SubEmitterUUID`, `DefaultPawnScriptUUID`, `GraphAssetUUID`... Escrever um
    // coletor por tipo significa que ESQUECER UM e questao de tempo, e o
    // sintoma seria um asset faltando no jogo empacotado: modelo invisivel, som
    // mudo, e nada no log do editor, porque no editor esta tudo la.
    //
    // Todos os assets do AXE sao JSON. Entao a varredura percorre o JSON
    // INTEIRO, recursivamente, e coleta toda string que:
    //
    //   1. tem a cara de um UUID (8-4-4-4-12 hex), E
    //   2. existe no AssetDatabase.
    //
    // Cobre os 26 campos sem conhecer nenhum deles pelo nome — e cobre tambem o
    // campo que alguem adicionar amanha, sem tocar neste arquivo.
    //
    // ── O ERRO CAI PARA O LADO SEGURO ────────────────────────────────────────
    //
    // Um falso positivo (uma string que por acaso casa com o UUID de outro
    // asset) inclui um arquivo a mais no build. Um falso negativo deixa o jogo
    // sem um asset. Os dois nao tem o mesmo peso, e a varredura generica erra
    // na direcao barata.
    //
    // ── O QUE A VARREDURA DE UUID NAO PEGA ───────────────────────────────────
    //
    // Referencia por CAMINHO. O `.axeskel` aponta para o `.fbx` de origem por
    // caminho relativo, nao por UUID; o mesmo vale para os arquivos de animacao
    // e para o HDRI do ambiente. Por isso a varredura tambem trata strings que
    // resolvem para um arquivo existente dentro da raiz de assets.
    //
    // E fica um furo conhecido, que nenhuma analise estatica fecha: um script
    // que resolve um asset por nome em tempo de execucao. Para esses existe a
    // lista de inclusao forcada — ver `Collect`.

    class AXE_API AssetDependencyGraph
    {
    public:
        struct Result
        {
            // Alcancaveis a partir das raizes, incluindo as proprias raizes.
            std::set<std::string> ReachableUUIDs;

            // Arquivos a copiar: o asset, o `.axemeta` e os derivados cozidos
            // (`.axemesh`, `.axeskelbin`, `.axeclipbin`) de cada alcancavel.
            std::vector<std::filesystem::path> Files;

            // Registrados no AssetDatabase e NAO alcancados. E o relatorio que
            // o usuario le antes de empacotar: "o `dancing.fbx` vai ficar de
            // fora — era isso mesmo?"
            std::vector<std::string> UnreachableUUIDs;

            // Referenciados por alguem e ausentes do AssetDatabase. Isto e
            // ERRO, nao aviso: significa um asset apagado com a referencia
            // ainda de pe, e no jogo apareceria como buraco.
            std::vector<std::string> MissingUUIDs;

            // ── Diagnostico por raiz ─────────────────────────────────────
            //
            // Quantas referencias DIRETAS cada raiz produziu.
            //
            // Existe porque a primeira coisa que deu errado neste sistema foi
            // exatamente o que este campo detecta: uma cena entrou como raiz,
            // apareceu em "usados", e mesmo assim tudo que ela referencia foi
            // listado como nao usado — porque o ARQUIVO no disco estava
            // desatualizado em relacao a cena aberta no editor.
            //
            // Sem este numero, o sintoma e mudo: o relatorio parece
            // funcionar, so mente. Uma raiz com zero referencias e quase
            // sempre um arquivo vazio, nao salvo, ou que o parser recusou.
            struct RootInfo
            {
                std::string UUID;
                std::string Name;
                std::size_t DirectRefs = 0;
                bool        Parsed = false;   // o arquivo abriu e era JSON valido
            };

            std::vector<RootInfo> Roots;
        };

        // `roots` — de onde partir. Tipicamente o GameMode ativo e as cenas do
        // jogo. Recebidos por parametro, e nao lidos do ProjectManager, para
        // que este modulo nao dependa do conceito de projeto: quem empacota
        // decide o que e raiz.
        //
        // `forcedUUIDs` — inclusao incondicional, para o que a analise estatica
        // nao alcanca (asset carregado por nome dentro de um script). E a valvula
        // de escape equivalente ao "Additional Asset Directories to Cook" da
        // Unreal, e existe porque a alternativa — fingir que o caso nao existe —
        // produz um jogo que quebra so numa cena especifica.
        static Result Collect(const std::vector<std::string>& roots,
            const std::vector<std::string>& forcedUUIDs = {});

        // Referencias DIRETAS de um asset, sem transitividade. Exposto porque
        // uma janela de editor pode querer mostrar "quem este asset usa" sem
        // percorrer o grafo inteiro.
        // `outParsed` (opcional) informa se o arquivo abriu e era JSON valido.
        // Distingue "nao referencia nada" de "nao consegui ler" — que na tela
        // parecem a mesma coisa e tem causas opostas.
        //
        // `outCookedFiles` (opcional) recebe caminhos referenciados que
        // EXISTEM em disco, tem extensao de arquivo cozido e NAO estao no
        // AssetDatabase.
        //
        // ── POR QUE ISTO PRECISOU EXISTIR ────────────────────────────────
        //
        // Um cozido normalmente e IRMAO de um asset registrado
        // (`idle.fbx` -> `idle.axeclipbin`), e o empacotador o encontra
        // trocando a extensao do record. Isso pressupoe que todo cozido
        // tenha um fonte registrado ao lado.
        //
        // O bake do Sequencer quebra a premissa: ele produz um
        // `.axeclipbin` que NAO veio de FBX nenhum — o `.axeskel` aponta
        // direto para ele. Sem esta lista, a animacao assada existiria no
        // editor e sumiria do build, que e o pior lugar para descobrir uma
        // falta.
        static std::set<std::string> DirectReferences(const std::string& uuid,
            bool* outParsed = nullptr,
            std::vector<std::filesystem::path>* outCookedFiles = nullptr);

        // true se a string tem forma de UUID (8-4-4-4-12 hex).
        static bool LooksLikeUUID(const std::string& s);
    };

} // namespace axe