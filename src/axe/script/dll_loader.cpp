#include "dll_loader.hpp"
#include "axe/log/log.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <array>

namespace axe
{
    void* DllLoader::Load(const std::string& dllPath)
    {
        HMODULE handle = LoadLibraryA(dllPath.c_str());
        if (!handle)
        {
            DWORD err = GetLastError();
            AXE_CORE_ERROR("DllLoader: falha ao carregar '{}' — erro {}", dllPath, err);
            return nullptr;
        }
        AXE_CORE_INFO("DllLoader: '{}' carregada.", dllPath);
        return (void*)handle;
    }

    void DllLoader::Unload(void* handle)
    {
        if (!handle) return;
        FreeLibrary((HMODULE)handle);
        AXE_CORE_INFO("DllLoader: DLL descarregada.");
    }

    ScriptBase* DllLoader::CreateInstance(void* handle)
    {
        if (!handle) return nullptr;

        using Fn = ScriptBase * (*)();
        Fn createFn = (Fn)GetProcAddress((HMODULE)handle, "CreateScript");
        if (!createFn)
        {
            AXE_CORE_ERROR("DllLoader: função 'CreateScript' não encontrada na DLL.");
            return nullptr;
        }
        return createFn();
    }

    bool DllLoader::IsMSVCAvailable()
    {
        return !FindMSVCCompiler().empty();
    }

    std::string DllLoader::FindMSVCCompiler()
    {
        // 1. Tenta cl.exe no PATH primeiro (Developer Command Prompt)
        {
            char buf[512] = {};
            DWORD r = SearchPathA(nullptr, "cl.exe", nullptr, sizeof(buf), buf, nullptr);
            if (r > 0) return std::string(buf);
        }

        // 2. Usa vswhere.exe para localizar o VS instalado (método oficial Microsoft)
        auto tryVsWhere = [](const std::string& vswhere) -> std::string
            {
                std::string cmd = """ + vswhere + "" -latest -requires Microsoft.VisualCpp.Tools.HostX64.TargetX64 -find VC\\Tools\\MSVC\\**\\bin\\Hostx64\\x64\\cl.exe 2>nul";
                char buf[1024] = {};
                FILE* pipe = _popen(cmd.c_str(), "r");
                if (!pipe) return "";
                if (fgets(buf, sizeof(buf), pipe))
                {
                    _pclose(pipe);
                    std::string result = buf;
                    // Remove newline
                    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
                        result.pop_back();
                    if (std::filesystem::exists(result)) return result;
                }
                _pclose(pipe);
                return "";
            };

        // vswhere está em locais previsíveis
        const std::vector<std::string> vswhereLocations = {
            "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe",
            "C:/Program Files/Microsoft Visual Studio/Installer/vswhere.exe",
        };
        for (auto& vswhere : vswhereLocations)
        {
            if (!std::filesystem::exists(vswhere)) continue;
            std::string cl = tryVsWhere(vswhere);
            if (!cl.empty()) return cl;
        }

        // 3. Fallback: busca manual em todos os caminhos comuns + anos + edições
        const std::vector<std::string> editions = {
            "Community", "Professional", "Enterprise", "BuildTools", "Preview"
        };
        const std::vector<std::string> years = { "2022", "2019", "2017" };
        const std::vector<std::string> roots = {
            "C:/Program Files/Microsoft Visual Studio",
            "C:/Program Files (x86)/Microsoft Visual Studio",
        };

        for (auto& root : roots)
            for (auto& year : years)
                for (auto& ed : editions)
                {
                    auto base = root + "/" + year + "/" + ed + "/VC/Tools/MSVC";
                    if (!std::filesystem::exists(base)) continue;
                    for (const auto& ver : std::filesystem::directory_iterator(base))
                    {
                        auto cl = ver.path() / "bin/Hostx64/x64/cl.exe";
                        if (std::filesystem::exists(cl)) return cl.string();
                        // ARM/x86 hosts também
                        cl = ver.path() / "bin/Hostx86/x64/cl.exe";
                        if (std::filesystem::exists(cl)) return cl.string();
                    }
                }

        AXE_CORE_WARN("DllLoader: MSVC (cl.exe) não encontrado. Scripts não poderão ser compilados.");
        return "";
    }

} // namespace axe