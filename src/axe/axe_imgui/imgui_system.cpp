#include "axe/axe_imgui/imgui_system.hpp"
#include "axe/axe_window/window.hpp"
#include "axe/log/log.hpp"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// S0b — sem <ImGuizmo.h>: o gizmo e do editor. Ver BeginFrame().

#include <filesystem>

namespace axe
{

    bool ImGuiSystem::Initialize(axe::Window* window)
    {
        IMGUI_CHECKVERSION();
        m_Context = ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        // ── Estilo global — visual "moderno", consistente com os cards arredondados
        // já usados manualmente no Script Editor (Components/Variables/Events).
        // Sem isso, os widgets nativos do ImGui (InputText, Combo, sliders, botões
        // padrão) ficam com cantos retos e contraste chapado — destoando do resto.
        {
            ImGuiStyle& style = ImGui::GetStyle();
            ImGui::StyleColorsDark(&style);

            // ── COLOR_FLOAT_V1 — seletor de cor em 0..1, nao em 0..255 ──────
            //
            // O padrao do ImGui e ImGuiColorEditFlags_Uint8: TODO ColorEdit e
            // ColorPicker da engine mostra e aceita numero inteiro de 0 a 255.
            // Quem digita 0.30 num campo desses recebe 0 — o campo trunca, sem
            // dizer nada, e a cor vira preto.
            //
            // Isso mordia justamente onde a precisao importa: a engine guarda
            // e o GLSL consome cor em float linear 0..1. O grafo de material
            // salva 0.1398 no .axegraph, mas o unico jeito de DIGITAR esse
            // valor era converter para 0..255 de cabeca e aceitar o passo de
            // 1/255. Numa agua estilizada, onde a diferenca entre a cor do
            // raso e a do fundo e o efeito inteiro, isso e a diferenca entre
            // acertar e nao acertar.
            //
            // SetColorEditOptions troca o DEFAULT global do contexto, entao
            // vale para os ~20 seletores ja espalhados pelo editor (luz, fog,
            // ceu, particula, notify de animacao, node Color, parametro de
            // material) sem tocar em nenhuma chamada. Um call site que queira
            // 0..255 continua podendo pedir ImGuiColorEditFlags_Uint8.
            ImGui::SetColorEditOptions(
                ImGuiColorEditFlags_Float |
                ImGuiColorEditFlags_DisplayRGB |
                ImGuiColorEditFlags_InputRGB |
                ImGuiColorEditFlags_PickerHueBar);

            // Arredondamento pronunciado (~8-10px), estilo UE5/Figma
            style.WindowRounding = 8.0f;
            style.ChildRounding = 8.0f;
            style.FrameRounding = 8.0f;   // InputText, Combo, sliders, checkboxes
            style.PopupRounding = 8.0f;
            style.ScrollbarRounding = 10.0f;
            style.GrabRounding = 8.0f;    // bolinha de sliders/scrollbar
            style.TabRounding = 8.0f;

            // Bordas finas e discretas em vez de chapadas
            style.WindowBorderSize = 1.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupBorderSize = 1.0f;
            style.FrameBorderSize = 1.0f;
            style.TabBorderSize = 0.0f;

            // Respiro — mais espaço interno e entre itens, sem ficar exagerado
            style.FramePadding = ImVec2(8.0f, 5.0f);
            style.ItemSpacing = ImVec2(8.0f, 6.0f);
            style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
            style.WindowPadding = ImVec2(8.0f, 8.0f);
            style.ScrollbarSize = 14.0f;
            style.GrabMinSize = 10.0f;
            style.IndentSpacing = 18.0f;

            // Cores — fundo dos campos com mais profundidade (não tão chapado/escuro
            // quanto o padrão), e accent azul consistente com os cards já feitos.
            ImVec4* c = style.Colors;
            c[ImGuiCol_WindowBg] = ImVec4(0.098f, 0.106f, 0.133f, 1.00f);
            c[ImGuiCol_ChildBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            c[ImGuiCol_PopupBg] = ImVec4(0.087f, 0.094f, 0.118f, 0.98f);
            c[ImGuiCol_Border] = ImVec4(1.000f, 1.000f, 1.000f, 0.08f);
            c[ImGuiCol_FrameBg] = ImVec4(0.165f, 0.176f, 0.212f, 1.00f);
            c[ImGuiCol_FrameBgHovered] = ImVec4(0.204f, 0.220f, 0.267f, 1.00f);
            c[ImGuiCol_FrameBgActive] = ImVec4(0.165f, 0.176f, 0.212f, 1.00f);
            c[ImGuiCol_TitleBg] = ImVec4(0.071f, 0.078f, 0.098f, 1.00f);
            c[ImGuiCol_TitleBgActive] = ImVec4(0.110f, 0.122f, 0.165f, 1.00f);
            c[ImGuiCol_MenuBarBg] = ImVec4(0.087f, 0.094f, 0.118f, 1.00f);
            c[ImGuiCol_ScrollbarBg] = ImVec4(0.071f, 0.078f, 0.098f, 0.60f);
            c[ImGuiCol_ScrollbarGrab] = ImVec4(0.300f, 0.310f, 0.350f, 1.00f);
            c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.380f, 0.390f, 0.430f, 1.00f);
            c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.450f, 0.460f, 0.500f, 1.00f);
            c[ImGuiCol_CheckMark] = ImVec4(0.270f, 0.600f, 1.000f, 1.00f);
            c[ImGuiCol_SliderGrab] = ImVec4(0.270f, 0.600f, 1.000f, 1.00f);
            c[ImGuiCol_SliderGrabActive] = ImVec4(0.350f, 0.680f, 1.000f, 1.00f);
            c[ImGuiCol_Button] = ImVec4(0.180f, 0.192f, 0.231f, 1.00f);
            c[ImGuiCol_ButtonHovered] = ImVec4(0.230f, 0.245f, 0.290f, 1.00f);
            c[ImGuiCol_ButtonActive] = ImVec4(0.150f, 0.160f, 0.196f, 1.00f);
            c[ImGuiCol_Header] = ImVec4(0.180f, 0.220f, 0.330f, 1.00f);
            c[ImGuiCol_HeaderHovered] = ImVec4(0.230f, 0.280f, 0.400f, 1.00f);
            c[ImGuiCol_HeaderActive] = ImVec4(0.200f, 0.250f, 0.360f, 1.00f);
            c[ImGuiCol_Separator] = ImVec4(1.000f, 1.000f, 1.000f, 0.07f);
            c[ImGuiCol_Tab] = ImVec4(0.110f, 0.122f, 0.157f, 1.00f);
            c[ImGuiCol_TabHovered] = ImVec4(0.200f, 0.250f, 0.360f, 1.00f);
            c[ImGuiCol_TabActive] = ImVec4(0.160f, 0.200f, 0.290f, 1.00f);
            c[ImGuiCol_DockingPreview] = ImVec4(0.270f, 0.600f, 1.000f, 0.35f);
            c[ImGuiCol_ResizeGrip] = ImVec4(0.270f, 0.600f, 1.000f, 0.20f);
            c[ImGuiCol_ResizeGripHovered] = ImVec4(0.270f, 0.600f, 1.000f, 0.55f);
            c[ImGuiCol_ResizeGripActive] = ImVec4(0.270f, 0.600f, 1.000f, 0.80f);
        }
        // ── Fonte de icones ──────────────────────────────────────────────────
        //
        // MESCLADA na fonte de texto, nao carregada como fonte separada. Com
        // MergeMode, "\uf0c7  Salvar" desenha o icone e o texto numa chamada so —
        // sem PushFont/PopFont em volta de cada botao, que e o que faz a
        // alternativa virar ruido em cada janela.
        //
        // A fonte e um SUBSET (assets/fonts/axe_icons.ttf, ~14 KB): so os glifos
        // que o editor usa de fato. Ver editor_icons.hpp — acrescentar um define
        // sem regerar o subset produz um retangulo vazio, nao um erro.
        {
            // ═══════════════════════════════════════════════════════════
            //  UI_FONT_V1 — a fonte de TEXTO
            //
            //  ── O DEFEITO QUE ISTO CONSERTA ────────────────────────────
            //
            //  Ate aqui o editor usava a ProggyClean embutida do ImGui
            //  (AddFontDefault). Ela e bitmap, so tem ASCII, e nao tem
            //  glifo nenhum acima de 0x7F — ou seja, TODO acento e todo
            //  travessao do editor saiam como '?'. E por isso que o titulo
            //  aparecia "Asset Viewer ? Pistol": o travessao nao existia na
            //  fonte, e nao havia erro nenhum para investigar.
            //
            //  ── POR QUE FONTE DO SISTEMA, E NAO UMA EMBARCADA ─────────
            //
            //  Embarcar uma TTF de texto significa escolher uma licenca,
            //  versionar um binario de centenas de KB e mante-lo. A fonte da
            //  interface do proprio sistema resolve o mesmo problema, ja
            //  esta instalada, e casa com o resto do desktop do usuario.
            //
            //  ── E SE NAO ACHAR NENHUMA ────────────────────────────────
            //
            //  Cai na ProggyClean, exatamente como antes. O editor nunca
            //  fica sem fonte: o pior caso e o comportamento atual.
            // ═══════════════════════════════════════════════════════════
            {
                // 16 px. A primeira tentativa foi 14 — perto demais da
                // ProggyClean para valer a troca, e ainda apertado numa tela
                // grande. 16 e o corpo de editor moderno (VS Code, Rider) e e
                // o que da respiro sem empurrar os layouts ajustados em pixel
                // pela engine. Um numero so controla tudo: mude aqui.
                constexpr float kUIFontSize = 16.0f;

                const char* kCandidates[] = {
                    "C:/Windows/Fonts/segoeui.ttf",   // Windows 7+
                    "C:/Windows/Fonts/tahoma.ttf",    // fallback antigo
                    "C:/Windows/Fonts/arial.ttf",
                    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                    "/System/Library/Fonts/SFNS.ttf",
                };

                // Latin-1 mais a Pontuacao Geral. A segunda faixa nao e
                // luxo: travessao (U+2014), meia-risca e aspas curvas caem
                // TODAS fora do Latin-1, e sao exatamente os caracteres que
                // apareciam como '?' — GetGlyphRangesDefault() para em
                // 0x00FF e nao cobriria nenhum deles.
                static const ImWchar kTextRange[] = {
                    0x0020, 0x00FF,   // ASCII + Latin-1 (todo acento)
                    0x2010, 0x2027,   // travessao, meia-risca, aspas curvas
                    0,
                };

                ImFontConfig textCfg;
                textCfg.OversampleH = 2;   // texto pequeno em tela nao-HiDPI
                textCfg.OversampleV = 1;
                textCfg.PixelSnapH = false;

                ImFont* uiFont = nullptr;
                for (const char* candidate : kCandidates)
                {
                    if (!std::filesystem::exists(candidate)) continue;

                    // Latin-1: cobre todo acento de portugues, espanhol,
                    // frances e alemao. Nao ha custo em pedir a faixa
                    // inteira — o atlas so gera os glifos que ela contem.
                    uiFont = io.Fonts->AddFontFromFileTTF(candidate,
                        kUIFontSize, &textCfg, kTextRange);

                    if (uiFont) break;
                }

                if (!uiFont)
                {
                    io.Fonts->AddFontDefault();
                    AXE_CORE_WARN("ImGui: nenhuma fonte de sistema encontrada — "
                        "usando a ProggyClean embutida (sem acentos).");
                }
            }

            static const ImWchar kIconRange[] = { 0xe4e2, 0xf84c, 0 };

            ImFontConfig cfg;
            cfg.MergeMode = true;
            cfg.PixelSnapH = true;

            // GlyphOffset e GlyphMinAdvanceX ficam ZERADOS de proposito.
            //
            // Os dois foram tentados e produziram o mesmo defeito por caminhos
            // diferentes: o MinAdvance alarga o avanco do glifo sem alargar o
            // DESENHO, e o ImGui centraliza pelo avanco — entao dentro de um
            // botao quadrado o icone escorrega pra esquerda. O Offset y era um
            // chute meu, e empurrava tudo pra baixo.
            //
            // A metrica real do glifo e a referencia certa. Onde o icone
            // precisa de espaco (ICON_X "  Texto"), o espaco esta no literal, e
            // e visivel a quem escreve; onde precisa de centralizacao exata
            // (botao quadrado), quem resolve e o ui::IconButton, que sabe o
            // tamanho da caixa.

            // Em resources/, que o premake ja copia pro lado do executavel no
            // pos-build. Caminho RELATIVO porque o debugdir do projeto e o
            // proprio targetdir — a mesma convencao das outras resources.
            const char* kIconFont = "resources/fonts/axe_icons.ttf";

            if (std::filesystem::exists(kIconFont))
            {
                // 13 px era o corpo da ProggyClean. Com o texto em 16 os
                // icones ficariam visivelmente menores que as letras ao lado
                // deles — a barra de ferramentas inteira pareceria desalinhada.
                // Um pouco MENOR que o texto e proposital: o glifo de icone
                // ocupa a caixa toda, a letra nao.
                io.Fonts->AddFontFromFileTTF(kIconFont, 15.0f, &cfg, kIconRange);
            }
            else
            {
                // Sem a fonte o editor CONTINUA funcionando: os defines viram
                // caracteres desconhecidos e aparecem como retangulos. Avisar
                // uma vez e melhor que uma tela cheia de quadradinhos sem
                // explicacao.
                AXE_CORE_WARN("ImGui: '{}' nao encontrado — os icones do editor vao "
                    "aparecer como retangulos vazios.", kIconFont);
            }
        }

