#include "asset_viewer_window.hpp"

#include "axe_editor/import/mesh_loader.hpp"
#include "axe_editor/ui/editor_widgets.hpp"   // ASSET_VIEWER_V2d
#include "axe_editor/ui/editor_icons.hpp"
#include "asset_picker.hpp"                      // ASSET_DEFAULTS_V1
#include "asset_spawn_defaults.hpp"
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

        // ── ASSET_VIEWER_V2 — traz o que o .axemeta guardou ────────────────
        //
        // Antes de carregar, e nao depois: o loader ja aplica as settings, e
        // ler aqui garante que o que o painel MOSTRA e o que foi APLICADO.
        // Sem registro (asset fora do projeto, ou base ainda nao varrida), o
        // UUID fica vazio, os campos ficam no padrao e a UI de configuracao
        // se desabilita sozinha — nao ha como salvar num asset sem meta.
        m_UUID.clear();
        m_Import = AssetImportSettings{};
        m_Dirty = false;
        if (const AssetRecord* rec = AssetDatabase::Get().GetByPath(path))
        {
            m_UUID = rec->UUID;
            m_Import = rec->Import;
        }

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

            // ── TARGET_SIZE_V2 — o alvo nasce do tamanho ATUAL ──────────────
            //
            // O default era 1.000 fixo, e isso era uma armadilha: quem clicasse
            // "Calcular" sem reparar no campo redimensionava o objeto para UM
            // METRO. Foi o que aconteceu com a pistola — ela virou uma arma de
            // 1 m de comprimento, e o socket do personagem, que so multiplica,
            // mostrou fielmente uma pistola gigante.
            //
            // Partindo do tamanho atual, clicar sem mexer no campo nao faz
            // nada. Para mudar o tamanho e preciso DIZER o tamanho — que e o
            // unico jeito de o campo nao decidir por conta propria.
            const glm::vec3 sz0 = m_BoundsMax - m_BoundsMin;
            const float biggest0 = std::max({ sz0.x, sz0.y, sz0.z });
            if (biggest0 > 0.0001f) m_TargetSize = biggest0;
        }

        // ── ASSET_VIEWER_V2b — alimenta o preview 3D ───────────────────────
        //
        // Depois dos bounds, e nao antes: e o FrameBounds que decide a
        // distancia da camera, e ele precisa das medidas ja calculadas. Uma
        // pistola de 19 cm e um predio de 40 m entram pela mesma linha.
        //
        // O material vem do proprio arquivo quando existe. Nao ha slot de
        // material configuravel ainda — isso e o proximo passo — mas mostrar o
        // material que o FBX trouxe ja e melhor que cinza chapado.
        if (m_PreviewEnabled)
        {
            // ── ASSET_DEFAULTS_V1b — o preview mostra o material ESCOLHIDO ─
            //
            // Antes ele usava sempre o material que veio do FBX, e o slot
            // "Material padrao" nao mudava nada na tela: escolher o material
            // da pistola e continuar vendo a pistola branca parece — com toda
            // a razao — que a configuracao nao funciona.
            //
            // A mesma precedencia do AssetSpawnDefaults::Apply: material
            // configurado vence o do arquivo. Se o preview usasse outra regra,
            // ele mostraria uma coisa e a cena criaria outra.
            std::shared_ptr<Material> previewMat;
            if (!m_Import.DefaultMaterialUUID.empty())
                previewMat = AssetSpawnDefaults::ResolveMaterial(m_Import.DefaultMaterialUUID);
            if (!previewMat)
                previewMat = MeshLoader::Load(path.string(), true).MaterialData;

            m_Preview.SetMesh(m_Mesh, previewMat);

            // Reimportacao NAO reenquadra, de proposito. Se a camera se
            // ajustasse ao tamanho novo, uma malha 5x menor ocuparia o mesmo
            // espaco na tela e a mudanca ficaria invisivel — exatamente a
            // mudanca que o usuario acabou de pedir para conferir. Com a
            // camera parada e a grade de 1 m no lugar, o objeto encolhe
            // diante dos olhos.
            if (m_HaveBounds && !m_KeepPreviewCamera)
                m_Preview.FrameBounds(m_BoundsMin, m_BoundsMax);
            m_KeepPreviewCamera = false;

            // ASSET_DEFAULTS_V1 — o volume de colisao gravado no meta aparece
            // junto com a malha, sem precisar abrir a secao.
            RefreshColliderPreview();
        }
    }

    void AssetViewerWindow::RenderPreview()
    {
        // So gasta um render se ha realmente um preview visivel. Janela
        // fechada, textura aberta ou preview desligado nao pagam nada.
        if (!m_Open || m_Type != AssetType::Mesh || !m_PreviewEnabled) return;
        if (!m_Mesh) return;

        m_Preview.Render();
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

        // Zoom. Fica AQUI, e nao na barra de cima: e o unico controle que se
        // usa com o olho na imagem, mexendo e olhando ao mesmo tempo.
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::SliderFloat("##zoom", &m_Zoom, kMinZoom, kMaxZoom, "%.2fx",
            ImGuiSliderFlags_Logarithmic))
            m_FitOnOpen = false;

        ImGui::SameLine();
        ImGui::TextDisabled("%.0f x %.0f", texW, texH);

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

    }

    // ASSET_VIEWER_V2c — a ficha vive na janela de DETALHES, nao por baixo da
    // imagem. Encostada na imagem ela roubava altura justamente de quem estava
    // com o zoom aberto olhando um canto da textura.
    void AssetViewerWindow::DrawTextureDetails()
    {
        if (!m_Texture) { ImGui::TextDisabled("Sem textura carregada."); return; }

        const float texW = (float)m_Texture->GetWidth();
        const float texH = (float)m_Texture->GetHeight();
        const int channels = m_Texture->GetChannels();

        ui::SectionHeader(ICON_IMAGE, "Visualizacao", ui::Accent::Neutral);
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

        ImGui::Spacing();
        ui::SectionHeader(ICON_SLIDERS, "Amostragem (.axemeta)", ui::Accent::Primary);
        // ── Amostragem: aplica NA HORA, sem reimportar ─────────────────────
        //
        // ASSET_VIEWER_V2 — na V1 estes dois combos mexiam so no objeto vivo e
        // o ajuste morria ao fechar a engine. Agora eles ESCREVEM no .axemeta
        // na hora, sem passo de reimportacao: filtro e wrap sao estado de
        // amostragem, e nao geometria de textura — mudar nao exige redecodar
        // nada. Botao "Salvar" aqui so daria ao usuario uma chance de perder o
        // ajuste sem motivo.
        {
            int filter = m_Texture ? (int)m_Texture->GetFilter() : m_Import.TextureFilter;
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::Combo("##filtro", &filter, "Nearest\0Linear\0Trilinear\0"))
            {
                if (m_Texture) m_Texture->SetFilter((Texture2D::Filter)filter);
                m_Import.TextureFilter = filter;
                PersistImportSettings();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Filtro de amostragem. Nearest da pixel duro\n"
                    "(arte pixelada); Trilinear usa os mips.\n"
                    "Aplica na hora e fica gravado no .axemeta.");

            ImGui::SameLine();
            int wrap = m_Texture ? (int)m_Texture->GetWrap() : m_Import.TextureWrap;
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::Combo("##wrap", &wrap, "Repeat\0Clamp\0Mirror\0"))
            {
                if (m_Texture) m_Texture->SetWrap((Texture2D::Wrap)wrap);
                m_Import.TextureWrap = wrap;
                PersistImportSettings();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Como a textura se repete fora de [0,1].\n"
                    "Clamp em textura de tiling causa costura;\n"
                    "Repeat em sprite causa halo na borda.\n"
                    "Aplica na hora e fica gravado no .axemeta.");

            if (m_UUID.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(sem .axemeta - nao grava)");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Este arquivo nao esta no AssetDatabase do\n"
                        "projeto. O ajuste vale para esta sessao e nao\n"
                        "sobrevive ao reinicio.");
            }
        }

        ImGui::Spacing();
        ui::SectionHeader(ICON_FILE, "Arquivo", ui::Accent::Neutral);
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

    // ASSET_VIEWER_V2c — a parte VISUAL da malha, na janela principal.
    //
    // Separada dos numeros porque as duas se ajustam de forma diferente: o
    // preview quer area, a ficha quer altura para caber a lista inteira sem
    // rolagem. Enquanto viviam na mesma janela, esticar uma encolhia a outra.
    void AssetViewerWindow::DrawMeshViewport()
    {
        if (!m_Mesh)
        {
            ImGui::TextDisabled("Nao foi possivel carregar a malha.");
            ImGui::TextDisabled("%s", m_Path.string().c_str());
            return;
        }

        if (!m_PreviewEnabled)
        {
            ImGui::TextDisabled("Preview 3D desligado.");
            ImGui::TextDisabled("Religue no botao do cubo, na barra acima.");
            return;
        }

        // Ocupa TODA a area restante. Com a ficha morando na outra metade
        // da janela, nada mais disputa altura aqui.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        m_Preview.Draw(ImVec2(avail.x, std::max(avail.y, 64.0f)));
    }

    void AssetViewerWindow::DrawMeshPanel()
    {
        if (!m_Mesh)
        {
            ImGui::TextDisabled("Nao foi possivel carregar a malha.");
            ImGui::TextDisabled("%s", m_Path.string().c_str());
            return;
        }

        ui::SectionHeader(ICON_CUBE, "Geometria", ui::Accent::Neutral);
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
                // ═══════════════════════════════════════════════════════════
                //  ASSET_VIEWER_V2c — as DUAS leituras, nao um palpite
                //
                //  A versao anterior dizia "malha de DCC costuma vir em cm,
                //  divida por 100". Para a pistola de 185 unidades isso dava
                //  1,85 m — a medida de uma PESSOA, nao de uma pistola. O
                //  arquivo estava em milimetros, e o painel apontava com
                //  confianca para o lado errado.
                //
                //  O erro nao foi a conta: foi fingir que da para saber a
                //  unidade olhando so o numero. 185 e uma pistola em mm e um
                //  personagem em cm, e quem sabe qual dos dois e o objeto e
                //  quem o modelou. Entao o painel mostra as duas leituras e
                //  deixa a escolha com quem tem a informacao.
                // ═══════════════════════════════════════════════════════════
                ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.35f, 1.0f),
                    "Maior dimensao: %.1f unidades (em %s) - nao esta em metros.",
                    biggest, axis);
                ImGui::TextDisabled("  Se o arquivo estiver em mm: %.3f m", biggest * 0.001f);
                ImGui::TextDisabled("  Se estiver em cm:          %.3f m", biggest * 0.01f);
                ImGui::TextDisabled("  Qual das duas bate com o objeto real? E essa.");
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
        ui::SectionHeader(ICON_FILE, "Arquivo", ui::Accent::Neutral);
        ImGui::TextDisabled("%s", m_Path.string().c_str());

        ImGui::Spacing();
        DrawImportSettings();

        ImGui::Spacing();
        ImGui::SeparatorText("Ainda nao");
        ImGui::TextDisabled("Slots de material e colisao por asset.");
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  ASSET_VIEWER_V2 — configuracao de importacao da malha
    //
    //  ── O QUE ESTE BLOCO RESOLVE ──────────────────────────────────────────
    //
    //  O painel de cima ja DIAGNOSTICAVA ("maior dimensao 1,03 m", "pivo fora
    //  do objeto") e mandava consertar no DCC. Diagnostico sem conserto e
    //  meia ferramenta: para mudar a escala de uma malha era preciso abrir o
    //  Blender, reexportar e torcer para nada mais ter mudado no caminho.
    //
    //  Aqui o ajuste vive no .axemeta, ao lado do arquivo, e o FBX original
    //  nunca e tocado. Reexportar do DCC nao perde a configuracao.
    //
    //  ── POR QUE NOS VERTICES, E NAO NO TRANSFORM ──────────────────────────
    //
    //  Compensar escala e pivo no Transform de cada instancia parece mais
    //  barato e e a armadilha: a compensacao teria de ser repetida em toda
    //  instancia, nao valeria para socket, colisao, bounds nem particula
    //  presa no osso — que leem a MALHA, e nao o Transform de quem a usa. Um
    //  asset com escala errada e um defeito do ASSET, e e nele que se
    //  conserta.
    //
    //  ── POR QUE UM BOTAO, E NAO APLICAR AO ARRASTAR ───────────────────────
    //
    //  Reimportar reabre o arquivo pelo Assimp e reconstroi a malha na GPU.
    //  Fazer isso a cada pixel de arrasto do slider travaria o editor num FBX
    //  grande. O drag mexe na copia; o botao e que paga o custo.
    // ═══════════════════════════════════════════════════════════════════════
    bool AssetViewerWindow::DrawImportSettings()
    {
        ui::SectionHeader(ICON_SLIDERS, "Importacao (.axemeta)", ui::Accent::Primary);

        if (m_UUID.empty())
        {
            ImGui::TextDisabled("Este arquivo nao esta no AssetDatabase do projeto,");
            ImGui::TextDisabled("entao nao ha .axemeta onde gravar a configuracao.");
            return false;
        }

        bool changed = false;

        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::DragFloat("Escala", &m_Import.MeshScale, 0.005f, 0.0001f, 1000.0f,
            "%.4f", ImGuiSliderFlags_AlwaysClamp))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Multiplica as posicoes dos vertices na importacao.\n"
                "Normais nao mudam: a escala e uniforme.");

        // Atalhos para as duas trocas de unidade que respondem por quase todo
        // caso real. Nao adivinham nada — so poupam a conta.
        // Os atalhos acendem quando a escala em vigor E aquela conversao: assim
        // a barra nao e so um teclado de atalhos, ela tambem RESPONDE "em que
        // unidade este asset esta configurado agora".
        ImGui::SameLine();
        if (ui::ToggleButton("mm", std::abs(m_Import.MeshScale - 0.001f) < 1e-6f,
            "Arquivo em milimetros (escala 0,001)"))
        {
            m_Import.MeshScale = 0.001f;  changed = true;
        }
        ImGui::SameLine();
        if (ui::ToggleButton("cm", std::abs(m_Import.MeshScale - 0.01f) < 1e-6f,
            "Arquivo em centimetros (escala 0,01)"))
        {
            m_Import.MeshScale = 0.01f;   changed = true;
        }
        ImGui::SameLine();
        if (ui::ToggleButton("in", std::abs(m_Import.MeshScale - 0.0254f) < 1e-6f,
            "Arquivo em polegadas (escala 0,0254)"))
        {
            m_Import.MeshScale = 0.0254f; changed = true;
        }
        ImGui::SameLine();
        if (ui::ToggleButton("1:1", std::abs(m_Import.MeshScale - 1.0f) < 1e-6f,
            "Arquivo ja em metros"))
        {
            m_Import.MeshScale = 1.0f;    changed = true;
        }

        // Alvo por tamanho: o usuario sabe quanto o objeto MEDE no mundo real
        // ("essa pistola tem 19 cm"), e quase nunca sabe o fator. Converter e
        // uma divisao — mas e a divisao que ele faria errado as 2 da manha.
        if (m_HaveBounds)
        {
            const glm::vec3 size = m_BoundsMax - m_BoundsMin;
            const float biggest = std::max({ size.x, size.y, size.z });
            if (biggest > 0.0001f)
            {
                ImGui::SetNextItemWidth(160.0f);
                ImGui::DragFloat("Maior dimensao alvo (m)", &m_TargetSize,
                    0.01f, 0.001f, 1000.0f, "%.3f");
                ImGui::SameLine();
                if (ui::AccentButton(ICON_MAGNIFYING_GLASS "  Calcular", ui::Accent::Add,
                    "Escreve a escala que faz a maior dimensao bater\n"
                    "com o valor pedido"))
                {
                    // O bounds exibido ja esta COM a escala atual aplicada —
                    // por isso o fator novo e relativo a ela, e nao absoluto.
                    // Ignorar isso faria cada clique reaplicar a correcao por
                    // cima da anterior.
                    m_Import.MeshScale *= (m_TargetSize / biggest);
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Escreve a escala que faz a maior dimensao\n"
                        "bater com o valor pedido. Parte do tamanho ATUAL,\n"
                        "que ja inclui a escala em vigor.");
            }
        }

        if (ImGui::Checkbox("Centralizar pivo", &m_Import.RecenterPivot)) changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Move os vertices para que o centro dos bounds\n"
                "caia na origem. Conserta rotacao torta e encaixe\n"
                "em socket sem compensar a mao no Transform.");

        if (ImGui::Checkbox("Apoiar no chao (Y=0)", &m_Import.DropToFloor)) changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Depois de centralizar, sobe a malha ate a base\n"
                "encostar em Y=0. E o que se quer em personagem e\n"
                "mobilia; em projetil ou peca solta, nao.");

        // Tudo daqui para cima mexe em VERTICES: precisa de reimportacao.
        if (changed) { m_Dirty = true; m_GeometryDirty = true; }

        ImGui::Spacing();
        if (DrawSpawnDefaults()) m_Dirty = true;

        // ── O que esta REALMENTE no disco ──────────────────────────────────
        //
        // Existe por causa de um episodio concreto: uma escala foi ajustada,
        // reimportada, e o preview mostrou o objeto do tamanho de antes. Sem
        // esta linha nao havia como distinguir tres explicacoes diferentes —
        // nao gravou, gravou e nao aplicou, ou e outro asset. Cada uma leva a
        // uma investigacao diferente, e o painel nao dizia qual.
        {
            float savedScale = 1.0f;
            bool  savedRecenter = false, savedFloor = false;
            if (const AssetRecord* rec = AssetDatabase::Get().GetByUUID(m_UUID))
            {
                savedScale = rec->Import.MeshScale;
                savedRecenter = rec->Import.RecenterPivot;
                savedFloor = rec->Import.DropToFloor;
            }

            // "Gravado" e o registro do AssetDatabase, que e o que vai para o
            // `.axemeta` E para o indice do projeto. Dizer ".axemeta" era
            // impreciso: esta linha nunca leu o arquivo, e essa impressao
            // falsa foi o que atrapalhou o diagnostico do bug do IMPORT_PERSIST_V2.
            ImGui::TextDisabled("Gravado: escala %.4f%s%s",
                savedScale,
                savedRecenter ? ", pivo centralizado" : "",
                savedFloor ? ", apoiado no chao" : "");

            if (m_HaveBounds)
            {
                const glm::vec3 sz = m_BoundsMax - m_BoundsMin;
                ImGui::TextDisabled("Malha em memoria: maior dimensao %.3f m",
                    std::max({ sz.x, sz.y, sz.z }));
            }
        }

        ImGui::Spacing();

        // AccentButton, e nao Button: a cor diz o que acontece. Reimportar e
        // a acao principal deste painel (azul); descartar e a saida (neutra).
        // A mesma paleta de intencao dos outros editores — ver editor_widgets.
        ImGui::BeginDisabled(!m_Dirty);
        if (ui::AccentButton(
            m_GeometryDirty ? ICON_CHECK "  Aplicar e reimportar"
            : ICON_CHECK "  Aplicar",
            ui::Accent::Primary,
            m_GeometryDirty
            ? "Grava no .axemeta e recarrega a malha do disco"
            : "Grava no .axemeta. Nao ha o que reimportar:\nmaterial e "
            "colisao nao mexem na geometria."))
        {
            // Reimportar so quando a GEOMETRIA mudou. Ver a nota do
            // m_GeometryDirty no header.
            if (m_GeometryDirty) Reimport();
            else                 PersistImportSettings();
            m_GeometryDirty = false;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!m_Dirty);
        if (ui::AccentButton(ICON_ROTATE_LEFT "  Descartar", ui::Accent::Neutral,
            "Volta os campos ao que esta gravado no disco"))
        {
            if (const AssetRecord* rec = AssetDatabase::Get().GetByUUID(m_UUID))
                m_Import = rec->Import;
            m_Dirty = false;
            m_GeometryDirty = false;
            RefreshColliderPreview();
        }
        ImGui::EndDisabled();

        if (m_Dirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.35f, 1.0f), "nao aplicado");
        }

        // A honestidade que evita um bug reportado que nao existe: objetos JA
        // colocados na cena seguram o ponteiro da malha antiga (o cache
        // devolvia a mesma instancia). Eles so pegam a versao nova quando a
        // cena recarregar. Dizer isso aqui custa duas linhas; descobrir
        // sozinho custa uma tarde.
        ImGui::Spacing();
        ImGui::TextDisabled("A malha e reimportada aqui e no proximo load da cena.");
        ImGui::TextDisabled("Objetos ja na cena so pegam a versao nova ao recarregar.");

        return changed;
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  ASSET_DEFAULTS_V1 — material padrao e colisao
    //
    //  ── POR QUE ISTO E DO ASSET, E NAO DA INSTANCIA ───────────────────────
    //
    //  "Toda vez que arrasto esta caixa para a cena eu troco o material e
    //  desenho o mesmo collider a mao" e uma propriedade do ASSET vestida de
    //  tarefa repetitiva. Uma pistola tem um material e tem uma forma de
    //  colisao; isso nao muda de instancia para instancia.
    //
    //  ── E CONTINUA SENDO SO UM PADRAO ─────────────────────────────────────
    //
    //  O que nasce na cena e um MaterialComponent e um ColliderComponent
    //  comuns, que o Inspector edita como sempre. Mudar aqui NAO reescreve o
    //  que ja foi colocado — se reescrevesse, um ajuste de importacao poderia
    //  alterar uma cena salva pelas costas de quem a salvou.
    // ═══════════════════════════════════════════════════════════════════════
    bool AssetViewerWindow::DrawSpawnDefaults()
    {
        ui::SectionHeader(ICON_PALETTE, "Ao instanciar na cena", ui::Accent::Primary);

        bool changed = false;

        // O AssetPicker ja existe e ja aceita arrastar do Asset Browser. Fazer
        // um combo proprio aqui daria um seletor de material que se comporta
        // diferente de todos os outros da engine.
        if (AssetPicker::Draw("Material padrao", m_Import.DefaultMaterialUUID,
            { AssetType::Material }, nullptr))
        {
            changed = true;

            // Aplica no preview IMEDIATAMENTE, sem esperar o botao Aplicar. O
            // ponto de escolher um material e ver como fica; adiar isso ate a
            // gravacao transformaria a escolha num chute.
            if (m_Type == AssetType::Mesh && m_Mesh)
            {
                auto mat = m_Import.DefaultMaterialUUID.empty()
                    ? MeshLoader::Load(m_Path.string(), true).MaterialData
                    : AssetSpawnDefaults::ResolveMaterial(m_Import.DefaultMaterialUUID);
                m_Preview.SetMesh(m_Mesh, mat);
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Substitui o material que veio do arquivo.\n"
                "Vazio mantem o do proprio FBX.");

        ImGui::Spacing();

        // -1 (nenhuma) mora fora do enum ColliderShape, entao o combo trabalha
        // com um indice deslocado. A conversao acontece so aqui: deixar o -1
        // vazar para o resto da UI espalharia o mesmo "+1/-1" por cinco
        // lugares, e um deles ficaria para tras.
        int shapeIdx = m_Import.CollisionShape + 1;
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::Combo("Colisao", &shapeIdx,
            "Nenhuma\0Caixa\0Esfera\0Capsula\0Malha exata\0Convex hull\0"))
        {
            m_Import.CollisionShape = shapeIdx - 1;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Caixa e esfera sao as baratas.\n"
                "Malha exata e SO para objeto estatico.\n"
                "Convex hull e a opcao para corpo que se move.");

        if (m_Import.CollisionShape >= 0)
        {
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::DragFloat("Folga (m)", &m_Import.CollisionPadding,
                0.005f, -0.5f, 2.0f, "%.3f"))
                changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Somada a cada lado do volume.\n"
                    "Collider colado na malha prende em quina e em degrau:\n"
                    "uns centimetros separam \"anda\" de \"engancha\".");

            if (ImGui::Checkbox("So gatilho (nao empurra)", &m_Import.CollisionIsTrigger))
                changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Detecta a sobreposicao mas nao colide.\n"
                    "Zona de coleta, sensor de porta, area de dano.");

            // A malha exata nao aceita corpo dinamico em motor de fisica
            // nenhum. Dizer isso AQUI, na hora da escolha, evita a versao cara
            // do aviso: o objeto atravessando o chao em pleno teste.
            if (m_Import.CollisionShape == 3)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.35f, 1.0f),
                    "Malha exata so funciona em objeto estatico.");
                ImGui::TextDisabled("  Para algo que se move, use Convex hull.");
            }
        }

        if (changed) RefreshColliderPreview();
        return changed;
    }

    void AssetViewerWindow::RefreshColliderPreview()
    {
        if (m_Type != AssetType::Mesh || !m_PreviewEnabled) return;

        if (m_Import.CollisionShape < 0 || !m_Mesh)
        {
            m_Preview.ClearCollider();
            return;
        }

        // O MESMO AssetSpawnDefaults::BuildCollider que a cena vai usar. Se o
        // preview calculasse o volume por conta propria, ele mostraria uma
        // coisa e a cena criaria outra — e a divergencia so apareceria depois,
        // com o objeto ja posicionado.
        ColliderComponent collider;
        if (AssetSpawnDefaults::BuildCollider(*m_Mesh, m_Import, collider))
            m_Preview.SetCollider(collider);
        else
            m_Preview.ClearCollider();
    }

    // Grava as settings no .axemeta. Usado direto pelos ajustes de textura,
    // que nao precisam de reimportacao, e pelo Reimport da malha.
    void AssetViewerWindow::PersistImportSettings()
    {
        if (m_UUID.empty()) return;
        AssetDatabase::Get().SetImportSettings(m_UUID, m_Import);
        m_Dirty = false;
    }

    void AssetViewerWindow::Reimport()
    {
        if (m_UUID.empty()) return;

        PersistImportSettings();

        // Invalidar o cache CERTO. Malha e textura tem caches separados e
        // independentes; errar o cache aqui daria o pior sintoma de todos —
        // "salvei, o arquivo mudou, e a tela continua igual".
        const std::string file = m_Path.string();
        if (m_Type == AssetType::Mesh)   MeshLoader::InvalidateCache(file);
        if (m_Type == AssetType::Texture) Texture2D::InvalidateCache(file);

        // Recarrega pelo mesmo caminho da abertura, para nao haver duas
        // versoes da rotina de carga divergindo com o tempo. Zoom e pan sao
        // preservados: reimportar nao e trocar de asset, e devolver a camera
        // ao inicio a cada ajuste tornaria a comparacao antes/depois inutil.
        m_KeepPreviewCamera = true;   // ver a nota no Open

        const float  zoom = m_Zoom;
        const ImVec2 pan = m_Pan;
        const bool   fit = m_FitOnOpen;

        Open(m_Path, m_Type, m_Name);

        m_Zoom = zoom;
        m_Pan = pan;
        m_FitOnOpen = fit;
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  ASSET_VIEWER_V2d — UMA janela, duas regioes, um divisor
    //
    //  ── O QUE DEU ERRADO ANTES ────────────────────────────────────────────
    //
    //  A versao anterior separou a ficha numa segunda JANELA dockavel. A
    //  intencao era boa (as duas metades querem coisas opostas do espaco) e o
    //  resultado nao: o ImGui docava a ficha em qualquer canto da engine, e o
    //  painel de numeros de um asset ia parar longe da imagem a que se refere,
    //  sem nada ligando os dois. O Asset Viewer deixou de ser UMA ferramenta.
    //
    //  ── O QUE RESOLVE ─────────────────────────────────────────────────────
    //
    //  Uma janela com duas regioes e um divisor arrastavel entre elas. O
    //  ajuste que ele queria continua existindo — e melhor, porque e continuo
    //  em vez de depender de encontrar o alvo de dock certo — e o Asset Viewer
    //  volta a docar como uma coisa so, onde quer que ele o ponha.
    //
    //  O divisor tambem gira: lado a lado num monitor largo, empilhado num
    //  painel estreito. Um botao, nao uma configuracao.
    // ═══════════════════════════════════════════════════════════════════════
    void AssetViewerWindow::Draw()
    {
        if (!m_Open) return;

        // Titulo com o nome do asset e ID fixo: sem o `###`, o ImGui trataria
        // cada asset como uma JANELA diferente e o layout dockado se perderia
        // a cada abertura. Mesmo padrao dos outros editores.
        char title[256];
        std::snprintf(title, sizeof(title), "Asset Viewer - %s###AssetViewer",
            m_Name.empty() ? "(sem asset)" : m_Name.c_str());

        if (!ImGui::Begin(title, &m_Open))
        {
            ImGui::End();
            return;
        }

        DrawToolbar();
        ImGui::Separator();

        const ImVec2 avail = ImGui::GetContentRegionAvail();

        if (!m_ShowDetails)
        {
            ImGui::BeginChild("##visual", ImVec2(0, 0), false);
            DrawVisual();
            ImGui::EndChild();
            ImGui::End();
            return;
        }

        // O divisor tem 6 px. Menos que isso e dificil de acertar com o mouse;
        // mais come area util e parece uma borda, nao uma alca.
        constexpr float kSplitter = 6.0f;

        if (!m_StackedLayout)
        {
            const float total = std::max(avail.x - kSplitter, 80.0f);
            float detailsW = std::clamp(total * m_DetailsFraction, 160.0f, total - 120.0f);
            const float visualW = total - detailsW;

            ImGui::BeginChild("##visual", ImVec2(visualW, 0), false);
            DrawVisual();
            ImGui::EndChild();

            ImGui::SameLine(0.0f, 0.0f);

            // O divisor. InvisibleButton e nao Button: ele nao deve ter
            // aparencia propria — quem desenha e a linha abaixo, que so
            // acende quando o mouse esta perto.
            ImGui::InvisibleButton("##split", ImVec2(kSplitter, std::max(avail.y, 1.0f)));
            const bool splitHot = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (splitHot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive())
            {
                detailsW = std::clamp(detailsW - ImGui::GetIO().MouseDelta.x,
                    160.0f, total - 120.0f);
                m_DetailsFraction = detailsW / total;
            }
            {
                const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                const float x = (a.x + b.x) * 0.5f;
                ImGui::GetWindowDrawList()->AddLine(ImVec2(x, a.y), ImVec2(x, b.y),
                    ImGui::GetColorU32(splitHot ? ImGuiCol_SliderGrab : ImGuiCol_Separator),
                    splitHot ? 2.0f : 1.0f);
            }

            ImGui::SameLine(0.0f, 0.0f);

            ImGui::BeginChild("##details", ImVec2(0, 0), false,
                ImGuiWindowFlags_AlwaysVerticalScrollbar);
            DrawDetails();
            ImGui::EndChild();
        }
        else
        {
            const float total = std::max(avail.y - kSplitter, 80.0f);
            float detailsH = std::clamp(total * m_DetailsFraction, 100.0f, total - 100.0f);
            const float visualH = total - detailsH;

            ImGui::BeginChild("##visual", ImVec2(0, visualH), false);
            DrawVisual();
            ImGui::EndChild();

            ImGui::InvisibleButton("##split", ImVec2(std::max(avail.x, 1.0f), kSplitter));
            const bool splitHot = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (splitHot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if (ImGui::IsItemActive())
            {
                detailsH = std::clamp(detailsH - ImGui::GetIO().MouseDelta.y,
                    100.0f, total - 100.0f);
                m_DetailsFraction = detailsH / total;
            }
            {
                const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                const float y = (a.y + b.y) * 0.5f;
                ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, y), ImVec2(b.x, y),
                    ImGui::GetColorU32(splitHot ? ImGuiCol_SliderGrab : ImGuiCol_Separator),
                    splitHot ? 2.0f : 1.0f);
            }

            ImGui::BeginChild("##details", ImVec2(0, 0), false,
                ImGuiWindowFlags_AlwaysVerticalScrollbar);
            DrawDetails();
            ImGui::EndChild();
        }

        ImGui::End();
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  A barra de ferramentas
    //
    //  Icone com tooltip, e nao checkbox com rotulo, pelo mesmo motivo do
    //  viewport e dos outros editores: uma fila de caixinhas rotuladas ocupa
    //  tres vezes a largura e ainda assim se le pior de relance. O tooltip nao
    //  e opcional em ui::IconButton justamente porque icone sozinho e enigma
    //  para quem chega — e quem ja conhece nunca le o tooltip.
    // ═══════════════════════════════════════════════════════════════════════
    void AssetViewerWindow::DrawToolbar()
    {
        if (m_Type == AssetType::Mesh)
        {
            if (ui::ToggleButton(ICON_CUBE, m_PreviewEnabled,
                "Preview 3D\nDesligar libera a GPU se a janela ficar aberta\n"
                "ao lado do viewport numa cena pesada."))
                m_PreviewEnabled = !m_PreviewEnabled;
            ImGui::SameLine();

            ImGui::BeginDisabled(!m_PreviewEnabled);

            if (ui::ToggleButton(ICON_BORDER_ALL, m_Preview.ShowGrid,
                "Grade de 1 m\nE o que torna a escala julgavel: 0,19 m e um\n"
                "numero; a malha ao lado de um metro e uma resposta."))
                m_Preview.ShowGrid = !m_Preview.ShowGrid;
            ImGui::SameLine();

            if (ui::IconButton(ICON_EXPAND,
                "Enquadrar\nRecentra a camera no objeto.\n"
                "Botao direito orbita, meio faz pan, roda aproxima.") && m_HaveBounds)
                m_Preview.FrameBounds(m_BoundsMin, m_BoundsMax);

            // Wireframe do collider. So aparece quando ha collider configurado
            // — um botao que nao pode fazer nada e ruido na barra.
            if (m_Import.CollisionShape >= 0)
            {
                ImGui::SameLine();
                if (ui::ToggleButton(ICON_BUG, m_Preview.ShowColliderWire,
                    "Wireframe da colisao\nCom convex hull ele cobre a malha de\n"
                    "linhas e a textura fica ilegivel. Desligar nao apaga\n"
                    "a colisao, so o desenho."))
                    m_Preview.ShowColliderWire = !m_Preview.ShowColliderWire;
            }

            ImGui::EndDisabled();
        }
        else if (m_Type == AssetType::Texture)
        {
            if (ui::IconButton(ICON_EXPAND, "Ajustar a textura na janela"))
            {
                m_FitOnOpen = true;
                m_Pan = ImVec2(0, 0);
            }
            ImGui::SameLine();

            if (ui::ToggleButton("1:1", std::abs(m_Zoom - 1.0f) < 0.001f,
                "Tamanho real, um texel por pixel"))
            {
                m_Zoom = 1.0f;
                m_Pan = ImVec2(0, 0);
                m_FitOnOpen = false;
            }
            ImGui::SameLine();

            // Canais como toggles coloridos: a cor do botao JA diz qual e o
            // canal, entao o rotulo de uma letra basta e nao precisa de
            // legenda nenhuma ao lado.
            if (ui::ToggleButton("R", m_ChR, "Mostrar o canal vermelho")) m_ChR = !m_ChR;
            ImGui::SameLine();
            if (ui::ToggleButton("G", m_ChG, "Mostrar o canal verde"))    m_ChG = !m_ChG;
            ImGui::SameLine();
            if (ui::ToggleButton("B", m_ChB, "Mostrar o canal azul"))     m_ChB = !m_ChB;
        }

        // ── Controles de layout, alinhados a direita ───────────────────────
        //
        // A direita porque nao sao sobre o ASSET, e sim sobre a janela. Mistura
        // dos dois na mesma fila e o que faz uma barra de ferramentas virar
        // uma gaveta.
        const float btn = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - (btn * 2.0f + spacing));

        ImGui::BeginDisabled(!m_ShowDetails);
        if (ui::IconButton(ICON_ARROWS_LEFT_RIGHT,
            m_StackedLayout ? "Ficha ao lado da imagem"
            : "Ficha abaixo da imagem"))
            m_StackedLayout = !m_StackedLayout;
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ui::ToggleButton(ICON_LIST, m_ShowDetails,
            "Mostrar a ficha do asset\n(geometria, bounds, importacao)"))
            m_ShowDetails = !m_ShowDetails;
    }

    void AssetViewerWindow::DrawVisual()
    {
        switch (m_Type)
        {
        case AssetType::Texture: DrawTexturePanel(); break;
        case AssetType::Mesh:    DrawMeshViewport(); break;
        default:
            ImGui::TextDisabled("Este tipo de asset ainda nao tem painel aqui.");
            break;
        }
    }

    void AssetViewerWindow::DrawDetails()
    {
        switch (m_Type)
        {
        case AssetType::Texture: DrawTextureDetails(); break;
        case AssetType::Mesh:    DrawMeshPanel();      break;
        default: break;
        }
    }
}
