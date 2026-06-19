#include <string>
#include "axe/input/input.hpp"
#include "axe/input_mapping.hpp"
#include "axe/axe_window/window.hpp"
#include "axe/log/log.hpp"
#include <cstring>
#include <algorithm>

namespace axe
{
    Window* Input::s_Window = nullptr;
    bool    Input::s_CurrentKeys[512] = {};
    bool    Input::s_PreviousKeys[512] = {};
    bool    Input::s_CurrentMouse[8] = {};
    bool    Input::s_PreviousMouse[8] = {};
    float   Input::s_DeltaTime = 0.0f;
    std::unordered_map<std::string, Input::TriggerRuntimeState> Input::s_TriggerStates;

    void Input::Init(Window* window)
    {
        s_Window = window;
        std::memset(s_CurrentKeys, 0, sizeof(s_CurrentKeys));
        std::memset(s_PreviousKeys, 0, sizeof(s_PreviousKeys));
        std::memset(s_CurrentMouse, 0, sizeof(s_CurrentMouse));
        std::memset(s_PreviousMouse, 0, sizeof(s_PreviousMouse));
        AXE_CORE_INFO("Input: inicializado.");
    }

    void Input::Update(float deltaTime)
    {
        if (!s_Window) return;

        s_DeltaTime = deltaTime;

        std::memcpy(s_PreviousKeys, s_CurrentKeys, sizeof(s_CurrentKeys));
        std::memcpy(s_PreviousMouse, s_CurrentMouse, sizeof(s_CurrentMouse));

        // Key enum usa valores GLFW (0–511)
        for (int i = 0; i < 512; i++)
            s_CurrentKeys[i] = s_Window->IsKeyDown(i);
    }

    int Input::GetKeyCode(const char* name)
    {
        if (!name || name[0] == 0) return 0;
        // Single letter
        if (name[0] >= 'A' && name[0] <= 'Z' && name[1] == 0) return (int)name[0];
        if (name[0] >= 'a' && name[0] <= 'z' && name[1] == 0) return (int)(name[0] - 'a' + 'A');

        // Named keys
        if (strcmp(name, "Space") == 0) return 32;
        if (strcmp(name, "Enter") == 0) return 257;
        if (strcmp(name, "Escape") == 0) return 256;
        if (strcmp(name, "Tab") == 0) return 258;
        if (strcmp(name, "Backspace") == 0) return 259;
        if (strcmp(name, "Delete") == 0) return 261;
        if (strcmp(name, "Insert") == 0) return 260;
        if (strcmp(name, "Home") == 0) return 268;
        if (strcmp(name, "End") == 0) return 269;
        if (strcmp(name, "PageUp") == 0) return 266;
        if (strcmp(name, "PageDown") == 0) return 267;
        if (strcmp(name, "Right") == 0) return 262;
        if (strcmp(name, "Left") == 0) return 263;
        if (strcmp(name, "Down") == 0) return 264;
        if (strcmp(name, "Up") == 0) return 265;
        if (strcmp(name, "LeftShift") == 0) return 340;
        if (strcmp(name, "RightShift") == 0) return 344;
        if (strcmp(name, "LeftControl") == 0) return 341;
        if (strcmp(name, "RightControl") == 0) return 345;
        if (strcmp(name, "LeftAlt") == 0) return 342;
        if (strcmp(name, "RightAlt") == 0) return 346;
        if (strcmp(name, "F1") == 0) return 290; if (name == "F2")  return 291;
        if (strcmp(name, "F3") == 0) return 292; if (name == "F4")  return 293;
        if (strcmp(name, "F5") == 0) return 294; if (name == "F6")  return 295;
        if (strcmp(name, "F7") == 0) return 296; if (name == "F8")  return 297;
        if (strcmp(name, "F9") == 0) return 298; if (name == "F10") return 299;
        if (strcmp(name, "F11") == 0) return 300; if (name == "F12") return 301;

        // Fallback — checa se é número puro
        bool isNum = (name[0] != 0);
        for (int i = 0; name[i]; i++) if (name[i] < '0' || name[i] > '9') { isNum = false; break; }
        if (isNum) return std::atoi(name);
        return 0;
    }

    bool Input::GetKey(Key key)
    {
        int k = static_cast<int>(key);
        if (k < 0 || k >= 512) return false;
        return s_CurrentKeys[k];
    }

    bool Input::GetKeyDown(Key key)
    {
        int k = static_cast<int>(key);
        if (k < 0 || k >= 512) return false;
        return s_CurrentKeys[k] && !s_PreviousKeys[k];
    }

    bool Input::GetKeyUp(Key key)
    {
        int k = static_cast<int>(key);
        if (k < 0 || k >= 512) return false;
        return !s_CurrentKeys[k] && s_PreviousKeys[k];
    }

    void Input::RefreshCurrent()
    {
        if (!s_Window) return;
        // Só relê o estado atual — NÃO avança s_PreviousKeys
        // Isso sincroniza o s_CurrentKeys da DLL sem quebrar GetKeyDown
        for (int i = 0; i < 512; i++)
            s_CurrentKeys[i] = s_Window->IsKeyDown(i);
    }

