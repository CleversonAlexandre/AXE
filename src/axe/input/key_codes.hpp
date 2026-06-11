#pragma once

namespace axe
{

    // Keycodes independentes de backend
    // Valores iguais ao GLFW para simplicidade agora
    // Se mudar para SDL, só muda o mapeamento no WindowSDL
    enum class Key : int
    {
        // Letras
        A = 65, B = 66, C = 67, D = 68, E = 69,
        F = 70, G = 71, H = 72, I = 73, J = 74,
        K = 75, L = 76, M = 77, N = 78, O = 79,
        P = 80, Q = 81, R = 82, S = 83, T = 84,
        U = 85, V = 86, W = 87, X = 88, Y = 89, Z = 90,

        // Números do teclado principal
        Num0 = 48, Num1 = 49, Num2 = 50, Num3 = 51, Num4 = 52,
        Num5 = 53, Num6 = 54, Num7 = 55, Num8 = 56, Num9 = 57,

        // Especiais
        Space = 32,
        Enter = 257,
        Backspace = 259,
        Tab = 258,
        Escape = 256,
        Delete = 261,
        Insert = 260,
        Home = 268,
        End = 269,
        PageUp = 266,
        PageDown = 267,

        // Setas
        Right = 262,
        Left = 263,
        Down = 264,
        Up = 265,

        // Modificadores
        LeftShift = 340,
        RightShift = 344,
        LeftControl = 341,
        RightControl = 345,
        LeftAlt = 342,
        RightAlt = 346,
        LeftSuper = 343,

        // F-Keys
        F1 = 290, F2 = 291, F3 = 292, F4 = 293,
        F5 = 294, F6 = 295, F7 = 296, F8 = 297,
        F9 = 298, F10 = 299, F11 = 300, F12 = 301,

        // Alias legados
        D_Key = 68,
    };

} // namespace axe