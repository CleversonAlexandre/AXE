#pragma once
#include "axe/core/types.hpp"
#include "script_base.hpp"
#include <string>

namespace axe
{
    class AXE_API DllLoader
    {
    public:
        // Carrega a DLL e retorna o handle
        // Retorna nullptr se falhar
        static void* Load(const std::string& dllPath);

        // Descarrega a DLL
        static void Unload(void* handle);

        // Cria uma instância do script a partir da DLL carregada
        // A DLL deve exportar: extern "C" __declspec(dllexport) ScriptBase* CreateScript()
        static ScriptBase* CreateInstance(void* handle);

        // Verifica se o MSVC está disponível no sistema
        static bool IsMSVCAvailable();

        // Retorna o path do cl.exe encontrado, ou "" se não encontrado
        static std::string FindMSVCCompiler();
    };

} // namespace axe