    float Input::GetAxis(const std::string& axisName)
    {
        if (axisName == "Horizontal")
        {
            float v = 0.0f;
            if (GetKey(Key::D)) v += 1.0f;
            if (GetKey(Key::A)) v -= 1.0f;
            return v;
        }
        if (axisName == "Vertical")
        {
            float v = 0.0f;
            if (GetKey(Key::W)) v += 1.0f;
            if (GetKey(Key::S)) v -= 1.0f;
            return v;
        }
        return 0.0f;
    }

    glm::vec2 Input::GetMousePosition()
    {
        if (!s_Window) return {};
        return s_Window->GetCursorPosition();
    }

    bool Input::GetMouseButton(int button)
    {
        if (button < 0 || button >= 8) return false;
        return s_CurrentMouse[button];
    }

    // ── Leitura física crua de um binding (sem considerar Triggers ainda) ──────
    bool Input::IsBindingPhysicallyDown(const InputBinding& binding)
    {
        switch (binding.Device)
        {
        case InputDevice::Keyboard:
            return GetKey(binding.KeyboardKey);
        case InputDevice::Mouse:
            return GetMouseButton((int)binding.MouseBtn);
        case InputDevice::Gamepad:
            // Leitura de hardware de gamepad ainda não implementada — ver
            // comentário em input_mapping.hpp sobre GamepadButton/Axis.
            return false;
        }
        return false;
    }

    // ── Máquina de estados de Triggers para UM binding ──────────────────────
    // Se a lista de Triggers estiver vazia, comporta-se como um único trigger
    // "Pressed" implícito (igual a Unreal: sem trigger explícito, dispara no
    // leading edge do dispositivo).
    ETriggerState Input::EvaluateBindingTriggers(const std::string& stateKey,
        const InputBinding& binding, bool physicallyDown)
    {
        auto& st = s_TriggerStates[stateKey]; // cria com defaults se não existir

        bool wasDown = st.WasDown;
        st.WasDown = physicallyDown;

        if (binding.Triggers.empty())
        {
            // Pressed implícito
            ETriggerState result = (physicallyDown && !wasDown) ? ETriggerState::Triggered
                : (physicallyDown ? ETriggerState::Ongoing : ETriggerState::None);
            return result;
        }

        // Com múltiplos Triggers configurados no MESMO binding, o resultado é
        // o "melhor" estado entre eles (mesma prioridade usada para agregar
        // binds diferentes — ver GetActionState), avaliados com o MESMO
        // estado runtime (TimeHeld/TimeSincePulse) compartilhado entre todos
        // — replica o comportamento da Unreal, onde múltiplos triggers no
        // mesmo mapping leem o mesmo histórico de tempo da tecla física.
        ETriggerState best = ETriggerState::None;
        auto rank = [](ETriggerState s) {
            switch (s) {
            case ETriggerState::Triggered: return 4;
            case ETriggerState::Ongoing:   return 3;
            case ETriggerState::Started:   return 2;
            case ETriggerState::Completed: return 1;
            default: return 0;
            }
            };

        if (physicallyDown) st.TimeHeld += s_DeltaTime;
        else st.TimeHeld = 0.0f;

        for (auto& trig : binding.Triggers)
        {
            ETriggerState result = ETriggerState::None;
            switch (trig.Type)
            {
            case TriggerType::Pressed:
                result = (physicallyDown && !wasDown) ? ETriggerState::Triggered : ETriggerState::None;
                break;

            case TriggerType::Released:
                result = (!physicallyDown && wasDown) ? ETriggerState::Triggered : ETriggerState::None;
                break;

            case TriggerType::Held:
                if (physicallyDown)
                    result = (st.TimeHeld >= trig.HoldTime) ? ETriggerState::Triggered : ETriggerState::Ongoing;
                else
                    result = wasDown ? ETriggerState::Completed : ETriggerState::None;
                break;

            case TriggerType::Tap:
                if (physicallyDown)
                    result = ETriggerState::Ongoing; // ainda não sabemos se vai ser um tap
                else if (wasDown)
                    // Soltou agora — disparou se o tempo total pressionado
                    // ficou dentro do limite do tap.
                    result = (st.TimeHeld <= trig.TapThreshold) ? ETriggerState::Triggered : ETriggerState::None;
                else
                    result = ETriggerState::None;
                break;

            case TriggerType::Pulse:
                if (physicallyDown)
                {
                    st.TimeSincePulse += s_DeltaTime;
                    bool firstFramePressed = !wasDown;
                    if (firstFramePressed)
                    {
                        st.TimeSincePulse = 0.0f;
                        st.PulsedOnce = false;
                        result = trig.PulseTriggerOnStart ? ETriggerState::Triggered : ETriggerState::Started;
                        if (trig.PulseTriggerOnStart) st.PulsedOnce = true;
                    }
                    else if (st.TimeSincePulse >= trig.PulseInterval)
                    {
                        st.TimeSincePulse = 0.0f;
                        result = ETriggerState::Triggered;
                    }
                    else
                    {
                        result = ETriggerState::Ongoing;
                    }
                }
                else
                {
                    result = wasDown ? ETriggerState::Completed : ETriggerState::None;
                }
                break;
            }
            if (rank(result) > rank(best)) best = result;
        }

        return best;
    }

