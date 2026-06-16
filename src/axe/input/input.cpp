#include <string>
#include "axe/input/input.hpp"
#include "axe/axe_window/window.hpp"
#include "axe/log/log.hpp"
#include <cstring>

namespace axe
{
    Window* Input::s_Window = nullptr;
    bool    Input::s_CurrentKeys[512] = {};
    bool    Input::s_PreviousKeys[512] = {};
    bool    Input::s_CurrentMouse[8] = {};
    bool    Input::s_PreviousMouse[8] = {};

    void Input::Init(Window* window)
    {
        s_Window = window;
        std::memset(s_CurrentKeys, 0, sizeof(s_CurrentKeys));
        std::memset(s_PreviousKeys, 0, sizeof(s_PreviousKeys));
        std::memset(s_CurrentMouse, 0, sizeof(s_CurrentMouse));
        std::memset(s_PreviousMouse, 0, sizeof(s_PreviousMouse));
        AXE_CORE_INFO("Input: inicializado.");
    }

    void Input::Update()
    {
        if (!s_Window) return;

        std::memcpy(s_PreviousKeys, s_CurrentKeys, sizeof(s_CurrentKeys));
        std::memcpy(s_PreviousMouse, s_CurrentMouse, sizeof(s_CurrentMouse));

        // Key enum usa valores GLFW (0–511)
        for (int i = 0; i < 512; i++)
            s_CurrentKeys[i] = s_Window->IsKeyDown(i);

        // Debug: loga WASD toda vez que algum estiver pressionado
        bool w = s_CurrentKeys[(int)Key::W];
        bool a = s_CurrentKeys[(int)Key::A];
        bool s = s_CurrentKeys[(int)Key::S];
        bool d = s_CurrentKeys[(int)Key::D];
        if (w || a || s || d)
            AXE_CORE_INFO("Input::Update WASD: W={} A={} S={} D={}", w, a, s, d);
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
            if (v != 0.0f) AXE_CORE_INFO("Input::GetAxis Horizontal={:.1f} D={} A={}", v, GetKey(Key::D), GetKey(Key::A));
            return v;
        }
        if (axisName == "Vertical")
        {
            float v = 0.0f;
            if (GetKey(Key::W)) v += 1.0f;
            if (GetKey(Key::S)) v -= 1.0f;
            if (v != 0.0f) AXE_CORE_INFO("Input::GetAxis Vertical={:.1f} W={} S={}", v, GetKey(Key::W), GetKey(Key::S));
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

} // namespace axe