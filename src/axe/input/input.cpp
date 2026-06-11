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

} // namespace axe