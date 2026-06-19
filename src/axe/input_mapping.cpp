#include "input_mapping.hpp"
#include "axe/log/log.hpp"
#include <fstream>
#include <algorithm>
#include <sstream>

namespace axe
{
    // ── Key <-> nome legível ────────────────────────────────────────────────
    static const char* KeyToDisplayName(Key k)
    {
        switch (k)
        {
        case Key::Space: return "Space";
        case Key::Enter: return "Enter";
        case Key::Backspace: return "Backspace";
        case Key::Tab: return "Tab";
        case Key::Escape: return "Escape";
        case Key::Delete: return "Delete";
        case Key::Insert: return "Insert";
        case Key::Home: return "Home";
        case Key::End: return "End";
        case Key::PageUp: return "Page Up";
        case Key::PageDown: return "Page Down";
        case Key::Right: return "Right Arrow";
        case Key::Left: return "Left Arrow";
        case Key::Down: return "Down Arrow";
        case Key::Up: return "Up Arrow";
        case Key::LeftShift: return "Left Shift";
        case Key::RightShift: return "Right Shift";
        case Key::LeftControl: return "Left Ctrl";
        case Key::RightControl: return "Right Ctrl";
        case Key::LeftAlt: return "Left Alt";
        case Key::RightAlt: return "Right Alt";
        case Key::LeftSuper: return "Left Super";
        case Key::F1: return "F1"; case Key::F2: return "F2";
        case Key::F3: return "F3"; case Key::F4: return "F4";
        case Key::F5: return "F5"; case Key::F6: return "F6";
        case Key::F7: return "F7"; case Key::F8: return "F8";
        case Key::F9: return "F9"; case Key::F10: return "F10";
        case Key::F11: return "F11"; case Key::F12: return "F12";
        case Key::Num0: return "0"; case Key::Num1: return "1";
        case Key::Num2: return "2"; case Key::Num3: return "3";
        case Key::Num4: return "4"; case Key::Num5: return "5";
        case Key::Num6: return "6"; case Key::Num7: return "7";
        case Key::Num8: return "8"; case Key::Num9: return "9";
        default: break;
        }
        int v = (int)k;
        if (v >= (int)'A' && v <= (int)'Z')
        {
            static char buf[2] = { 0, 0 };
            buf[0] = (char)v;
            return buf;
        }
        return "?";
    }

    static const char* GamepadButtonToDisplayName(GamepadButton b)
    {
        switch (b)
        {
        case GamepadButton::A: return "A";
        case GamepadButton::B: return "B";
        case GamepadButton::X: return "X";
        case GamepadButton::Y: return "Y";
        case GamepadButton::LeftBumper: return "LB";
        case GamepadButton::RightBumper: return "RB";
        case GamepadButton::Back: return "Back";
        case GamepadButton::Start: return "Start";
        case GamepadButton::LeftStick: return "L Stick";
        case GamepadButton::RightStick: return "R Stick";
        case GamepadButton::DPadUp: return "D-Pad Up";
        case GamepadButton::DPadRight: return "D-Pad Right";
        case GamepadButton::DPadDown: return "D-Pad Down";
        case GamepadButton::DPadLeft: return "D-Pad Left";
        }
        return "?";
    }

    // ── InputTrigger ─────────────────────────────────────────────────────────
    std::string InputTrigger::ToDisplayString() const
    {
        std::ostringstream ss;
        switch (Type)
        {
        case TriggerType::Pressed:  ss << "Pressed"; break;
        case TriggerType::Released: ss << "Released"; break;
        case TriggerType::Held:     ss << "Held (" << HoldTime << "s)"; break;
        case TriggerType::Tap:      ss << "Tap (" << TapThreshold << "s)"; break;
        case TriggerType::Pulse:    ss << "Pulse (" << PulseInterval << "s)"; break;
        }
        return ss.str();
    }

    nlohmann::json InputTrigger::Serialize() const
    {
        nlohmann::json j;
        j["type"] = (int)Type;
        j["holdTime"] = HoldTime;
        j["tapThreshold"] = TapThreshold;
        j["pulseInterval"] = PulseInterval;
        j["pulseOnStart"] = PulseTriggerOnStart;
        return j;
    }

    void InputTrigger::Deserialize(const nlohmann::json& j)
    {
        Type = (TriggerType)j.value("type", 0);
        HoldTime = j.value("holdTime", 0.5f);
        TapThreshold = j.value("tapThreshold", 0.2f);
        PulseInterval = j.value("pulseInterval", 0.1f);
        PulseTriggerOnStart = j.value("pulseOnStart", true);
    }

    // ── InputModifier ────────────────────────────────────────────────────────
    std::string InputModifier::ToDisplayString() const
    {
        std::ostringstream ss;
        switch (Type)
        {
        case ModifierType::Negate:
            ss << "Negate (";
            if (NegateX) ss << "X";
            if (NegateY) ss << "Y";
            if (NegateZ) ss << "Z";
            ss << ")";
            break;
        case ModifierType::Scale:
            ss << "Scale x" << ScaleFactor;
            break;
        case ModifierType::DeadZone:
            ss << "Dead Zone [" << DeadZoneLower << ", " << DeadZoneUpper << "]";
            break;
        case ModifierType::Swizzle:
            ss << "Swizzle";
            break;
        }
        return ss.str();
    }

