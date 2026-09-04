#pragma once

#include "axe/asset/asset.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe_editor/preview/mesh_preview.hpp"   // ASSET_VIEWER_V2b

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

        // ASSET_VIEWER_V2b — desenha a cena do preview no framebuffer.
        //
        // Chamado pelo EditorLayer ANTES do ImGui do frame, na mesma lista dos
        // outros previews. Nao pode virar parte do Draw: durante o ImGui o
        // alvo de render ligado e outro, e a imagem sairia no lugar errado —
        // ou em lugar nenhum.
        void RenderPreview();

        bool IsOpen() const { return m_Open; }
        void Close() { m_Open = false; }

        // Este tipo tem painel? O despacho pergunta antes de rotear, para nao
        // abrir uma janela vazia num tipo que ja tem editor proprio.
        static bool Handles(AssetType type)
        {
            return type == AssetType::Texture || type == AssetType::Mesh;
        }

    private:
        // ASSET_VIEWER_V2c — o par visual / ficha. Ver a nota sobre as duas
        // janelas no Draw.
        void DrawToolbar();          // a barra de icones, comum aos dois tipos
        void DrawTexturePanel();     // area visual: a imagem
        void DrawTextureDetails();   // ficha: dimensoes, canais, caminho
        void DrawMeshViewport();     // area visual: o preview 3D
        void DrawMeshPanel();        // ficha: geometria + importacao
        void DrawVisual();           // despacha a area visual por tipo
        void DrawDetails();          // despacha a ficha por tipo

        // ASSET_VIEWER_V2 — o bloco de configuracao de importacao, comum aos
        // dois tipos. Devolve true se algo mudou e foi gravado.
        bool DrawImportSettings();

        // ASSET_DEFAULTS_V1 — o bloco de material padrao e colisao. Devolve
        // true se algo mudou.
        bool DrawSpawnDefaults();

        // Refaz o wireframe de colisao mostrado no preview a partir das
        // settings atuais. Chamado a cada mudanca — e barato (le os bounds ja
        // calculados) e e o que faz o volume responder ao slider na hora.
        void RefreshColliderPreview();

        // Recarrega o asset do disco aplicando as settings atuais. Invalida o
        // cache CERTO — malha e textura tem caches separados, e invalidar o
        // errado daria "salvei e nao mudou nada".
        void Reimport();

        // Grava m_Import no .axemeta via AssetDatabase. Separado do Reimport
        // porque textura nao precisa reimportar para valer.
        void PersistImportSettings();

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

        // ASSET_VIEWER_V2b — o preview 3D. Componente compartilhado, e nao a
        // quinta copia da montagem de framebuffer + cena + camera que as
        // outras janelas fazem a mao. Ver mesh_preview.hpp.
        ui::MeshPreview m_Preview;
        bool  m_PreviewEnabled = true;

        // ── ASSET_VIEWER_V2d — layout interno ──────────────────────────────
        //
        // A tentativa anterior foi DUAS janelas ImGui dockaveis. Errada: elas
        // docavam em qualquer lugar da engine, e o painel de numeros de um
        // asset acabava do outro lado da tela, longe da imagem a que se refere
        // — e sem nada dizendo que os dois eram a mesma coisa.
        //
        // Uma janela, duas regioes e um divisor arrastavel da o mesmo ajuste
        // sem esse defeito: o Asset Viewer doca como UMA coisa so, e o
        // equilibrio entre imagem e ficha e escolha de quem usa.
        bool  m_ShowDetails = true;
        bool  m_StackedLayout = false;   // false = lado a lado, true = empilhado
        float m_DetailsFraction = 0.34f; // fatia da ficha, 0..1

        // Reimportar recarrega pelo Open, mas a camera do preview tem de ficar
        // parada para o antes/depois ser visivel. Ver a nota no Open.
        bool  m_KeepPreviewCamera = false;

        // Medidas calculadas UMA VEZ no Open. Percorrer os vertices todo frame
        // para desenhar um label seria pagar por nada — malha de personagem
        // tem dezenas de milhares deles.
        bool      m_HaveBounds = false;
        glm::vec3 m_BoundsMin{ 0.0f };
        glm::vec3 m_BoundsMax{ 0.0f };
        std::size_t m_VertexCount = 0;
        std::size_t m_TriangleCount = 0;

        // ── ASSET_VIEWER_V2 — configuracao de importacao ────────────────────
        //
        // COPIA de trabalho, e nao o registro do AssetDatabase. O usuario mexe
        // aqui e so ao confirmar e que vai para o disco: sem isso, arrastar um
        // slider de escala reimportaria a malha a cada pixel de movimento.
        AssetImportSettings m_Import;
        std::string         m_UUID;     // vazio = asset fora do projeto
        bool                m_Dirty = false;

        // ASSET_DEFAULTS_V1 — separado do m_Dirty porque nem toda mudanca
        // custa o mesmo. Escala e pivo reescrevem os VERTICES e exigem
        // reimportar; material padrao e collider so mudam o que a proxima
        // instancia vai receber, e gravar basta. Um botao "Aplicar e
        // reimportar" que reimporta um FBX de 50 MB para trocar um material
        // ensina o usuario a ter medo de mexer.
        bool                m_GeometryDirty = false;

        // Alvo do atalho "Calcular escala" — campo de UI, nao vai para o meta.
        float m_TargetSize = 1.0f;
    };
}