    // ── Aplicação de Modifiers sobre um valor bruto (1 componente) ──────────
    // Aplicados em ordem na lista, cada um operando sobre o resultado do
    // anterior. O valor de entrada/saída é tratado como vec3 mesmo para Axis1D
    // (componentes extras simplesmente ficam 0 e são ignorados pelo chamador).
    glm::vec3 Input::ApplyModifiers(const InputBinding& binding, float rawValue)
    {
        glm::vec3 v(rawValue * binding.Scale, 0.0f, 0.0f);

        for (auto& mod : binding.Modifiers)
        {
            switch (mod.Type)
            {
            case ModifierType::Negate:
                if (mod.NegateX) v.x = -v.x;
                if (mod.NegateY) v.y = -v.y;
                if (mod.NegateZ) v.z = -v.z;
                break;

            case ModifierType::Scale:
                v *= mod.ScaleFactor;
                break;

            case ModifierType::DeadZone:
            {
                auto applyDz = [&](float c) {
                    float mag = std::abs(c);
                    if (mag <= mod.DeadZoneLower) return 0.0f;
                    float t = (mag - mod.DeadZoneLower) / std::max(0.0001f, mod.DeadZoneUpper - mod.DeadZoneLower);
                    t = std::min(t, 1.0f);
                    return (c < 0.0f ? -1.0f : 1.0f) * t;
                    };
                v.x = applyDz(v.x); v.y = applyDz(v.y); v.z = applyDz(v.z);
                break;
            }

            case ModifierType::Swizzle:
            {
                glm::vec3 src = v;
                float* comps[3] = { &src.x, &src.y, &src.z };
                v.x = *comps[std::clamp(mod.SwizzleX, 0, 2)];
                v.y = *comps[std::clamp(mod.SwizzleY, 0, 2)];
                v.z = *comps[std::clamp(mod.SwizzleZ, 0, 2)];
                break;
            }
            }
        }
        return v;
    }

    ETriggerState Input::GetActionState(const std::string& actionName)
    {
        auto* action = InputMappingConfig::Get().FindAction(actionName);
        if (!action) return ETriggerState::None;

        auto rank = [](ETriggerState s) {
            switch (s) {
            case ETriggerState::Triggered: return 4;
            case ETriggerState::Ongoing:   return 3;
            case ETriggerState::Started:   return 2;
            case ETriggerState::Completed: return 1;
            default: return 0;
            }
            };

        ETriggerState best = ETriggerState::None;
        for (int bi = 0; bi < (int)action->Bindings.size(); bi++)
        {
            ETriggerState s = GetActionBindState(actionName, bi);
            if (rank(s) > rank(best)) best = s;
        }
        return best;
    }

    ETriggerState Input::GetActionBindState(const std::string& actionName, int bindIndex)
    {
        auto* action = InputMappingConfig::Get().FindAction(actionName);
        if (!action || bindIndex < 0 || bindIndex >= (int)action->Bindings.size())
            return ETriggerState::None;

        auto& binding = action->Bindings[bindIndex];
        bool down = IsBindingPhysicallyDown(binding);
        std::string key = actionName + "#" + std::to_string(bindIndex);
        return EvaluateBindingTriggers(key, binding, down);
    }

    float Input::GetAxisValue1D(const std::string& axisName)
    {
        auto* axis = InputMappingConfig::Get().FindAxis(axisName);
        if (!axis) return 0.0f;

        float total = 0.0f;
        for (auto& binding : axis->Bindings)
        {
            bool down = IsBindingPhysicallyDown(binding);
            float raw = down ? 1.0f : 0.0f;
            total += ApplyModifiers(binding, raw).x;
        }
        return total;
    }

    glm::vec2 Input::GetAxisValue2D(const std::string& axisName)
    {
        auto* axis = InputMappingConfig::Get().FindAxis(axisName);
        if (!axis) return glm::vec2(0.0f);

        glm::vec3 total(0.0f);
        for (auto& binding : axis->Bindings)
        {
            bool down = IsBindingPhysicallyDown(binding);
            float raw = down ? 1.0f : 0.0f;
            total += ApplyModifiers(binding, raw);
        }
        return glm::vec2(total.x, total.y);
    }

    glm::vec3 Input::GetAxisValue3D(const std::string& axisName)
    {
        auto* axis = InputMappingConfig::Get().FindAxis(axisName);
        if (!axis) return glm::vec3(0.0f);

        glm::vec3 total(0.0f);
        for (auto& binding : axis->Bindings)
        {
            bool down = IsBindingPhysicallyDown(binding);
            float raw = down ? 1.0f : 0.0f;
            total += ApplyModifiers(binding, raw);
        }
        return total;
    }

} // namespace axe