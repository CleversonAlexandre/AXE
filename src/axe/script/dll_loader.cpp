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
        // Tenta cl.exe no PATH
        {
            char buf[512] = {};
            DWORD r = SearchPathA(nullptr, "cl.exe", nullptr, sizeof(buf), buf, nullptr);
            if (r > 0) return std::string(buf);
        }

        // Procura em locais comuns do Visual Studio
        const std::vector<std::string> searchPaths = {
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC",
            "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC",
            "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC",
            "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/MSVC",
            "C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Tools/MSVC",
        };

        for (const auto& base : searchPaths)
        {
            if (!std::filesystem::exists(base)) continue;
            for (const auto& ver : std::filesystem::directory_iterator(base))
            {
                auto cl = ver.path() / "bin/Hostx64/x64/cl.exe";
                if (std::filesystem::exists(cl))
                    return cl.string();
            }
        }

        AXE_CORE_WARN("DllLoader: MSVC (cl.exe) não encontrado. Scripts não poderão ser compilados.");
        return "";
    }

} // namespace axe