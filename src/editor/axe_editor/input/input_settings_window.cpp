#include "input_settings_window.hpp"
#include "axe/input/input.hpp"
#include "axe/log/log.hpp"
#include <cstring>

namespace axe
{
    // Mesma paleta de cores por "categoria" usada nos badges de Variables —
    // aqui reaproveitada por tipo de dispositivo, para leitura visual rápida.
    static const ImVec4 kDeviceCol[3] = {
        ImVec4(0.55f, 0.85f, 0.95f, 1), // Keyboard — azul claro
        ImVec4(0.95f, 0.75f, 0.35f, 1), // Mouse — laranja
        ImVec4(0.65f, 0.85f, 0.45f, 1), // Gamepad — verde
    };

    void InputSettingsWindow::Open()
    {
        m_Open = true;
    }

    void InputSettingsWindow::SetProjectPath(const std::filesystem::path& projectRoot)
    {
        m_ProjectRoot = projectRoot;
        m_ConfigPath = projectRoot / "InputConfig.json";
        InputMappingConfig::Get().Load(m_ConfigPath);
        m_SelectedActionIdx = m_SelectedAxisIdx = -1;
    }

    void InputSettingsWindow::SaveConfig()
    {
        if (m_ConfigPath.empty())
        {
            AXE_CORE_ERROR("Input Settings: nenhum projeto carregado, não é possível salvar.");
            return;
        }
        InputMappingConfig::Get().Save(m_ConfigPath);
    }