        m_NativeWindow = static_cast<GLFWwindow*>(window->GetNativeWindow());

        if (!ImGui_ImplGlfw_InitForOpenGL(m_NativeWindow, true))
        {
            AXE_CORE_ERROR("Failed to initialize ImGui GLFW backend");
            return false;
        }

        if (!ImGui_ImplOpenGL3_Init("#version 450"))
        {
            AXE_CORE_ERROR("Failed to initialize ImGui OpenGL3 backend");
            return false;
        }

        //AXE_CORE_INFO("ImGui initialized");
        return true;
    }

    void ImGuiSystem::Shutdown()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        AXE_CORE_INFO("ImGui shutdown");
    }

    void ImGuiSystem::BeginFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // S0b — `ImGuizmo::BeginFrame()` SAIU daqui e passou a ser chamado
        // pela ImGuiLayer, do lado do editor.
        //
        // Nao e cosmetica: o ImGuizmo e ferramenta de autoria (gizmo de
        // translacao/rotacao/escala) e agora e compilado no `editor.exe`,
        // junto do unico consumidor dele, o `viewport_renderer`. As funcoes
        // dele sao livres, sem macro de export — chamar daqui deixaria a
        // `axe.dll` pedindo um simbolo que o exe tem e ela nao.
        //
        // E, no fundo, o binding de ImGui com a janela nao deveria mesmo
        // saber que existe gizmo.
    }

    void ImGuiSystem::EndFrame()
    {
        ImGui::Render();

        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData && drawData->Valid)
            ImGui_ImplOpenGL3_RenderDrawData(drawData);
    }

    void ImGuiSystem::OnEvent(Event& event)
    {
        // Backend GLFW cuida de todos os eventos — não precisamos processar manualmente
    }

} // namespace axe