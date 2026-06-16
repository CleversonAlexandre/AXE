#include "script_compiler.hpp"
#include "dll_loader.hpp"
#include "axe/log/log.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <sstream>

namespace axe
{
    std::string ScriptCompiler::s_CompilerPath;

    std::string ScriptCompiler::GetCompilerPath()
    {
        if (s_CompilerPath.empty())
            s_CompilerPath = DllLoader::FindMSVCCompiler();
        return s_CompilerPath;
    }

    static std::string FindVcVarsAll(const std::string& clExePath)
    {
        // cl.exe fica em: .../MSVC/<ver>/bin/Hostx64/x64/cl.exe
        // vcvarsall.bat fica em: .../VC/Auxiliary/Build/vcvarsall.bat
        std::filesystem::path cl(clExePath);
        // Sobe: x64 -> Hostx64 -> bin -> <ver> -> MSVC -> VC -> VS root
        auto msvcBin = cl.parent_path().parent_path().parent_path(); // .../MSVC/<ver>
        auto msvcRoot = msvcBin.parent_path().parent_path();          // .../VC/Tools/MSVC -> .../VC
        // Porém a estrutura é: VC/Tools/MSVC/<ver>/bin/...
        // vcvarsall está em: VC/Auxiliary/Build/
        auto vcDir = msvcRoot.parent_path().parent_path(); // .../VC
        auto vcvarsall = vcDir / "Auxiliary" / "Build" / "vcvarsall.bat";
        if (std::filesystem::exists(vcvarsall))
            return vcvarsall.string();
        return "";
    }

    bool ScriptCompiler::Compile(
        const std::string& cppPath,
        const std::string& dllOutput,
        const std::string& engineIncludeDir,
        const std::string& axeLibPath,
        CompileCallback callback)
    {
        std::string compiler = GetCompilerPath();
        if (compiler.empty())
        {
            std::string msg = "MSVC não encontrado. Instale o Visual Studio Build Tools.";
            AXE_CORE_ERROR("ScriptCompiler: {}", msg);
            if (callback) callback(msg, false);
            return false;
        }

        // Garante que o diretório de saída existe
        std::filesystem::create_directories(
            std::filesystem::path(dllOutput).parent_path());

        // Monta o comando de compilação
        // /LD        = gera DLL
        // /O2        = otimização
        // /EHsc      = exceções C++
        // /std:c++20 = padrão C++20 (entt requer concepts)
        // /utf-8     = encoding UTF-8 (exigido pelo spdlog/fmt)
        // /I         = include do engine
        // /Fe        = arquivo de saída
        // Localiza vcvarsall.bat no mesmo VS que tem o cl.exe
        // e wrapa o comando para configurar o ambiente INCLUDE/LIB
        std::string vcvarsall = FindVcVarsAll(compiler);

        // Monta flags /I para cada path (separados por ;)
        std::string includeFlags;
        {
            std::istringstream includeStream(engineIncludeDir);
            std::string token;
            while (std::getline(includeStream, token, ';'))
                if (!token.empty())
                    includeFlags += " /I\"" + token + "\"";
        }

        // Detecta se axe.dll foi compilada em Debug — usa mesmo CRT para evitar
        // incompatibilidade de layout de std::string, std::vector etc. cross-DLL.
        // axe.dll Debug usa /MDd (_ITERATOR_DEBUG_LEVEL=2), Release usa /MD.
#if defined(_DEBUG) || defined(AXE_DEBUG)
        const char* crtFlag = " /MDd";
        const char* debugDefs = " /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2";
#else
        const char* crtFlag = " /MD";
        const char* debugDefs = " /DNDEBUG /D_ITERATOR_DEBUG_LEVEL=0";
#endif

        std::ostringstream cmd;
        if (!vcvarsall.empty())
        {
            cmd << "cmd /C \"\""
                << vcvarsall << "\" x64 && "
                << "\"" << compiler << "\""
                << " /LD /Od /EHsc /std:c++20 /utf-8 /DAXE_PLATFORM_WINDOWS"
                << crtFlag << debugDefs
                << includeFlags
                << " /Fe\"" << dllOutput << "\""
                << " \"" << cppPath << "\""
                << " /link /DLL \"" << axeLibPath << "\"";
        }
        else
        {
            cmd << "\"" << compiler << "\""
                << " /LD /Od /EHsc /std:c++20 /utf-8 /DAXE_PLATFORM_WINDOWS"
                << crtFlag << debugDefs
                << includeFlags
                << " /Fe\"" << dllOutput << "\""
                << " \"" << cppPath << "\""
                << " /link /DLL \"" << axeLibPath << "\"";
        }

        AXE_CORE_INFO("ScriptCompiler: compilando '{}'...", cppPath);
        AXE_CORE_INFO("ScriptCompiler: cmd = {}", cmd.str());

        std::string output;
        bool ok = RunProcess(cmd.str(), output);

        if (ok)
        {
            AXE_CORE_INFO("ScriptCompiler: compilação bem-sucedida → '{}'", dllOutput);
            if (callback) callback("Compilação bem-sucedida.", true);
        }
        else
        {
            AXE_CORE_ERROR("ScriptCompiler: erro de compilação:\n{}", output);
            if (callback) callback(output, false);
        }

        return ok;
    }

    bool ScriptCompiler::RunProcess(const std::string& cmdLine, std::string& output)
    {
        // Pipe para capturar stdout/stderr
        HANDLE hReadPipe, hWritePipe;
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
            return false;
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION pi = {};

        std::string cmd = cmdLine;
        bool created = CreateProcessA(
            nullptr, cmd.data(), nullptr, nullptr,
            TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

        CloseHandle(hWritePipe);

        if (!created)
        {
            CloseHandle(hReadPipe);
            return false;
        }

        // Lê a saída do processo
        char buf[4096];
        DWORD bytesRead;
        while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0)
        {
            buf[bytesRead] = '\0';
            output += buf;
        }

        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);

        return exitCode == 0;
    }

} // namespace axe