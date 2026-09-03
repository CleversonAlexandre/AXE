#include "asset_viewer_window.hpp"

#include "axe_editor/import/mesh_loader.hpp"
#include "axe/log/log.hpp"

#include <algorithm>
#include <cstdio>

namespace axe
{
    namespace
    {
        constexpr float kMinZoom = 0.05f;
        constexpr float kMaxZoom = 32.0f;
        constexpr float kCheckerSize = 8.0f;

        const char* ChannelsLabel(std::uint32_t c)
        {
            switch (c)
            {
            case 1:  return "R (cinza)";
            case 2:  return "RG";
            case 3:  return "RGB";
            case 4:  return "RGBA";
            default: return "desconhecido";
            }
        }
    }

    void AssetViewerWindow::Open(const std::filesystem::path& path, AssetType type,
        const std::string& name)
    {
        if (!Handles(type)) return;

        m_Path = path;
        m_Type = type;
        m_Name = name;
        m_Open = true;

        m_Texture.reset();
        m_Mesh.reset();
        m_HaveBounds = false;
        m_VertexCount = 0;
        m_TriangleCount = 0;

        m_Zoom = 1.0f;
        m_Pan = ImVec2(0.0f, 0.0f);
        m_FitOnOpen = true;
        m_ChR = m_ChG = m_ChB = true;
        m_Background = 0;

        if (type == AssetType::Texture)
        {
            m_Texture = Texture2D::Create(path.string());
            return;
        }

        // ── Malha ──────────────────────────────────────────────────────────
        //
        // As medidas sao tiradas AQUI, uma vez. Percorrer os vertices todo
        // frame so para desenhar tres labels seria pagar caro por nada: uma
        // malha de personagem tem dezenas de milhares deles.
        // O MeshLoader do editor devolve malha + material e ja cacheia por
        // caminho — abrir o mesmo asset duas vezes nao reimporta.
        m_Mesh = MeshLoader::Load(path.string(), /*quiet=*/true).MeshData;
        if (!m_Mesh) return;

        const auto& verts = m_Mesh->GetVertices();
        m_VertexCount = verts.size();
        m_TriangleCount = m_Mesh->GetIndices().size() / 3;

        if (!verts.empty())
        {
            m_BoundsMin = m_BoundsMax = verts[0].Position;
            for (const auto& v : verts)
            {
                m_BoundsMin = glm::min(m_BoundsMin, v.Position);
                m_BoundsMax = glm::max(m_BoundsMax, v.Position);
            }
            m_HaveBounds = true;
        }
    }

    void AssetViewerWindow::DrawCheckerboard(const ImVec2& min, const ImVec2& max) const
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(min, max, true);

        // Fundo liso: pinta e sai. So o modo xadrez desenha os quadrados.
        if (m_Background != 0)
        {
            const ImU32 solid =
                m_Background == 1 ? IM_COL32(0, 0, 0, 255) :
                m_Background == 2 ? IM_COL32(255, 255, 255, 255) :
                IM_COL32(255, 0, 255, 255);
            dl->AddRectFilled(min, max, solid);
            dl->PopClipRect();
            return;
        }

        const ImU32 a = IM_COL32(58, 58, 62, 255);
        const ImU32 b = IM_COL32(78, 78, 84, 255);

        dl->AddRectFilled(min, max, a);

        // Comeca no multiplo de kCheckerSize ANTES do canto, senao o padrao
        // "anda" junto com a imagem ao arrastar, e o xadrez deixa de parecer
        // fundo para parecer parte do conteudo.
        const float x0 = std::floor(min.x / kCheckerSize) * kCheckerSize;
        const float y0 = std::floor(min.y / kCheckerSize) * kCheckerSize;

        for (float y = y0, row = 0; y < max.y; y += kCheckerSize, ++row)
            for (float x = x0 + (int(row) % 2 ? kCheckerSize : 0.0f);
                x < max.x; x += 2.0f * kCheckerSize)
        {
            dl->AddRectFilled(ImVec2(x, y),
                ImVec2(std::min(x + kCheckerSize, max.x),
                    std::min(y + kCheckerSize, max.y)), b);
        }

