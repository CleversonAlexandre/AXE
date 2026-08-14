#pragma once
#include "axe/asset/asset_dependency_graph.hpp"

#include <imgui.h>
#include <string>
#include <vector>

namespace axe
{
    // ── Asset Report — B3.2 ──────────────────────────────────────────────────
    //
    // Mostra o resultado do AssetDependencyGraph: o que o jogo usa, o que esta
    // sobrando e o que esta quebrado.
    //
    // POR QUE ANTES DO EMPACOTADOR
    //
    //   Porque a analise sozinha ja responde duas perguntas que hoje nao tem
    //   resposta nenhuma no editor:
    //
    //     "de tudo que importei, o que o jogo realmente usa?"
    //     "tem alguma referencia apontando para um asset que eu apaguei?"
    //
    //   A segunda em especial: uma referencia quebrada hoje e invisivel ate a
    //   cena abrir com um buraco. Esta janela a mostra em uma lista.
    //
    //   E quando o empacotador existir, e por aqui que se confere o que vai
    //   entrar ANTES de gerar o build — em vez de descobrir pelo tamanho do
    //   instalador.
    //
    // NAO EMPACOTA NADA. So le e relata.
    class AssetReportWindow
    {
    public:
        void Open();
        bool IsOpen() const { return m_Open; }
        void Draw();

    private:
        // Roda o Collect. Separado do Draw porque percorrer o grafo abre e
        // parseia cada asset do projeto — barato para algumas centenas, mas nao
        // e coisa para fazer por frame.
        void Run();

        void DrawList(const char* label,
            const std::vector<std::string>& uuids,
            const ImVec4& accent,
            const char* emptyText);

        bool m_Open = false;
        bool m_HasResult = false;

        AssetDependencyGraph::Result m_Result;

        // Ordenados por NOME para exibicao. O Result guarda UUIDs, que nao
        // dizem nada a quem le.
        std::vector<std::string> m_ReachableSorted;

        char m_Filter[64] = {};

        // Incluir as cenas todas como raiz, ou so o GameMode ativo?
        //
        // Padrao: todas as cenas. Um projeto em desenvolvimento costuma ter
        // cenas de teste que ainda nao estao ligadas ao GameMode, e partir so
        // dele mostraria quase tudo como "nao usado" — um relatorio alarmante e
        // errado.
        bool m_RootsAllScenes = true;

        // A cena inicial existe no disco mas nao esta no AssetDatabase.
        // Sintoma de indice desatualizado; invalida o relatorio inteiro.
        bool m_StartSceneUnregistered = false;
    };

} // namespace axe