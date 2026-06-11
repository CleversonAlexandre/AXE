#pragma once
#include "axe/core/types.hpp"
#include <string>
#include <functional>

namespace axe
{
    // Callback de progresso: (mensagem, sucesso)
    using CompileCallback = std::function<void(const std::string&, bool)>;

    class AXE_API ScriptCompiler
    {
    public:
        // Compila o arquivo .cpp e gera a .dll
        // cppPath   — path do .cpp gerado pelo ScriptGraphCompiler
        // dllOutput — path de saída da .dll
        // callback  — chamado ao final com resultado
        static bool Compile(
            const std::string& cppPath,
            const std::string& dllOutput,
            const std::string& engineIncludeDir,
            const std::string& axeLibPath,
            CompileCallback callback = nullptr);

        // Retorna o path do compilador em uso
        static std::string GetCompilerPath();

    private:
        static std::string s_CompilerPath;

        // Executa um processo e captura a saída
        static bool RunProcess(const std::string& cmdLine, std::string& output);
    };

} // namespace axe