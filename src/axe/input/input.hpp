#pragma once
#include "axe/core/types.hpp"
#include "axe/input/key_codes.hpp"
#include "axe/utils/glm_config.hpp"
#include <string>
#include <unordered_map>

// Forward declaration — evita incluir window.hpp no header gerado
namespace axe { class Window; }

namespace axe
{
    // ── Estado de avaliação de um Trigger (replica o conceito da Unreal) ──────
    // None      : condição do trigger não está sendo satisfeita.
    // Started   : primeiro frame em que a entrada começou a ser avaliada
    //             (ex.: tecla de um Held acabou de ser pressionada, mas o
    //             HoldTime ainda não foi atingido).
    // Ongoing   : continua em avaliação ao longo de múltiplos frames (ex.:
    //             Held contando o tempo, ainda não disparou).
    // Triggered : a condição foi satisfeita NESTE frame — este é o "disparou
    //             de verdade", equivalente ao bool simples que outros engines
    //             expõem.
    // Completed : trigger encerrado depois de ter disparado (ex.: soltou a
    //             tecla de um Held após o Triggered já ter ocorrido).
    enum class ETriggerState : int
    {
        None = 0,
        Started = 1,
        Ongoing = 2,
        Triggered = 3,
        Completed = 4,
    };

    // ── Input — singleton estático acessível de qualquer DLL de script ────────
    // Inicializado pelo editor/jogo com um ponteiro para a Window ativa.
    // Os scripts usam: axe::Input::GetKey(axe::Key::W)
    class AXE_API Input
    {
    public:
        // Chamado uma vez na inicialização do editor/jogo
        static void Init(Window* window);

        // Chamado uma vez por frame para capturar o estado atual
        // (diferencia Pressed/Released de Held). deltaTime é necessário para
        // avaliar Triggers com componente de tempo (Held/Tap/Pulse) — ver
        // GetActionState/GetAxisValue1D etc. Default 0 mantém compatibilidade
        // com qualquer chamador antigo que não passe o parâmetro (mas nesse
        // caso os Triggers baseados em tempo não avançam corretamente).
        static void Update(float deltaTime = 0.0f);
        // Só relê o estado atual das teclas sem avançar o histórico (previous)
        // Usado para sincronizar DLLs de script que têm instância estática separada
        static void RefreshCurrent();
        static const bool* GetCurrentKeys() { return s_CurrentKeys; }
        static const bool* GetPreviousKeys() { return s_PreviousKeys; }

        // ── API usada pelo código gerado ──────────────────────────────────────

        // Tecla mantida pressionada este frame
        static bool GetKey(Key key);
        static int  GetKeyCode(const char* name);  // "Space" -> 32, "W" -> 87

        // Tecla foi pressionada NESTE frame (leading edge)
        static bool GetKeyDown(Key key);

        // Tecla foi solta NESTE frame (trailing edge)
        static bool GetKeyUp(Key key);

        // Eixo virtual legado: "Horizontal" → A/D → [-1, 1]
        //                       "Vertical"   → S/W → [-1, 1]
        // Mantido por compatibilidade — prefira GetAxisValue1D com o novo
        // sistema de Axis Mappings (InputMappingConfig) para eixos configuráveis.
        static float GetAxis(const std::string& axisName);

        // ── Action/Axis Mappings (InputMappingConfig) ──────────────────────────
        // Estado agregado: combina todos os binds da Action, retornando o
        // "melhor" status entre eles (prioridade Triggered > Ongoing > Started
        // > Completed > None). Use quando não precisar diferenciar qual
        // dispositivo/tecla específico disparou.
        static ETriggerState GetActionState(const std::string& actionName);
        static bool GetActionTriggered(const std::string& actionName) {
            return GetActionState(actionName) == ETriggerState::Triggered;
        }

        // Estado de UM bind específico dentro da Action (índice na lista de
        // Bindings, na ordem configurada no Input Settings) — use quando for
        // necessário diferenciar, por exemplo, Space de Gamepad A na mesma Action.
        static ETriggerState GetActionBindState(const std::string& actionName, int bindIndex);

        // Valor de um Axis Mapping já com Modifiers aplicados (Negate, Scale,
        // Dead Zone, Swizzle) e somado entre todos os binds ativos. Use a
        // variante correspondente ao AxisValueType configurado no Input
        // Settings (1D/2D/3D) — chamar a variante errada retorna só o primeiro
        // componente relevante (sem crash, mas sem sentido semântico).
        static float     GetAxisValue1D(const std::string& axisName);
        static glm::vec2 GetAxisValue2D(const std::string& axisName);
        static glm::vec3 GetAxisValue3D(const std::string& axisName);

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

        static float       s_DeltaTime;

        // ── Estado runtime por-binding para Triggers com componente de tempo ──
        // Chave: "ActionName#bindIndex" (ou "AxisName#bindIndex"). Não persiste
        // em disco — é puramente de execução, reconstruído a cada sessão.
        struct TriggerRuntimeState
        {
            float TimeHeld = 0.0f;       // segundos consecutivos pressionado
            float TimeSincePulse = 0.0f; // segundos desde o último Pulse disparado
            bool  WasDown = false;       // estado no frame anterior (para edges)
            bool  PulsedOnce = false;    // já disparou o Pulse inicial (PulseTriggerOnStart)
        };
        static std::unordered_map<std::string, TriggerRuntimeState> s_TriggerStates;

        // Lê se o dispositivo físico de um InputBinding está pressionado AGORA
        // (sem considerar Triggers ainda — leitura crua do hardware).
        static bool IsBindingPhysicallyDown(const struct InputBinding& binding);

        // Avalia a lista de Triggers de um binding (ou usa Pressed implícito
        // se a lista estiver vazia) e atualiza o estado runtime associado.
        static ETriggerState EvaluateBindingTriggers(const std::string& stateKey,
            const struct InputBinding& binding, bool physicallyDown);

        // Aplica a lista de Modifiers de um binding sobre um valor float bruto,
        // retornando o vetor resultante (componentes não usados ficam em 0).
        static glm::vec3 ApplyModifiers(const struct InputBinding& binding, float rawValue);
    };

} // namespace axe