    nlohmann::json InputModifier::Serialize() const
    {
        nlohmann::json j;
        j["type"] = (int)Type;
        j["negateX"] = NegateX; j["negateY"] = NegateY; j["negateZ"] = NegateZ;
        j["scaleFactor"] = ScaleFactor;
        j["deadZoneLower"] = DeadZoneLower; j["deadZoneUpper"] = DeadZoneUpper;
        j["swizzleX"] = SwizzleX; j["swizzleY"] = SwizzleY; j["swizzleZ"] = SwizzleZ;
        return j;
    }

    void InputModifier::Deserialize(const nlohmann::json& j)
    {
        Type = (ModifierType)j.value("type", 0);
        NegateX = j.value("negateX", true);
        NegateY = j.value("negateY", true);
        NegateZ = j.value("negateZ", true);
        ScaleFactor = j.value("scaleFactor", 1.0f);
        DeadZoneLower = j.value("deadZoneLower", 0.2f);
        DeadZoneUpper = j.value("deadZoneUpper", 1.0f);
        SwizzleX = j.value("swizzleX", 1);
        SwizzleY = j.value("swizzleY", 0);
        SwizzleZ = j.value("swizzleZ", 2);
    }

    // ── InputBinding ─────────────────────────────────────────────────────────
    std::string InputBinding::ToDisplayString() const
    {
        switch (Device)
        {
        case InputDevice::Keyboard: return KeyToDisplayName(KeyboardKey);
        case InputDevice::Mouse:
            switch (MouseBtn)
            {
            case MouseButton::Left: return "Mouse Left";
            case MouseButton::Right: return "Mouse Right";
            case MouseButton::Middle: return "Mouse Middle";
            }
            return "Mouse ?";
        case InputDevice::Gamepad:
            return std::string("Gamepad ") + GamepadButtonToDisplayName(GamepadBtn);
        }
        return "?";
    }

    nlohmann::json InputBinding::Serialize() const
    {
        nlohmann::json j;
        j["device"] = (int)Device;
        j["key"] = (int)KeyboardKey;
        j["mouseBtn"] = (int)MouseBtn;
        j["gpBtn"] = (int)GamepadBtn;
        j["gpAxis"] = (int)GamepadAx;
        j["scale"] = Scale;
        j["triggers"] = nlohmann::json::array();
        for (auto& t : Triggers) j["triggers"].push_back(t.Serialize());
        j["modifiers"] = nlohmann::json::array();
        for (auto& m : Modifiers) j["modifiers"].push_back(m.Serialize());
        return j;
    }

    void InputBinding::Deserialize(const nlohmann::json& j)
    {
        Device = (InputDevice)j.value("device", 0);
        KeyboardKey = (Key)j.value("key", (int)Key::Space);
        MouseBtn = (MouseButton)j.value("mouseBtn", 0);
        GamepadBtn = (GamepadButton)j.value("gpBtn", 0);
        GamepadAx = (GamepadAxis)j.value("gpAxis", 0);
        Scale = j.value("scale", 1.0f);
        Triggers.clear();
        if (j.contains("triggers"))
            for (auto& tj : j["triggers"]) { InputTrigger t; t.Deserialize(tj); Triggers.push_back(t); }
        Modifiers.clear();
        if (j.contains("modifiers"))
            for (auto& mj : j["modifiers"]) { InputModifier m; m.Deserialize(mj); Modifiers.push_back(m); }
    }

    // Serialização auxiliar para Triggers/Modifiers de Action/Axis (não-membros,
    // já que ActionMapping/AxisMapping são structs simples sem métodos próprios).
    static nlohmann::json SerializeTriggers(const std::vector<InputTrigger>& v)
    {
        nlohmann::json j = nlohmann::json::array();
        for (auto& t : v) j.push_back(t.Serialize());
        return j;
    }
    static std::vector<InputTrigger> DeserializeTriggers(const nlohmann::json& j)
    {
        std::vector<InputTrigger> v;
        if (j.is_array())
            for (auto& tj : j) { InputTrigger t; t.Deserialize(tj); v.push_back(t); }
        return v;
    }
    static nlohmann::json SerializeModifiers(const std::vector<InputModifier>& v)
    {
        nlohmann::json j = nlohmann::json::array();
        for (auto& m : v) j.push_back(m.Serialize());
        return j;
    }
    static std::vector<InputModifier> DeserializeModifiers(const nlohmann::json& j)
    {
        std::vector<InputModifier> v;
        if (j.is_array())
            for (auto& mj : j) { InputModifier m; m.Deserialize(mj); v.push_back(m); }
        return v;
    }

