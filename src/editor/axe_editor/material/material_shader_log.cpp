// material_shader_log.cpp
// Log de compilação do shader exibido na janela "Shader Log" (info/warning/
// error), com auto-scroll.

#include "material_editor_window.hpp"
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

    void MaterialEditorWindow::DrawShaderLog()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));

        if (ImGui::Begin("Shader Log", nullptr, ImGuiWindowFlags_NoScrollbar))
        {
            // Botão limpar
            if (ImGui::SmallButton("Limpar"))
                ClearLog();

            ImGui::SameLine();
            ImGui::TextDisabled("%d messagens", (int)m_ShaderLog.size());

            ImGui::Separator();
            //Lista de mensagens com scroll
            ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false,
                ImGuiWindowFlags_HorizontalScrollbar);

            for (auto& entry : m_ShaderLog)
            {
                ImVec4 color;
                const char* prefix;

                switch (entry.level)
                {
                case ShaderLogEntry::Level::Info:
                    color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                    prefix = "[INFO]";
                    break;
                case ShaderLogEntry::Level::Warnning:
                    color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
                    prefix = "[WARN]";
                    break;
                case ShaderLogEntry::Level::Error:
                    color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                    prefix = "[ERR]";
                    break;
                }

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted((prefix + entry.message).c_str());
                ImGui::PopStyleColor();
            }

            //Auto-Scroll para o final
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);

            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar();

    }


} // namespace axe