    // Captura: enquanto m_CapturingForAction/Axis estiver ativo, escuta a
    // próxima tecla/botão de mouse pressionado (leading edge, comparando
    // current vs previous para não disparar enquanto o usuário ainda segura
    // a tecla que abriu a captura) e grava no binding alvo.
    bool InputSettingsWindow::DrawCaptureOverlay()
    {
        if (!m_CapturingForAction && !m_CapturingForAxis) return false;

        ImGui::OpenPopup("##capture_input");
        ImGui::SetNextWindowSize(ImVec2(280, 100));
        bool captured = false;
        if (ImGui::BeginPopupModal("##capture_input", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
        {
            ImGui::TextWrapped("Pressione uma tecla ou botão do mouse...");
            ImGui::Spacing();
            ImGui::TextDisabled("Esc para cancelar");

            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                m_CapturingForAction = m_CapturingForAxis = false;
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return false;
            }

            // Teclado — varre o array de 512 codes do Input do AXE (GLFW-based),
            // detectando leading edge para não capturar a tecla repetidamente.
            const bool* cur = Input::GetCurrentKeys();
            const bool* prev = Input::GetPreviousKeys();
            int foundKey = -1;
            for (int i = 0; i < 512; i++)
                if (cur[i] && !prev[i]) { foundKey = i; break; }

            int mouseBtn = -1;
            for (int b = 0; b < 3; b++)
                if (ImGui::IsMouseClicked(b)) { mouseBtn = b; break; }

            if (foundKey >= 0 || mouseBtn >= 0)
            {
                InputBinding nb;
                if (foundKey >= 0)
                {
                    nb.Device = InputDevice::Keyboard;
                    nb.KeyboardKey = (Key)foundKey;
                }
                else
                {
                    nb.Device = InputDevice::Mouse;
                    nb.MouseBtn = (MouseButton)mouseBtn;
                }

                auto& cfg = InputMappingConfig::Get();
                if (m_CapturingForAction && m_CaptureTargetIdx >= 0 &&
                    m_CaptureTargetIdx < (int)cfg.GetActions().size())
                {
                    auto& bindings = cfg.GetActions()[m_CaptureTargetIdx].Bindings;
                    if (m_CaptureBindingIdx >= 0 && m_CaptureBindingIdx < (int)bindings.size())
                        bindings[m_CaptureBindingIdx] = nb;
                    else
                        bindings.push_back(nb);
                }
                else if (m_CapturingForAxis && m_CaptureTargetIdx >= 0 &&
                    m_CaptureTargetIdx < (int)cfg.GetAxes().size())
                {
                    auto& bindings = cfg.GetAxes()[m_CaptureTargetIdx].Bindings;
                    if (m_CaptureBindingIdx >= 0 && m_CaptureBindingIdx < (int)bindings.size())
                        bindings[m_CaptureBindingIdx] = nb;
                    else
                        bindings.push_back(nb);
                }

                m_CapturingForAction = m_CapturingForAxis = false;
                captured = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        return captured;
    }

    // Combo de tipo + campos relevantes para cada um. Retorna true se "x"
    // (remover) foi clicado — chamador remove depois do loop, mesmo padrão
    // usado para bindings/actions/axes.
    bool InputSettingsWindow::DrawTriggerRow(InputTrigger& trigger)
    {
        static const char* kTriggerNames[] = { "Pressed", "Released", "Held", "Tap", "Pulse" };
        int typeIdx = (int)trigger.Type;
        ImGui::SetNextItemWidth(110.f);
        if (ImGui::Combo("##trigtype", &typeIdx, kTriggerNames, 5))
            trigger.Type = (TriggerType)typeIdx;
        ImGui::SameLine();

        // Campo numérico específico do tipo escolhido — só um por vez.
        switch (trigger.Type)
        {
        case TriggerType::Held:
            ImGui::SetNextItemWidth(70.f);
            ImGui::DragFloat("##holdtime", &trigger.HoldTime, 0.05f, 0.05f, 10.f, "%.2fs");
            ImGui::SameLine();
            break;
        case TriggerType::Tap:
            ImGui::SetNextItemWidth(70.f);
            ImGui::DragFloat("##tapthresh", &trigger.TapThreshold, 0.02f, 0.02f, 2.f, "%.2fs");
            ImGui::SameLine();
            break;
        case TriggerType::Pulse:
            ImGui::SetNextItemWidth(70.f);
            ImGui::DragFloat("##pulseint", &trigger.PulseInterval, 0.02f, 0.02f, 5.f, "%.2fs");
            ImGui::SameLine();
            ImGui::Checkbox("On Start##pulse", &trigger.PulseTriggerOnStart);
            ImGui::SameLine();
            break;
        case TriggerType::Pressed:
        case TriggerType::Released:
            break; // sem parâmetros extras
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
        bool remove = ImGui::SmallButton("x##trig");
        ImGui::PopStyleColor(3);
        return remove;
    }

    bool InputSettingsWindow::DrawModifierRow(InputModifier& modifier)
    {
        static const char* kModifierNames[] = { "Negate", "Scale", "Dead Zone", "Swizzle" };
        int typeIdx = (int)modifier.Type;
        ImGui::SetNextItemWidth(110.f);
        if (ImGui::Combo("##modtype", &typeIdx, kModifierNames, 4))
            modifier.Type = (ModifierType)typeIdx;
        ImGui::SameLine();

        switch (modifier.Type)
        {
        case ModifierType::Negate:
            ImGui::Checkbox("X##negx", &modifier.NegateX); ImGui::SameLine();
            ImGui::Checkbox("Y##negy", &modifier.NegateY); ImGui::SameLine();
            ImGui::Checkbox("Z##negz", &modifier.NegateZ); ImGui::SameLine();
            break;
        case ModifierType::Scale:
            ImGui::SetNextItemWidth(70.f);
            ImGui::DragFloat("##scalefactor", &modifier.ScaleFactor, 0.05f, -10.f, 10.f, "%.2f");
            ImGui::SameLine();
            break;
        case ModifierType::DeadZone:
            ImGui::SetNextItemWidth(60.f);
            ImGui::DragFloat("##dzlower", &modifier.DeadZoneLower, 0.01f, 0.f, 1.f, "%.2f");
            ImGui::SameLine();
            ImGui::TextDisabled("-"); ImGui::SameLine();
            ImGui::SetNextItemWidth(60.f);
            ImGui::DragFloat("##dzupper", &modifier.DeadZoneUpper, 0.01f, 0.f, 1.f, "%.2f");
            ImGui::SameLine();
            break;
        case ModifierType::Swizzle:
        {
            static const char* kComponentNames[] = { "X", "Y", "Z" };
            ImGui::SetNextItemWidth(40.f);
            ImGui::Combo("##swizx", &modifier.SwizzleX, kComponentNames, 3); ImGui::SameLine();
            ImGui::SetNextItemWidth(40.f);
            ImGui::Combo("##swizy", &modifier.SwizzleY, kComponentNames, 3); ImGui::SameLine();
            ImGui::SetNextItemWidth(40.f);
            ImGui::Combo("##swizz", &modifier.SwizzleZ, kComponentNames, 3); ImGui::SameLine();
            break;
        }
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
        bool remove = ImGui::SmallButton("x##mod");
        ImGui::PopStyleColor(3);
        return remove;
    }

    // Retorna true se o "x" foi clicado — o chamador remove o binding DEPOIS
    // do loop terminar (evita invalidar o vetor/iteração no meio do desenho
    // dos demais binds, que continuariam referenciando índices deslocados).
    bool InputSettingsWindow::DrawBindingRow(std::vector<InputBinding>& bindings, int bindIdx,
        bool isAxis, int ownerIdx)
    {
        auto& b = bindings[bindIdx];
        ImGui::PushID(bindIdx);

        ImVec4 col = kDeviceCol[(int)b.Device];
        std::string label = b.ToDisplayString();

        // Pill com o nome do bind, mesmo padrão visual das badges de tipo
        ImVec2 textSz = ImGui::CalcTextSize(label.c_str());
        ImVec2 badgeSz = ImVec2(textSz.x + 16.f, 20.f);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, ImVec2(p0.x + badgeSz.x, p0.y + badgeSz.y),
            ImGui::ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, 0.20f)), 10.0f);
        dl->AddRect(p0, ImVec2(p0.x + badgeSz.x, p0.y + badgeSz.y),
            ImGui::ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, 0.85f)), 10.0f, 0, 1.2f);
        dl->AddText(ImVec2(p0.x + 8.f, p0.y + 2.f), ImGui::ColorConvertFloat4ToU32(col), label.c_str());
        ImGui::Dummy(badgeSz);
        ImGui::SameLine();

        // Scale — só relevante em Axis Mappings (ex.: W=+1, S=-1)
        if (isAxis)
        {
            ImGui::SetNextItemWidth(60.f);
            ImGui::DragFloat("##scale", &b.Scale, 0.1f, -10.f, 10.f, "%.1f");
            ImGui::SameLine();
        }

        if (ImGui::SmallButton("Rebind"))
        {
            if (isAxis) { m_CapturingForAxis = true; m_CapturingForAction = false; }
            else { m_CapturingForAction = true; m_CapturingForAxis = false; }
            m_CaptureTargetIdx = ownerIdx;
            m_CaptureBindingIdx = bindIdx;
        }
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
        bool removeClicked = ImGui::SmallButton("x");
        ImGui::PopStyleColor(3);

        // ── Triggers e Modifiers deste bind — um nível só, direto na tecla
        // (decisão de design: sem separação visual "padrão da Action" vs
        // "override por tecla", mesmo a estrutura de dados suportando ambos).
        ImGui::Indent(16.f);

        ImGui::TextDisabled("Triggers:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.3f, 0.42f, 1));
        if (ImGui::SmallButton("+##addtrig")) b.Triggers.push_back(InputTrigger{});
        ImGui::PopStyleColor();
        int removeTrigIdx = -1;
        for (int ti = 0; ti < (int)b.Triggers.size(); ti++)
        {
            ImGui::PushID(ti);
            if (DrawTriggerRow(b.Triggers[ti])) removeTrigIdx = ti;
            ImGui::PopID();
        }
        if (removeTrigIdx >= 0) b.Triggers.erase(b.Triggers.begin() + removeTrigIdx);

        ImGui::TextDisabled("Modifiers:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.3f, 0.42f, 1));
        if (ImGui::SmallButton("+##addmod")) b.Modifiers.push_back(InputModifier{});
        ImGui::PopStyleColor();
        int removeModIdx = -1;
        for (int mi = 0; mi < (int)b.Modifiers.size(); mi++)
        {
            ImGui::PushID(mi);
            if (DrawModifierRow(b.Modifiers[mi])) removeModIdx = mi;
            ImGui::PopID();
        }
        if (removeModIdx >= 0) b.Modifiers.erase(b.Modifiers.begin() + removeModIdx);

        ImGui::Unindent(16.f);

        ImGui::PopID();
        return removeClicked;
    }

    void InputSettingsWindow::DrawActionsSection()
    {
        auto& cfg = InputMappingConfig::Get();
        auto& actions = cfg.GetActions();

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        ImGui::InputTextWithHint("##newaction", "Nome da Action (ex: Jump)", m_NewActionName, sizeof(m_NewActionName));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.18f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.55f, 0.24f, 1));
        if (ImGui::Button("+ Action") && m_NewActionName[0] != 0)
        {
            cfg.AddAction(m_NewActionName);
            m_NewActionName[0] = 0;
        }
        ImGui::PopStyleColor(2);
        ImGui::Spacing();

        int removeIdx = -1;
        for (int i = 0; i < (int)actions.size(); i++)
        {
            auto& a = actions[i];
            ImGui::PushID(i);
            bool sel = (m_SelectedActionIdx == i);

            float cardWidth = ImGui::GetContentRegionAvail().x;
            float headerH = 28.f;
            ImVec2 cardMin = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 bg = sel
                ? ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.35f, 0.5f, 0.4f))
                : ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.04f));
            dl->AddRectFilled(cardMin, ImVec2(cardMin.x + cardWidth, cardMin.y + headerH), bg, 6.0f);

            ImGui::Dummy(ImVec2(8, headerH));
            ImGui::SameLine(0, 0);
            if (ImGui::Selectable(a.Name.c_str(), false, 0, ImVec2(cardWidth - 80.f, headerH)))
                m_SelectedActionIdx = sel ? -1 : i;

            // Resumo visível mesmo com o card recolhido.
            if (!a.Bindings.empty())
            {
                std::string summary = a.Bindings.size() == 1
                    ? a.Bindings[0].ToDisplayString()
                    : std::to_string(a.Bindings.size()) + " binds";
                ImVec2 sumSz = ImGui::CalcTextSize(summary.c_str());
                dl->AddText(ImVec2(cardMin.x + cardWidth - 145.f - sumSz.x, cardMin.y + 6.f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.6f, 0.6f, 0.6f, 1)), summary.c_str());
            }

            ImGui::SameLine(cardWidth - 100.f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.2f, 0.24f, 1));
            if (ImGui::SmallButton("+ Bind"))
            {
                m_CapturingForAction = true; m_CapturingForAxis = false;
                m_CaptureTargetIdx = i; m_CaptureBindingIdx = -1;
                m_SelectedActionIdx = i; // expande automaticamente para mostrar o resultado da captura
                sel = true;
            }
            ImGui::PopStyleColor();

            // "x" com posição ABSOLUTA a partir da borda direita do card — antes
            // usava SameLine() encadeado depois do "+ Bind", e como a largura
            // desse botão varia com a fonte/tema, o "x" podia acabar empurrado
            // para fora da janela (cortado), como reportado.
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 24.f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
            if (ImGui::SmallButton("x")) removeIdx = i;
            ImGui::PopStyleColor(3);

            ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMin.y + headerH + 2.f));

            if (sel)
            {
                ImGui::Indent(20.f);
                int removeBindIdx = -1;
                for (int bi = 0; bi < (int)a.Bindings.size(); bi++)
                {
                    if (DrawBindingRow(a.Bindings, bi, false, i)) removeBindIdx = bi;
                    ImGui::Spacing();
                }
                if (removeBindIdx >= 0)
                    a.Bindings.erase(a.Bindings.begin() + removeBindIdx);
                if (a.Bindings.empty())
                    ImGui::TextDisabled("Nenhum bind — clique em \"+ Bind\"");
                ImGui::Unindent(20.f);
                ImGui::Spacing();
            }
            ImGui::Dummy(ImVec2(0, 3));
            ImGui::PopID();
        }
        if (removeIdx >= 0)
        {
            cfg.RemoveAction(actions[removeIdx].Name);
            if (m_SelectedActionIdx == removeIdx) m_SelectedActionIdx = -1;
        }
    }

    void InputSettingsWindow::DrawAxesSection()
    {
        auto& cfg = InputMappingConfig::Get();
        auto& axes = cfg.GetAxes();

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        ImGui::InputTextWithHint("##newaxis", "Nome do Axis (ex: MoveForward)", m_NewAxisName, sizeof(m_NewAxisName));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.32f, 0.45f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.45f, 0.6f, 1));
        if (ImGui::Button("+ Axis") && m_NewAxisName[0] != 0)
        {
            cfg.AddAxis(m_NewAxisName);
            m_NewAxisName[0] = 0;
        }
        ImGui::PopStyleColor(2);
        ImGui::Spacing();

        int removeIdx = -1;
        for (int i = 0; i < (int)axes.size(); i++)
        {
            auto& a = axes[i];
            ImGui::PushID(i);
            bool sel = (m_SelectedAxisIdx == i);

            float cardWidth = ImGui::GetContentRegionAvail().x;
            float headerH = 28.f;
            ImVec2 cardMin = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 bg = sel
                ? ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.35f, 0.5f, 0.4f))
                : ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.04f));
            dl->AddRectFilled(cardMin, ImVec2(cardMin.x + cardWidth, cardMin.y + headerH), bg, 6.0f);

            ImGui::Dummy(ImVec2(8, headerH));
            ImGui::SameLine(0, 0);
            if (ImGui::Selectable(a.Name.c_str(), false, 0, ImVec2(cardWidth - 80.f, headerH)))
                m_SelectedAxisIdx = sel ? -1 : i;

            // Resumo visível mesmo com o card recolhido — sem isso, não dá para
            // saber se já existe algum bind sem expandir o card.
            if (!a.Bindings.empty())
            {
                std::string summary = a.Bindings.size() == 1
                    ? a.Bindings[0].ToDisplayString()
                    : std::to_string(a.Bindings.size()) + " binds";
                ImVec2 sumSz = ImGui::CalcTextSize(summary.c_str());
                dl->AddText(ImVec2(cardMin.x + cardWidth - 145.f - sumSz.x, cardMin.y + 6.f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.6f, 0.6f, 0.6f, 1)), summary.c_str());
            }

            ImGui::SameLine(cardWidth - 100.f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.2f, 0.24f, 1));
            if (ImGui::SmallButton("+ Bind"))
            {
                m_CapturingForAxis = true; m_CapturingForAction = false;
                m_CaptureTargetIdx = i; m_CaptureBindingIdx = -1;
                m_SelectedAxisIdx = i; // expande automaticamente para mostrar o resultado da captura
                sel = true;
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 24.f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.3f, 0.3f, 1));
            if (ImGui::SmallButton("x")) removeIdx = i;
            ImGui::PopStyleColor(3);

            ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMin.y + headerH + 2.f));

            if (sel)
            {
                ImGui::Indent(20.f);
                int removeBindIdx = -1;
                for (int bi = 0; bi < (int)a.Bindings.size(); bi++)
                {
                    if (DrawBindingRow(a.Bindings, bi, true, i)) removeBindIdx = bi;
                    ImGui::Spacing();
                }
                if (removeBindIdx >= 0)
                    a.Bindings.erase(a.Bindings.begin() + removeBindIdx);
                if (a.Bindings.empty())
                    ImGui::TextDisabled("Nenhum bind — clique em \"+ Bind\"");
                ImGui::Unindent(20.f);
                ImGui::Spacing();
            }
            ImGui::Dummy(ImVec2(0, 3));
            ImGui::PopID();
        }
        if (removeIdx >= 0)
        {
            cfg.RemoveAxis(axes[removeIdx].Name);
            if (m_SelectedAxisIdx == removeIdx) m_SelectedAxisIdx = -1;
        }
    }

    void InputSettingsWindow::Draw()
    {
        if (!m_Open) return;

        ImGui::SetNextWindowSize(ImVec2(560, 640), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Input Settings", &m_Open))
        {
            if (m_ConfigPath.empty())
            {
                ImGui::TextDisabled("Nenhum projeto carregado.");
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.35f, 0.55f, 1));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.5f, 0.75f, 1));
                if (ImGui::Button("Save")) SaveConfig();
                ImGui::PopStyleColor(2);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", m_ConfigPath.filename().string().c_str());
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::BeginTabBar("##inputtabs"))
                {
                    if (ImGui::BeginTabItem("Action Mappings"))
                    {
                        DrawActionsSection();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Axis Mappings"))
                    {
                        DrawAxesSection();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            }
        }
        ImGui::End();

        DrawCaptureOverlay();
    }

} // namespace axe