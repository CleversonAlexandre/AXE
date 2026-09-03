// material_shader_log.cpp
// Log de compilação do shader exibido na janela "Shader Log" (info/warning/
// error), com auto-scroll.

#include "material_editor_window.hpp"

// MATERIAL_EDITOR_STYLE_V1 — mesmos widgets/icones do Script, Rig e Anim.
#include "editor/axe_editor/ui/editor_widgets.hpp"
#include "editor/axe_editor/ui/editor_icons.hpp"

#include <imgui.h>
#include <sstream>

namespace axe
{

    void MaterialEditorWindow::LogInfo(const std::string& msg)
    {
        std::istringstream stream(msg);
        std::string line;
        while (std::getline(stream, line))
            if (!line.empty())
                m_ShaderLog.push_back({ ShaderLogEntry::Level::Info, line });
    }
    void MaterialEditorWindow::LogWarning(const std::string& msg)
    {
        m_ShaderLog.push_back({ ShaderLogEntry::Level::Warnning, msg });
    }
    void MaterialEditorWindow::LogError(const std::string& msg)
    {
        // Divide mensagens multi-linha em entradas separadas
        std::istringstream stream(msg);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty())
                m_ShaderLog.push_back({ ShaderLogEntry::Level::Error, line });
        }
    }
    void MaterialEditorWindow::ClearLog()
    {
        m_ShaderLog.clear();
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  MATERIAL_EDITOR_STYLE_V1 — o Shader Log
    //
    //  Duas mudancas, e as duas sao de LEITURA, nao de enfeite:
    //
    //  1. O prefixo era "[INFO]"/"[ERR]" grudado na mensagem, com a cor como
    //     unica pista. Virou ICONE + cor, que e o que o resto do editor faz —
    //     e o que permite achar o erro correndo o olho pela coluna, sem ler.
    //
    //  2. Um CONTADOR DE ERROS ao lado do total. Uma compilacao que falha
    //     costuma cuspir cinco linhas de INFO e duas de ERR; sem o contador, a
    //     unica forma de saber se deu certo e ler tudo ate o fim.
    // ═════════════════════════════════════════════════════════════════════════
    void MaterialEditorWindow::DrawShaderLog()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));

        if (ImGui::Begin("Shader Log", nullptr, ImGuiWindowFlags_NoScrollbar))
        {
            if (ui::IconButton(ICON_TRASH, "Limpar o log", ui::Accent::Danger))
                ClearLog();

            ImGui::SameLine();

            int errors = 0, warnings = 0;
            for (auto& e : m_ShaderLog)
            {
                if (e.level == ShaderLogEntry::Level::Error)    ++errors;
                if (e.level == ShaderLogEntry::Level::Warnning) ++warnings;
            }

            ImGui::TextDisabled("%d mensagens", (int)m_ShaderLog.size());

            if (warnings > 0)
            {
                ImGui::SameLine();
                ImGui::TextColored(ui::AccentColor(ui::Accent::Warning),
                    ICON_TRIANGLE_EXCLAMATION "  %d", warnings);
            }
            if (errors > 0)
            {
                ImGui::SameLine();
                ImGui::TextColored(ui::AccentColor(ui::Accent::Danger),
                    ICON_XMARK "  %d", errors);
            }
            else if (!m_ShaderLog.empty())
            {
                ImGui::SameLine();
                ImGui::TextColored(ui::AccentColor(ui::Accent::Add), ICON_CHECK);
            }

            ImGui::Separator();

            ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false,
                ImGuiWindowFlags_HorizontalScrollbar);

            for (auto& entry : m_ShaderLog)
            {
                ImVec4 color;
                const char* icon;

                switch (entry.level)
                {
                case ShaderLogEntry::Level::Warnning:
                    color = ui::AccentColor(ui::Accent::Warning);
                    icon = ICON_TRIANGLE_EXCLAMATION;
                    break;
                case ShaderLogEntry::Level::Error:
                    color = ui::AccentColor(ui::Accent::Danger);
                    icon = ICON_XMARK;
                    break;
                case ShaderLogEntry::Level::Info:
                default:
                    color = ImVec4(0.62f, 0.66f, 0.72f, 1.0f);
                    icon = ICON_CIRCLE_INFO;
                    break;
                }

                // O icone leva a cor de intencao; a MENSAGEM fica em cinza
                // claro. Linha inteira colorida de vermelho cansa a vista e,
                // pior, esconde onde o erro comeca quando ha varias seguidas.
                ImGui::TextColored(color, "%s", icon);
                ImGui::SameLine();
                ImGui::TextUnformatted(entry.message.c_str());
            }

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);

            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }


} // namespace axe