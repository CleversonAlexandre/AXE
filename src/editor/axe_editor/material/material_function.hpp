#pragma once
#include "axe/core/types.hpp"
#include "editor/axe_editor/node_graph/material_graph.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// =============================================================================
//  MATFUNC_V1 — Material Function
//
//  Um subgrafo de material reutilizavel, guardado num asset proprio
//  (`.axematfunc`). E o equivalente da Material Function da Unreal, e existe
//  pela razao que o proprio Clever apontou: quando uma combinacao de nodes vira
//  util o bastante para ter nome, o lugar dela e um ASSET que o autor abre,
//  edita e versiona — nao um node novo compilado dentro do editor. Node
//  hard-coded fecha a porta que o grafo existe para abrir.
//
//  ── O QUE E, EXATAMENTE ──────────────────────────────────────────────────
//
//  Um `.axematfunc` guarda TRES coisas:
//
//    1. O nome da funcao.
//    2. A ASSINATURA: a lista de entradas e de saidas, com nome e tipo.
//    3. O GRAFO — a mesma classe MaterialGraph do `.axegraph`, sem nenhuma
//       adaptacao. Dentro dele o autor usa os mesmos nodes de sempre; o que
//       muda e o comeco e o fim: em vez de Material Output, ele termina em
//       um ou mais `Function Output`, e comeca em `Function Input`.
//
//  A assinatura e DERIVADA do grafo toda vez que a funcao e salva — os nodes
//  Function Input/Output sao a fonte da verdade enquanto se edita, e o
//  cabecalho e so um cache. Isso evita a pior classe de bug possivel aqui,
//  que e a assinatura declarada num lugar e o grafo dizendo outra coisa.
//  O cache existe para que um node de chamada consiga montar seus pinos sem
//  precisar carregar e desserializar o grafo inteiro da funcao.
//
//  ── O RUNTIME NAO CONHECE ISTO ───────────────────────────────────────────
//
//  Funcao NAO vira shader. Ela e INLINADA no material que a chama, no momento
//  da compilacao (ver MaterialCompiler). O `.axeshader` cozido que sai e
//  identico ao de um material em que os mesmos nodes tivessem sido desenhados
//  a mao no grafo principal.
//
//  Consequencia pratica, e ela e o ponto todo do desenho: `axe.dll`, o
//  renderer e o `game.exe` nao mudam em nada e nao precisam saber que Material
//  Function existe. Toda esta funcionalidade vive no editor, do lado da
//  ferramenta de autoria — que e a regra de ouro do PACKAGING_READINESS
//  ("Ferramenta de autoria cozinha em disco; o runtime so le").
// =============================================================================

namespace axe
{
    // MaterialFunctionParam, PinTypeToString e PinTypeFromString moram em
    // node_types.hpp: o MaterialGraph precisa deles para montar os pinos do
    // node de chamada, e este header ja inclui material_graph.hpp.

    class MaterialFunction
    {
    public:
        MaterialFunction();

        static std::shared_ptr<MaterialFunction> Create(const std::string& name = "NewMaterialFunction");
        // idSeed empurra o contador de ID do grafo ANTES de desserializar.
        // Zero para uso normal (abrir no editor); o MaterialCompiler passa uma
        // faixa propria por chamada inlinada — ver MaterialGraph::SeedNextID.
        static std::shared_ptr<MaterialFunction> LoadFromFile(
            const std::filesystem::path& filepath, int idSeed = 0);

        // O GRAFO VEM DE FORA, e isso nao e detalhe.
        //
        // Quem edita e o MaterialEditorWindow, e la o grafo e do m_Graph da
        // janela — o mesmo membro que segura o grafo de um material comum, e
        // por isso o canvas, o undo e o painel de detalhes funcionam sem saber
        // que estao editando uma funcao. Se este objeto tambem quisesse ser
        // dono, seriam dois donos do mesmo grafo ou uma copia que sai de
        // sincronia com o que esta na tela.
        //
        // O caminho de LEITURA e o contrario: Load() monta e possui o proprio
        // grafo, porque quem chama e o compilador, que so quer ler.
        bool Save(const std::filesystem::path& filepath, MaterialGraph& graph);
        bool Load(const std::filesystem::path& filepath, int idSeed = 0);

        // Le SO o cabecalho: nome, descricao e assinatura, sem montar o grafo.
        //
        // Existe para o node de chamada. Um material com dez chamadas de
        // funcao nao pode desserializar dez grafos completos so para saber
        // quantos pinos desenhar — e o node de chamada e redesenhado a cada
        // frame do editor.
        static bool ReadSignature(const std::filesystem::path& filepath,
            std::string& outName,
            std::vector<MaterialFunctionParam>& outInputs,
            std::vector<MaterialFunctionParam>& outOutputs);

        // Varre o grafo e reconstroi Inputs/Outputs a partir dos nodes
        // Function Input / Function Output. Chamado no Save.
        //
        // A ORDEM dos pinos e a ordem VERTICAL dos nodes no canvas (pos_y, e
        // pos_x como desempate). Sem uma regra assim a ordem seria a de
        // criacao dos nodes, que ninguem enxerga: arrastar um Function Input
        // para cima nao mudaria nada, e a lista no node de chamada apareceria
        // fora de ordem sem explicacao.
        void RebuildSignatureFromGraph(MaterialGraph& graph);

        const std::string& GetName() const { return m_Name; }
        void SetName(const std::string& n) { m_Name = n; }

        const std::string& GetDescription() const { return m_Description; }
        void SetDescription(const std::string& d) { m_Description = d; }

        MaterialGraph* GetGraph() { return m_Graph.get(); }

        // Entrega a posse do grafo para quem vai edita-lo. Depois disto
        // GetGraph() devolve nulo — e de proposito: um Save() que caisse de
        // volta no grafo interno gravaria a versao de antes das edicoes.
        std::unique_ptr<MaterialGraph> TakeGraph() { return std::move(m_Graph); }

        const std::vector<MaterialFunctionParam>& GetInputs()  const { return m_Inputs; }
        const std::vector<MaterialFunctionParam>& GetOutputs() const { return m_Outputs; }

        const std::filesystem::path& GetFilePath() const { return m_FilePath; }

    private:
        std::string                        m_Name = "NewMaterialFunction";
        std::string                        m_Description;
        std::vector<MaterialFunctionParam> m_Inputs;
        std::vector<MaterialFunctionParam> m_Outputs;
        std::unique_ptr<MaterialGraph>     m_Graph;
        std::filesystem::path              m_FilePath;
    };

} // namespace axe