#pragma once

namespace axe
{

    // Keycodes independentes de backend
    // Valores iguais ao GLFW para simplicidade agora
    // Se mudar para SDL, só muda o mapeamento no WindowSDL
    enum class Key : int
    {
        W = 87,
        A = 65,
        S = 83,
        D = 68,
        Q = 81,
        E = 69,
        P = 80,
        R = 82,
        T = 84,
        F2 = 291,
        Delete = 261,
        Escape = 256,
        LeftShift = 340,
        LeftControl = 341,
        LeftAlt = 342,
        D_Key = 68,  // Ctrl+D
    };

} // namespace axe