    // ── InputMappingConfig ──────────────────────────────────────────────────
    InputMappingConfig& InputMappingConfig::Get()
    {
        static InputMappingConfig instance;
        return instance;
    }

    ActionMapping* InputMappingConfig::FindAction(const std::string& name)
    {
        for (auto& a : m_Actions) if (a.Name == name) return &a;
        return nullptr;
    }

    AxisMapping* InputMappingConfig::FindAxis(const std::string& name)
    {
        for (auto& a : m_Axes) if (a.Name == name) return &a;
        return nullptr;
    }

    ActionMapping& InputMappingConfig::AddAction(const std::string& name)
    {
        if (auto* existing = FindAction(name)) return *existing;
        ActionMapping am;
        am.Name = name;
        m_Actions.push_back(std::move(am));
        return m_Actions.back();
    }

    AxisMapping& InputMappingConfig::AddAxis(const std::string& name)
    {
        if (auto* existing = FindAxis(name)) return *existing;
        AxisMapping xm;
        xm.Name = name;
        m_Axes.push_back(std::move(xm));
        return m_Axes.back();
    }

    void InputMappingConfig::RemoveAction(const std::string& name)
    {
        m_Actions.erase(std::remove_if(m_Actions.begin(), m_Actions.end(),
            [&](const ActionMapping& a) { return a.Name == name; }), m_Actions.end());
    }

    void InputMappingConfig::RemoveAxis(const std::string& name)
    {
        m_Axes.erase(std::remove_if(m_Axes.begin(), m_Axes.end(),
            [&](const AxisMapping& a) { return a.Name == name; }), m_Axes.end());
    }

    bool InputMappingConfig::Load(const std::filesystem::path& path)
    {
        m_Actions.clear();
        m_Axes.clear();
        m_LoadedPath = path;

        if (!std::filesystem::exists(path))
            return false;

        try
        {
            std::ifstream f(path);
            nlohmann::json j = nlohmann::json::parse(f);

            if (j.contains("actions"))
                for (auto& aj : j["actions"])
                {
                    ActionMapping am;
                    am.Name = aj.value("name", "");
                    if (aj.contains("triggers")) am.Triggers = DeserializeTriggers(aj["triggers"]);
                    if (aj.contains("modifiers")) am.Modifiers = DeserializeModifiers(aj["modifiers"]);
                    if (aj.contains("bindings"))
                        for (auto& bj : aj["bindings"])
                        {
                            InputBinding b;
                            b.Deserialize(bj);
                            am.Bindings.push_back(b);
                        }
                    m_Actions.push_back(std::move(am));
                }

            if (j.contains("axes"))
                for (auto& aj : j["axes"])
                {
                    AxisMapping xm;
                    xm.Name = aj.value("name", "");
                    xm.ValueType = (AxisValueType)aj.value("valueType", 0);
                    if (aj.contains("triggers")) xm.Triggers = DeserializeTriggers(aj["triggers"]);
                    if (aj.contains("modifiers")) xm.Modifiers = DeserializeModifiers(aj["modifiers"]);
                    if (aj.contains("bindings"))
                        for (auto& bj : aj["bindings"])
                        {
                            InputBinding b;
                            b.Deserialize(bj);
                            xm.Bindings.push_back(b);
                        }
                    m_Axes.push_back(std::move(xm));
                }

            AXE_CORE_INFO("InputMappingConfig: carregado de {} ({} actions, {} axes)",
                path.string(), m_Actions.size(), m_Axes.size());
            return true;
        }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("InputMappingConfig: falha ao carregar {} — {}", path.string(), e.what());
            return false;
        }
    }

    bool InputMappingConfig::Save(const std::filesystem::path& path) const
    {
        try
        {
            nlohmann::json j;
            j["actions"] = nlohmann::json::array();
            for (auto& a : m_Actions)
            {
                nlohmann::json aj;
                aj["name"] = a.Name;
                aj["triggers"] = SerializeTriggers(a.Triggers);
                aj["modifiers"] = SerializeModifiers(a.Modifiers);
                aj["bindings"] = nlohmann::json::array();
                for (auto& b : a.Bindings) aj["bindings"].push_back(b.Serialize());
                j["actions"].push_back(aj);
            }
            j["axes"] = nlohmann::json::array();
            for (auto& a : m_Axes)
            {
                nlohmann::json aj;
                aj["name"] = a.Name;
                aj["valueType"] = (int)a.ValueType;
                aj["triggers"] = SerializeTriggers(a.Triggers);
                aj["modifiers"] = SerializeModifiers(a.Modifiers);
                aj["bindings"] = nlohmann::json::array();
                for (auto& b : a.Bindings) aj["bindings"].push_back(b.Serialize());
                j["axes"].push_back(aj);
            }

            std::ofstream f(path);
            f << j.dump(4);
            AXE_CORE_INFO("InputMappingConfig: salvo em {}", path.string());
            return true;
        }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("InputMappingConfig: falha ao salvar {} — {}", path.string(), e.what());
            return false;
        }
    }

} // namespace axe