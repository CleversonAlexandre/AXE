#pragma once
#include "axe/core/types.hpp"

namespace axe
{
    // Fonte de tempo ÚNICA da engine.
    //
    // Alimentada uma vez por frame pelo loop principal (Application::Run) a
    // partir de Window::GetTime() — que é a ÚNICA peça que conhece a
    // biblioteca de janela/SO (GLFW). Qualquer sistema (render, materiais,
    // lógica de cena) lê o tempo daqui, SEM incluir <GLFW/glfw3.h> nem
    // chamar glfwGetTime() direto. Como é um relógio só, forward e deferred
    // animam u_Time em perfeita sincronia.
    //
    // Métodos são out-of-line de propósito (definidos em time.cpp, dentro da
    // axe.dll): assim o static s_Elapsed vive num lugar só e EXE + DLL
    // compartilham o mesmo valor pelos símbolos exportados — mesmo padrão do
    // Input. Um static inline no header duplicaria o valor entre módulos.
    class AXE_API Time
    {
    public:
        // Segundos decorridos desde o início da aplicação.
        static float Elapsed();

        // Chamado uma vez por frame pelo loop principal.
        static void SetElapsed(float seconds);

    private:
        static float s_Elapsed;
    };
}