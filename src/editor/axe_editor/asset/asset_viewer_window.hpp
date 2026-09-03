#pragma once

#include "axe/asset/asset.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/mesh/mesh.hpp"

#include <imgui.h>
#include <filesystem>
#include <memory>
#include <string>

namespace axe
{
    // ═══════════════════════════════════════════════════════════════════════
    //  ASSET_VIEWER_V1 — a janela que faltava para textura e malha
    //
    //  ── POR QUE ELA EXISTE ─────────────────────────────────────────────────
    //
    //  Todo tipo de asset da AXE ja tem onde ser aberto: Material, Particle,
    //  SoundCue, AnimGraph, ControlRig, Anim Clip. Dois nao tinham: TEXTURA e
    //  MALHA. Dar duplo clique numa `.png` ou numa `.fbx` no Asset Browser nao
    //  fazia nada, e a unica forma de saber o que havia dentro de um asset era
    //  aplicar em alguma coisa e olhar.
    //
    //  ── O DESENHO ──────────────────────────────────────────────────────────
    //
    //  UMA janela, um painel por TIPO. Nao uma janela por tipo: o fluxo real e
    //  "estou olhando um asset", e abrir o proximo troca o conteudo, do mesmo
    //  jeito que o Material Editor troca de material. Tipos novos entram como
    //  um `case` a mais, nao como uma janela a mais.
    //
    //  Ela se pluga no MESMO despacho que os outros editores usam
    //  (AssetBrowser::SetAssetOpenCallback, resolvido no EditorLayer), entao
    //  nao ha caminho novo de abertura para divergir do que ja existe.
    //
    //  ── O QUE ESTA V1 FAZ, E O QUE NAO FAZ ────────────────────────────────
    //
    //  FAZ: ver e medir. Textura com zoom, xadrez por baixo (alfa fica visivel
    //  sem truque de shader), isolamento de canal, dimensoes, canais do
    //  arquivo. Malha com contagem de vertices e triangulos, bounds e tamanho
    //  em metros. Mais os ajustes que NAO precisam de reimportacao — filtro e
    //  wrap — que sao estado de amostragem e aplicam na hora.
    //
    //  NAO FAZ: configuracao PERSISTIDA de importacao (escala da malha,
    //  eixo-up, gerar colisao, sRGB por asset). Isso exige o `.axemeta` virar
    //  arquivo de settings e um pipeline de reimportacao, e e a fase 2.
    //  Prometer aqui e entregar meia funcionalidade que reseta ao reabrir.
    // ═══════════════════════════════════════════════════════════════════════
    class AssetViewerWindow
    {
    public:
        // Abre um asset. Tipos sem painel sao ignorados de proposito — quem
        // decide o que abre e o despacho no EditorLayer, e este metodo so
        // aceita o que sabe mostrar.
        void Open(const std::filesystem::path& path, AssetType type,
            const std::string& name);

        void Draw();

        bool IsOpen() const { return m_Open; }
        void Close() { m_Open = false; }

        // Este tipo tem painel? O despacho pergunta antes de rotear, para nao
        // abrir uma janela vazia num tipo que ja tem editor proprio.
        static bool Handles(AssetType type)
        {
            return type == AssetType::Texture || type == AssetType::Mesh;
        }

    private:
        void DrawTexturePanel();
        void DrawMeshPanel();

        // Xadrez atras da imagem. Sem ele, alfa zero num fundo escuro e
        // indistinguivel de preto opaco — e "esta textura tem alfa?" e uma
        // das perguntas que mais se faz olhando uma textura.
        void DrawCheckerboard(const ImVec2& min, const ImVec2& max) const;

        bool        m_Open = false;
        AssetType   m_Type = AssetType::Unknown;
        std::string m_Name;
        std::filesystem::path m_Path;

        // ── Textura ────────────────────────────────────────────────────────
        std::shared_ptr<Texture2D> m_Texture;

        float  m_Zoom = 1.0f;
        ImVec2 m_Pan{ 0.0f, 0.0f };
        bool   m_FitOnOpen = true;

        // Mascara de canal. Feita com o `tint_col` do ImGui::Image, e nao com
        // shader: isolar canal de verdade exigiria um passe de blit, e passe
        // e codigo GL — que nao pode viver no editor. O tint mostra o canal na
        // COR dele em vez de em cinza; e menos bonito e nao mente.
        bool m_ChR = true, m_ChG = true, m_ChB = true;

        // Fundo atras da imagem: 0 xadrez, 1 preto, 2 branco, 3 magenta.
        // Substitui o que seria um "isolar canal A" — ver a nota no .cpp.
        int m_Background = 0;

        // ── Malha ──────────────────────────────────────────────────────────
        std::shared_ptr<Mesh> m_Mesh;

        // Medidas calculadas UMA VEZ no Open. Percorrer os vertices todo frame
        // para desenhar um label seria pagar por nada — malha de personagem
        // tem dezenas de milhares deles.
        bool      m_HaveBounds = false;
        glm::vec3 m_BoundsMin{ 0.0f };
        glm::vec3 m_BoundsMax{ 0.0f };
        std::size_t m_VertexCount = 0;
        std::size_t m_TriangleCount = 0;
    };
}