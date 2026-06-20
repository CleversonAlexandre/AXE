#pragma once
#include "axe/core/types.hpp"
#include "axe/input/key_codes.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <filesystem>

namespace axe
{
    // ── Dispositivos de binding ────────────────────────────────────────────
    enum class InputDevice : int
    {
        Keyboard = 0,
        Mouse = 1,
        Gamepad = 2,
    };

    enum class MouseButton : int
    {
        Left = 0, Right = 1, Middle = 2,
    };

    // Convenção XInput. Leitura de hardware ainda não implementada (ver
    // Input::GetGamepadButton) — enums e formato de dados já prontos para não
    // exigir migração de InputConfig.json quando o suporte real chegar.
    enum class GamepadButton : int
    {
        A = 0, B = 1, X = 2, Y = 3,
        LeftBumper = 4, RightBumper = 5,
        Back = 6, Start = 7,
        LeftStick = 8, RightStick = 9,
        DPadUp = 10, DPadRight = 11, DPadDown = 12, DPadLeft = 13,
    };

    enum class GamepadAxis : int
    {
        LeftX = 0, LeftY = 1,
        RightX = 2, RightY = 3,
        LeftTrigger = 4, RightTrigger = 5,
    };

    // ── Tipo de valor de uma Axis Mapping ───────────────────────────────────
    // Action Mappings são sempre Digital (bool) — essa categoria é separada na
    // UI por decisão de design. Axis Mappings podem ser 1D/2D/3D: por exemplo
    // "Move" como Axis2D lendo WASD inteiro em um único mapping (X=Right,
    // Y=Forward), ao invés de duas Axis1D separadas.
    enum class AxisValueType : int
    {
        Axis1D = 0,
        Axis2D = 1,
        Axis3D = 2,
    };

    // ── Triggers ─────────────────────────────────────────────────────────────
    // Decidem QUANDO a Action/Axis dispara, além da leitura "crua" do estado
    // pressionado/solto. Replicado do Enhanced Input da Unreal.
    enum class TriggerType : int
    {
        Pressed = 0,  // dispara uma vez no frame em que foi pressionado (leading edge)
        Released = 1, // dispara uma vez no frame em que foi solto (trailing edge)
        Held = 2,     // dispara repetidamente enquanto pressionado por >= HoldTime
        Tap = 3,      // dispara se solto antes de TapThreshold segundos
        Pulse = 4,    // dispara a cada PulseInterval segundos enquanto pressionado
    };

    struct AXE_API InputTrigger
    {
        TriggerType Type = TriggerType::Pressed;
        float HoldTime = 0.5f;       // usado por Held
        float TapThreshold = 0.2f;   // usado por Tap
        float PulseInterval = 0.1f;  // usado por Pulse
        bool  PulseTriggerOnStart = true; // Pulse também dispara no instante do Pressed

        nlohmann::json Serialize() const;
        void Deserialize(const nlohmann::json& j);
        std::string ToDisplayString() const; // ex.: "Held (0.5s)"
    };

    // ── Modifiers ────────────────────────────────────────────────────────────
    // Transformam o valor lido ANTES dos Triggers avaliarem. Aplicados em
    // ordem na lista. Os campos abaixo cobrem os 4 modifiers desta passada;
    // cada um usa só os campos que lhe interessam (comentado em cada caso).
    enum class ModifierType : int
    {
        Negate = 0,   // inverte o sinal nos eixos marcados (NegateX/Y/Z)
        Scale = 1,    // multiplica o valor por ScaleFactor (todos os eixos)
        DeadZone = 2, // zera valores entre -DeadZoneLower e +DeadZoneLower;
        // remapeia o restante para [0, DeadZoneUpper] -> [0,1]
        Swizzle = 3,  // reordena componentes conforme SwizzleX/Y/Z (índice do
        // componente de origem: 0=X, 1=Y, 2=Z)
    };

    struct AXE_API InputModifier
    {
        ModifierType Type = ModifierType::Negate;

        // Negate
        bool NegateX = true, NegateY = true, NegateZ = true;

        // Scale
        float ScaleFactor = 1.0f;

        // Dead Zone
        float DeadZoneLower = 0.2f;
        float DeadZoneUpper = 1.0f;

        // Swizzle — para cada componente de SAÍDA, qual componente de ENTRADA
        // usar (0=X, 1=Y, 2=Z). Default troca X<->Y (comum para inverter
        // stick horizontal/vertical).
        int SwizzleX = 1, SwizzleY = 0, SwizzleZ = 2;

        nlohmann::json Serialize() const;
        void Deserialize(const nlohmann::json& j);
        std::string ToDisplayString() const; // ex.: "Negate (X)", "Scale x2.0"
    };

    // ── Binding individual ─────────────────────────────────────────────────
    // Um "fio" dentro de uma Action ou Axis Mapping, ligando-o a uma tecla,
    // botão de mouse ou gamepad física. Carrega seus PRÓPRIOS Triggers e
    // Modifiers (mapeando o conceito de "overrides no Mapping Context" da
    // Unreal) — aplicados em conjunto com os da Action/Axis "pai".
    struct AXE_API InputBinding
    {
        InputDevice Device = InputDevice::Keyboard;
        Key         KeyboardKey = Key::Space;
        MouseButton MouseBtn = MouseButton::Left;
        GamepadButton GamepadBtn = GamepadButton::A;
        GamepadAxis GamepadAx = GamepadAxis::LeftX;
        float       Scale = 1.0f; // multiplicador rápido por-binding (ex.: W=+1, S=-1)

        std::vector<InputTrigger>  Triggers;
        std::vector<InputModifier> Modifiers;

        nlohmann::json Serialize() const;
        void Deserialize(const nlohmann::json& j);
        std::string ToDisplayString() const; // ex.: "W", "Mouse Right", "Gamepad A"
    };

    struct ActionMapping
    {
        std::string Name; // nome semântico, ex.: "Jump"
        std::vector<InputTrigger>  Triggers;  // Triggers/Modifiers padrão da Action
        std::vector<InputModifier> Modifiers; // (aplicados antes dos do binding)
        std::vector<InputBinding> Bindings;
    };

    struct AxisMapping
    {
        std::string Name; // nome semântico, ex.: "MoveForward" ou "Move" (2D)
        AxisValueType ValueType = AxisValueType::Axis1D;
        std::vector<InputTrigger>  Triggers;
        std::vector<InputModifier> Modifiers;
        std::vector<InputBinding> Bindings;
    };

    // ── Config central — Singleton ─────────────────────────────────────────
    class AXE_API InputMappingConfig
    {
    public:
        static InputMappingConfig& Get();

        bool Load(const std::filesystem::path& path);
        bool Save(const std::filesystem::path& path) const;

        std::vector<ActionMapping>& GetActions() { return m_Actions; }
        std::vector<AxisMapping>& GetAxes() { return m_Axes; }
        const std::vector<ActionMapping>& GetActions() const { return m_Actions; }
        const std::vector<AxisMapping>& GetAxes()     const { return m_Axes; }

        ActionMapping* FindAction(const std::string& name);
        AxisMapping* FindAxis(const std::string& name);

        ActionMapping& AddAction(const std::string& name);
        AxisMapping& AddAxis(const std::string& name);
        void RemoveAction(const std::string& name);
        void RemoveAxis(const std::string& name);

        const std::filesystem::path& GetLoadedPath() const { return m_LoadedPath; }

    private:
        std::vector<ActionMapping> m_Actions;
        std::vector<AxisMapping>   m_Axes;
        std::filesystem::path     m_LoadedPath;
    };

} // namespace axe