        dl->PopClipRect();
    }

    void AssetViewerWindow::DrawTexturePanel()
    {
        if (!m_Texture || !m_Texture->IsLoaded())
        {
            ImGui::TextDisabled("Nao foi possivel carregar a textura.");
            ImGui::TextDisabled("%s", m_Path.string().c_str());
            return;
        }

        const float texW = (float)m_Texture->GetWidth();
        const float texH = (float)m_Texture->GetHeight();
        const std::uint32_t channels = m_Texture->GetChannels();

        // ── Barra de ferramentas ───────────────────────────────────────────
        if (ImGui::Button("Ajustar")) { m_FitOnOpen = true; m_Pan = ImVec2(0, 0); }
        ImGui::SameLine();
        if (ImGui::Button("1:1")) { m_Zoom = 1.0f; m_Pan = ImVec2(0, 0); m_FitOnOpen = false; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::SliderFloat("##zoom", &m_Zoom, kMinZoom, kMaxZoom, "%.2fx",
            ImGuiSliderFlags_Logarithmic))
            m_FitOnOpen = false;

        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();

        ImGui::Checkbox("R", &m_ChR); ImGui::SameLine();
        ImGui::Checkbox("G", &m_ChG); ImGui::SameLine();
        ImGui::Checkbox("B", &m_ChB); ImGui::SameLine();

        // ── FUNDO, e nao "isolar o canal A" ───────────────────────────────
        //
        // A primeira versao tinha um checkbox "A". Ele nao funcionaria: o alfa
        // da imagem e composto pelo driver, e o tint do draw list MULTIPLICA —
        // nao da para forcar opaco por ali. Isolar canal de verdade exige um
        // passe de blit com shader, e passe e codigo GL, que nao pode viver no
        // editor.
        //
        // Trocar o FUNDO responde a mesma pergunta e funciona de verdade: e
        // como todo visualizador de textura serio faz. Xadrez mostra que ha
        // alfa; branco e preto mostram como a arte se comporta sobre claro e
        // sobre escuro; magenta denuncia franja de canal RGB sujo nas bordas
        // transparentes — o defeito classico de PNG exportado sem premultiply.
        ImGui::SetNextItemWidth(120.0f);
        ImGui::Combo("##fundo", &m_Background, "Xadrez\0Preto\0Branco\0Magenta\0");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Fundo atras da imagem.\n"
                "Magenta denuncia franja nas bordas transparentes.");

        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();

        // ── Amostragem: aplica NA HORA, sem reimportar ─────────────────────
        {
            int filter = (int)m_Texture->GetFilter();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::Combo("##filtro", &filter, "Nearest\0Linear\0Trilinear\0"))
                m_Texture->SetFilter((Texture2D::Filter)filter);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Filtro de amostragem. Nearest da pixel duro\n"
                    "(arte pixelada); Trilinear usa os mips.\n"
                    "Aplica na hora — nao e reimportacao.");

            ImGui::SameLine();
            int wrap = (int)m_Texture->GetWrap();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::Combo("##wrap", &wrap, "Repeat\0Clamp\0Mirror\0"))
                m_Texture->SetWrap((Texture2D::Wrap)wrap);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Como a textura se repete fora de [0,1].\n"
                    "Clamp em textura de tiling causa costura;\n"
                    "Repeat em sprite causa halo na borda.");
        }

        ImGui::Separator();

        // ── Area da imagem ─────────────────────────────────────────────────
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float infoH = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        const ImVec2 canvas(avail.x, std::max(avail.y - infoH, 32.0f));

        const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
        const ImVec2 canvasMax(canvasMin.x + canvas.x, canvasMin.y + canvas.y);

        DrawCheckerboard(canvasMin, canvasMax);

        // "Ajustar" e resolvido AQUI, e nao no Open, porque so agora se sabe o
        // tamanho do painel — no Open a janela pode nem ter sido desenhada.
        if (m_FitOnOpen && texW > 0.0f && texH > 0.0f)
        {
            m_Zoom = std::min(canvas.x / texW, canvas.y / texH);
            m_Zoom = std::clamp(m_Zoom, kMinZoom, kMaxZoom);
        }

        const ImVec2 drawn(texW * m_Zoom, texH * m_Zoom);
        const ImVec2 center((canvasMin.x + canvasMax.x) * 0.5f,
            (canvasMin.y + canvasMax.y) * 0.5f);
        const ImVec2 imgMin(center.x - drawn.x * 0.5f + m_Pan.x,
            center.y - drawn.y * 0.5f + m_Pan.y);

        // Desenhada pelo DRAW LIST, e nao por ImGui::Image, por dois motivos:
        //
        //  1. o overload de Image com tint mudou de assinatura entre versoes do
        //     ImGui; AddImage com cor e estavel em todas;
        //  2. aqui a imagem nao e um "item" da UI — quem recebe hover, roda e
        //     arrasto e o InvisibleButton do canvas inteiro, logo abaixo. Duas
        //     coisas disputando o mesmo mouse dariam zoom so em cima da
        //     imagem e nao no resto do painel.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(canvasMin, canvasMax, true);

        const ImU32 tint = IM_COL32(m_ChR ? 255 : 0,
            m_ChG ? 255 : 0,
            m_ChB ? 255 : 0, 255);

        dl->AddImage((ImTextureID)(uintptr_t)m_Texture->GetRendererID(),
            imgMin, ImVec2(imgMin.x + drawn.x, imgMin.y + drawn.y),
            ImVec2(0, 0), ImVec2(1, 1), tint);

        dl->PopClipRect();

        // Consome a area toda para o hover e a roda funcionarem em qualquer
        // ponto do canvas, e nao so em cima da imagem.
        ImGui::SetCursorScreenPos(canvasMin);
        ImGui::InvisibleButton("##canvas", canvas,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

        if (ImGui::IsItemHovered())
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                // Zoom MULTIPLICATIVO: um passo de roda muda a mesma proporcao
                // em qualquer zoom. Somar daria passos gigantes perto de 0.05x
                // e imperceptiveis em 32x.
                m_Zoom = std::clamp(m_Zoom * std::pow(1.15f, wheel), kMinZoom, kMaxZoom);
                m_FitOnOpen = false;
            }

            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            {
                const ImVec2 d = ImGui::GetIO().MouseDelta;
                m_Pan.x += d.x;
                m_Pan.y += d.y;
                m_FitOnOpen = false;
            }
        }

        // ── Ficha tecnica ──────────────────────────────────────────────────
        ImGui::Text("%.0f x %.0f  |  %s", texW, texH, ChannelsLabel(channels));
        ImGui::SameLine();

        // Potencia de dois importa: mipmap e wrap de repeticao so se comportam
        // bem assim em hardware antigo, e continua sendo o costume da area.
        auto isPow2 = [](float v) {
            const auto n = (std::uint32_t)v;
            return n && ((n & (n - 1)) == 0);
            };
        if (!isPow2(texW) || !isPow2(texH))
            ImGui::TextDisabled("|  nao e potencia de dois");
        else
            ImGui::TextDisabled("|  potencia de dois");

        ImGui::TextDisabled("%s", m_Path.string().c_str());
    }

    void AssetViewerWindow::DrawMeshPanel()
    {
        if (!m_Mesh)
        {
            ImGui::TextDisabled("Nao foi possivel carregar a malha.");
            ImGui::TextDisabled("%s", m_Path.string().c_str());
            return;
        }

        ImGui::SeparatorText("Geometria");
        ImGui::Text("Vertices:   %zu", m_VertexCount);
        ImGui::Text("Triangulos: %zu", m_TriangleCount);

        if (m_HaveBounds)
        {
            const glm::vec3 size = m_BoundsMax - m_BoundsMin;
            const glm::vec3 center = (m_BoundsMax + m_BoundsMin) * 0.5f;

            ImGui::Spacing();
            ImGui::SeparatorText("Bounds (espaco da malha)");
            ImGui::Text("Min:    %.3f  %.3f  %.3f", m_BoundsMin.x, m_BoundsMin.y, m_BoundsMin.z);
            ImGui::Text("Max:    %.3f  %.3f  %.3f", m_BoundsMax.x, m_BoundsMax.y, m_BoundsMax.z);
            ImGui::Text("Tamanho:%.3f  %.3f  %.3f", size.x, size.y, size.z);
            ImGui::Text("Centro: %.3f  %.3f  %.3f", center.x, center.y, center.z);

            ImGui::Spacing();

            // ═══════════════════════════════════════════════════════════════
            //  A leitura que evita uma tarde perdida
            //
            //  Malha de DCC vem em unidades da ferramenta, nao em metros, e o
            //  sintoma nunca e "esta grande": e sombra errada, particula no
            //  lugar errado, colisao que nao encaixa.
            //
            //  ── POR QUE A MAIOR DIMENSAO, E NAO A ALTURA ──────────────────
            //
            //  A primeira versao olhava so o Y. Serve para personagem e MENTE
            //  para prop: uma pistola de 1,03 m de comprimento tem 0,19 m de
            //  altura, e o painel dizia "compativel com metros" — tecnicamente
            //  verdade, e inutil. O que define a escala de um objeto e a maior
            //  dimensao dele, seja qual eixo for.
            // ═══════════════════════════════════════════════════════════════
            const float biggest = std::max({ size.x, size.y, size.z });
            const char* axis = (biggest == size.x) ? "X" : (biggest == size.y ? "Y" : "Z");

            if (biggest > 10.0f)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.35f, 1.0f),
                    "Maior dimensao: %.1f unidades (em %s).", biggest, axis);
                ImGui::TextDisabled("  Quase certamente nao esta em metros — malha de");
                ImGui::TextDisabled("  DCC costuma vir em cm. Dividir por 100 daria %.2f m.", biggest * 0.01f);
                ImGui::TextDisabled("  Para um personagem de 1,8 m, a escala seria ~%.4f.", 1.8f / biggest);
            }
            else if (biggest > 0.0001f)
            {
                // Sem alarme: so o numero, para ele conferir contra o objeto
                // real. Um numero plausivel para uma malha e absurdo para
                // outra — quem sabe qual e o objeto e ele, nao a engine.
                ImGui::TextDisabled("Maior dimensao: %.2f m (em %s). Confere com o objeto real?",
                    biggest, axis);
            }

            // ── Pivo fora do objeto ───────────────────────────────────────
            //
            // Malha deslocada da origem estraga rotacao e encaixe, e e
            // invisivel ate alguem tentar girar o objeto ou prender num
            // socket — ai vira uma compensacao a mao no Transform, que fica no
            // projeto para sempre.
            //
            // Comparado com a MAIOR dimensao, e nao com a altura: um prop
            // deitado tem altura minuscula, e qualquer deslocamento passaria
            // no teste antigo sem aviso nenhum.
            const float off = glm::length(glm::vec2(center.x, center.z));
            if (off > biggest * 0.2f && off > 0.01f)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.35f, 1.0f),
                    "Pivo fora do objeto: centro a %.2f da origem em XZ.", off);
                ImGui::TextDisabled("  Girar vai parecer torto e encaixe em socket vai");
                ImGui::TextDisabled("  exigir compensacao a mao no Transform. O conserto");
                ImGui::TextDisabled("  e no DCC: centralizar a malha na origem e reexportar.");
            }
        }
        else
        {
            // Caminho do .axemesh cozido: o loader devolve a malha ja na GPU e
            // GetVertices() vem vazio de proposito (ver mesh.hpp). Nao e erro.
            ImGui::Spacing();
            ImGui::TextDisabled("Sem dados de vertice em CPU para medir.");
            ImGui::TextDisabled("Malha cozida carrega direto para a GPU.");
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Arquivo");
        ImGui::TextDisabled("%s", m_Path.string().c_str());

        ImGui::Spacing();
        ImGui::SeparatorText("Ainda nao");
        ImGui::TextDisabled("Preview 3D, slots de material e colisao por asset");
        ImGui::TextDisabled("entram na fase 2 — eles precisam do .axemeta virar");
        ImGui::TextDisabled("arquivo de configuracao e de um passo de reimportacao.");
    }

    void AssetViewerWindow::Draw()
    {
        if (!m_Open) return;

        // Titulo com o nome do asset e ID fixo: sem o `###`, o ImGui trataria
        // cada asset como uma JANELA diferente e o layout dockado se perderia
        // a cada abertura. Mesmo padrao dos outros editores.
        char title[256];
        std::snprintf(title, sizeof(title), "Asset Viewer — %s###AssetViewer",
            m_Name.empty() ? "(sem asset)" : m_Name.c_str());

        if (!ImGui::Begin(title, &m_Open))
        {
            ImGui::End();
            return;
        }

        switch (m_Type)
        {
        case AssetType::Texture: DrawTexturePanel(); break;
        case AssetType::Mesh:    DrawMeshPanel();    break;
        default:
            ImGui::TextDisabled("Este tipo de asset ainda nao tem painel aqui.");
            break;
        }

        ImGui::End();
    }
}