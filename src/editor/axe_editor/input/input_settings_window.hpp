#pragma once
#include "axe/input_mapping.hpp"
#include <imgui.h>
#include <string>

namespace axe
{
    // ── Input Settings — janela do editor para configurar Action/Axis Mappings.
    // Edita InputMappingConfig::Get() diretamente em memória; "Save" persiste
    // em InputConfig.json na raiz do projeto. Visual consistente com o painel
    // Script Members (cards arredondados, badges coloridos por tipo de bind).
    class InputSettingsWindow
    {
    public:
        void Open();
        bool IsOpen() const { return m_Open; }
        void Draw();

        // Chamado pelo EditorLayer ao abrir/trocar de projeto, para carregar
        // o InputConfig.json correto (cada projeto tem o seu).
        void SetProjectPath(const std::filesystem::path& projectRoot);

    private:
        bool m_Open = false;
        std::filesystem::path m_ProjectRoot;
        std::filesystem::path m_ConfigPath; // m_ProjectRoot / "InputConfig.json"

        char m_NewActionName[64] = {};
        char m_NewAxisName[64] = {};

        int  m_SelectedActionIdx = -1;
        int  m_SelectedAxisIdx = -1;

        // Captura de tecla: quando != -1, o próximo InputBinding recebido vai
        // para essa Action/Axis (índice indica qual binding está sendo
        // "re-capturado", ou size() do vetor para um binding novo).
        bool m_CapturingForAction = false;
        bool m_CapturingForAxis = false;
        int  m_CaptureTargetIdx = -1;   // índice da Action/Axis sendo editada
        int  m_CaptureBindingIdx = -1;  // índice do binding (-1 = adicionar novo)

        void DrawActionsSection();
        void DrawAxesSection();
        bool DrawBindingRow(std::vector<InputBinding>& bindings, int bindIdx,
            bool isAxis, int ownerIdx);
        bool DrawTriggerRow(InputTrigger& trigger);   // true = remoção pedida
        bool DrawModifierRow(InputModifier& modifier); // true = remoção pedida
        bool DrawCaptureOverlay(); // retorna true se capturou algo neste frame
        void SaveConfig();
    };

} // namespace axe