#pragma once
#include "axe/core/types.hpp"
#include <string>
#include <filesystem>

namespace axe
{
    // ─────────────────────────────────────────────────────────────────────────
    //  ScriptPaths — onde moram os artefatos gerados de um script
    //
    //  ANTES: o .cpp e a .dll de todo script eram escritos em
    //  <exe>/temp_scripts/<Nome>.dll — ou seja, dentro da pasta bin do EDITOR,
    //  não do projeto. Três consequências, todas observadas:
    //
    //    1. Projetos diferentes compartilhavam a mesma pasta. Dois projetos com
    //       um "BP_Player" cada gravavam por cima um do outro; qual .dll ficava
    //       dependia de quem compilou por último.
    //    2. Apagar o script no Asset Browser deixava .cpp/.dll/.exp/.lib para
    //       trás. Nada os limpava, nunca.
    //    3. Fazer backup do projeto não levava nada disso junto, e mandar o
    //       projeto para outra máquina significava recompilar tudo às cegas.
    //
    //  AGORA:
    //    <Projeto>/Intermediate/Scripts/   .cpp gerado    (regenerável)
    //    <Projeto>/Binaries/Scripts/       .dll/.lib/.exp (regenerável)
    //
    //  O nome do arquivo leva o UUID do asset: "BP_Player_a1b2c3d4.dll".
    //  Isso NÃO é enfeite — é o que torna possível responder "de quem é este
    //  arquivo?" olhando só para ele. Com isso, varrer órfãos é comparar UUIDs
    //  contra o AssetDatabase, e colisão entre dois scripts de mesmo nome
    //  deixa de existir por construção. Foi a mesma escolha já feita no resto
    //  da engine: identidade é UUID, nome é exibição.
    //
    //  Este módulo vive na engine, e não no editor, porque quem resolve o
    //  caminho no Play (ScriptWorld) é código de runtime. Duas cópias da regra
    //  divergiriam no primeiro dia.
    // ─────────────────────────────────────────────────────────────────────────
    class AXE_API ScriptPaths
    {
    public:
        // ── SC29: de onde sai a raiz ──────────────────────────────────────────
        //
        // Ate aqui a resposta era so "o projeto aberto no ProjectManager". Num
        // jogo empacotado nao ha projeto aberto: a raiz voltava VAZIA, o
        // ResolveDll nao achava nada e NENHUM script carregava — sem erro de
        // compilacao, sem crash, so um jogo em que nada acontece. O editor
        // nunca passaria por isso, entao o defeito so apareceria no dia do
        // primeiro empacotamento.
        //
        // Agora a raiz tem tres fontes, nesta ordem:
        //
        //   1. SetRuntimeRoot()  — definida explicitamente por quem hospeda o
        //                          jogo. Ganha de tudo: e a unica que expressa
        //                          uma decisao, e nao uma deducao.
        //   2. ProjectManager    — o caso do editor.
        //   3. pasta do .exe     — o layout natural de um build empacotado
        //                          (jogo.exe + Binaries/Scripts ao lado).
        //
        // O passo 3 e o que faz um build funcionar sem ninguem configurar
        // nada; o passo 1 existe para quando o layout for outro.
        static void SetRuntimeRoot(const std::filesystem::path& root);

        // Raiz efetiva, ja resolvida pela ordem acima. Vazia so se o proprio
        // caminho do executavel nao puder ser lido.
        static std::filesystem::path Root();

        // <Raiz>/Intermediate/Scripts.
        //
        // create — cria a pasta se faltar. FALSO por padrao de proposito: um
        // jogo empacotado nao gera codigo, e criar pastas vazias ao lado do
        // executavel do jogador e lixo que ninguem pediu. O editor chama com
        // true quando vai escrever.
        static std::filesystem::path IntermediateDir(bool create = false);

        // <Raiz>/Binaries/Scripts — mesma regra.
        static std::filesystem::path BinariesDir(bool create = false);

        // Nome de arquivo canônico, sem extensão: "<Nome>_<uuid8>".
        // uuid vazio (asset ainda não registrado) devolve só o nome — o
        // fluxo continua funcionando, só perde a garantia de unicidade.
        static std::string ArtifactStem(const std::string& scriptName,
            const std::string& uuid);

        static std::filesystem::path CppPathFor(const std::string& scriptName,
            const std::string& uuid);
        static std::filesystem::path DllPathFor(const std::string& scriptName,
            const std::string& uuid);

        // Onde a DLL deste script está DE FATO, verificando o disco.
        // Ordem de busca:
        //   1. Binaries/Scripts/<Nome>_<uuid8>.dll   (formato atual)
        //   2. Binaries/Scripts/<Nome>.dll           (projeto compilado entre
        //      a mudança de pasta e a de nomenclatura)
        //   3. Binaries/Scripts/<Nome>_*.dll         (UUID desconhecido)
        //   4. <exe>/temp_scripts/<Nome>.dll         (formato legado)
        //
        // O passo 3 e a rede para o RUNTIME: la o AssetDatabase pode nao estar
        // carregado, e sem ele nao ha UUID para montar o nome exato. Procurar
        // pelo padrao resolve — mas so quando o resultado e UNICO. Com dois
        // candidatos ele desiste e avisa, porque carregar a DLL errada e pior
        // do que nao carregar nenhuma: o jogo roda com o script de outro
        // objeto e o sintoma nao aponta para lugar nenhum.
        //
        // O passo 4 existe para que um projeto já compilado continue rodando
        // sem recompilar tudo no primeiro Play. Devolve caminho vazio se não
        // achar nada — o chamador avisa "compile antes do Play", que é a
        // mensagem certa.
        static std::filesystem::path ResolveDll(const std::string& scriptName,
            const std::string& uuid);

        // Apaga .cpp/.dll/.lib/.exp/.pdb/.obj de UM script. Chamado quando o
        // asset é excluído no Asset Browser.
        //
        // IMPORTANTE: a DLL precisa estar DESCARREGADA antes. No Windows um
        // módulo carregado fica travado no disco e o remove() falha em
        // silêncio — por isso a função devolve quantos arquivos conseguiu
        // apagar, para o chamador poder avisar quando o número não bate.
        static int RemoveArtifactsFor(const std::string& scriptName,
            const std::string& uuid);

        // Varre Intermediate/Scripts e Binaries/Scripts e apaga tudo cujo
        // sufixo _<uuid8> não corresponda a nenhum asset vivo no
        // AssetDatabase. Arquivos SEM sufixo de UUID são deixados em paz: são
        // de antes da mudança e não há como saber de quem são.
        //
        // Chamada na abertura do projeto — o momento em que o AssetDatabase
        // acabou de ser carregado e a resposta é confiável.
        static int SweepOrphans();

    private:
        // <exe>/temp_scripts — só para leitura, na migração.
        static std::filesystem::path LegacyDir();
    };

} // namespace axe