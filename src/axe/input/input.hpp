#pragma once
#include "axe/core/types.hpp"
#include "axe/input/key_codes.hpp"
#include "axe/utils/glm_config.hpp"
#include <string>

// Forward declaration — evita incluir window.hpp no header gerado
namespace axe { class Window; }

namespace axe
{
    // ── Input — singleton estático acessível de qualquer DLL de script ────────
    // Inicializado pelo editor/jogo com um ponteiro para a Window ativa.
    // Os scripts usam: axe::Input::GetKey(axe::Key::W)
    class AXE_API Input
    {
    public:
        // Chamado uma vez na inicialização do editor/jogo
        static void Init(Window* window);

        // Chamado uma vez por frame para capturar o estado atual
        // (diferencia Pressed/Released de Held)
        static void Update();
        // Só relê o estado atual das teclas sem avançar o histórico (previous)
        // Usado para sincronizar DLLs de script que têm instância estática separada
        static void RefreshCurrent();
        static const bool* GetCurrentKeys() { return s_CurrentKeys; }
        static const bool* GetPreviousKeys() { return s_PreviousKeys; }

        // ── API usada pelo código gerado ──────────────────────────────────────

        // Tecla mantida pressionada este frame
        static bool GetKey(Key key);

        // Tecla foi pressionada NESTE frame (leading edge)
        static bool GetKeyDown(Key key);

        // Tecla foi solta NESTE frame (trailing edge)
        static bool GetKeyUp(Key key);

        // Eixo virtual: "Horizontal" → A/D → [-1, 1]
        //               "Vertical"   → S/W → [-1, 1]
        static float GetAxis(const std::string& axisName);

        // Mouse
        static glm::vec2 GetMousePosition();
        static bool GetMouseButton(int button);  // 0=esquerdo, 1=direito, 2=meio

    private:
        static Window* s_Window;

        // Estado atual e anterior (para detectar pressed/released)
        static bool       s_CurrentKeys[512];
        static bool       s_PreviousKeys[512];

        static bool       s_CurrentMouse[8];
        static bool       s_PreviousMouse[8];
    };

} // namespace axe