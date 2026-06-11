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

    // ── Valida que um cl.exe é o compilador MSVC real ─────────────────────────
    // O cl.exe falso do NuGet/Entity Framework fica em pastas arbitrárias.
    // O cl.exe real do MSVC tem sempre c1.dll ou c2.dll no mesmo diretório.
    static bool IsRealMSVCCompiler(const std::filesystem::path& clPath)
    {
        if (!std::filesystem::exists(clPath)) return false;

        // Deve estar dentro de VC\Tools\MSVC
        std::string s = clPath.string();
        bool inMSVC = (s.find("VC\\Tools\\MSVC") != std::string::npos ||
            s.find("VC/Tools/MSVC") != std::string::npos);
        if (!inMSVC) return false;

        // Confirma presença de c1.dll / c2.dll (só existem no compilador real)
        auto dir = clPath.parent_path();
        return std::filesystem::exists(dir / "c1.dll") ||
            std::filesystem::exists(dir / "c2.dll") ||
            std::filesystem::exists(dir / "c1xx.dll");
    }

    // ── Busca recursiva em uma raiz do VS ─────────────────────────────────────
    // Percorre: <root>/<qualquer pasta>/<edição>/VC/Tools/MSVC/<ver>/bin/Hostx64/x64/cl.exe
    // Isso cobre anos numéricos (2022, 18, 17, ...) e qualquer edição futura.
    static std::string SearchVSRoot(const std::string& root)
    {
        namespace fs = std::filesystem;

        fs::path rootPath(root);
        if (!fs::exists(rootPath)) return "";

        std::error_code ec;

        // Nível 1: ano/versão (ex: "2022", "2019", "18", "17"...)
        for (const auto& yearEntry : fs::directory_iterator(rootPath, ec))
        {
            if (!yearEntry.is_directory()) continue;

            // Nível 2: edição (Community, Professional, Enterprise, BuildTools...)
            for (const auto& edEntry : fs::directory_iterator(yearEntry.path(), ec))
            {
                if (!edEntry.is_directory()) continue;

                fs::path msvcBase = edEntry.path() / "VC" / "Tools" / "MSVC";
                if (!fs::exists(msvcBase)) continue;

                // Nível 3: versão do toolset (ex: 14.38.33130, 14.51.36231...)
                // Prefere a versão mais recente — itera e guarda a maior
                std::string bestCl;
                std::string bestVer;

                for (const auto& verEntry : fs::directory_iterator(msvcBase, ec))
                {
                    if (!verEntry.is_directory()) continue;

                    fs::path cl = verEntry.path() / "bin" / "Hostx64" / "x64" / "cl.exe";
                    if (IsRealMSVCCompiler(cl))
                    {
                        std::string ver = verEntry.path().filename().string();
                        if (ver > bestVer)   // comparação lexicográfica — funciona para X.Y.Z
                        {
                            bestVer = ver;
                            bestCl = cl.string();
                        }
                    }

                    // Fallback: Hostx86 targeting x64
                    cl = verEntry.path() / "bin" / "Hostx86" / "x64" / "cl.exe";
                    if (bestCl.empty() && IsRealMSVCCompiler(cl))
                        bestCl = cl.string();
                }

                if (!bestCl.empty())
                    return bestCl;
            }
        }
        return "";
    }

    std::string DllLoader::FindMSVCCompiler()
    {
        // ── Estratégia 1: vswhere.exe (método oficial Microsoft) ──────────────
        auto tryVsWhere = [](const std::string& vswhere) -> std::string
            {
                std::string cmd =
                    "\"" + vswhere + "\""
                    " -latest -products *"
                    " -requires Microsoft.VisualCpp.Tools.HostX64.TargetX64"
                    " -find \"VC\\Tools\\MSVC\\**\\bin\\Hostx64\\x64\\cl.exe\""
                    " 2>nul";

                FILE* pipe = _popen(cmd.c_str(), "r");
                if (!pipe) return "";

                char buf[1024] = {};
                while (fgets(buf, sizeof(buf), pipe))
                {
                    std::string result = buf;
                    while (!result.empty() &&
                        (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
                        result.pop_back();

                    if (IsRealMSVCCompiler(result))
                    {
                        _pclose(pipe);
                        AXE_CORE_INFO("DllLoader: cl.exe via vswhere: {}", result);
                        return result;
                    }
                }
                _pclose(pipe);
                return "";
            };

        for (auto& vsw : {
            "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe",
            "C:/Program Files/Microsoft Visual Studio/Installer/vswhere.exe" })
        {
            if (!std::filesystem::exists(vsw)) continue;
            std::string cl = tryVsWhere(vsw);
            if (!cl.empty()) return cl;
        }

        // ── Estratégia 2: busca nas raízes do VS sem assumir ano fixo ─────────
        // Cobre VS2017 (15), VS2019 (16), VS2022 (17), VS2025 (18) e futuros.
        for (auto& root : {
            "C:/Program Files/Microsoft Visual Studio",
            "C:/Program Files (x86)/Microsoft Visual Studio" })
        {
            std::string cl = SearchVSRoot(root);
            if (!cl.empty())
            {
                AXE_CORE_INFO("DllLoader: cl.exe encontrado: {}", cl);
                return cl;
            }
        }

        // ── Estratégia 3: PATH como último recurso, com validação ─────────────
        {
            char buf[512] = {};
            if (SearchPathA(nullptr, "cl.exe", nullptr, sizeof(buf), buf, nullptr) > 0)
            {
                if (IsRealMSVCCompiler(buf))
                {
                    AXE_CORE_INFO("DllLoader: cl.exe no PATH: {}", buf);
                    return std::string(buf);
                }
                AXE_CORE_WARN("DllLoader: cl.exe no PATH ignorado (não é MSVC real): {}", buf);
            }
        }

        AXE_CORE_ERROR(
            "DllLoader: MSVC (cl.exe) não encontrado.\n"
            "  Instale o Visual Studio com o componente\n"
            "  'Desenvolvimento para desktop com C++' ou o\n"
            "  'Visual Studio Build Tools' (gratuito).\n"
            "  Download: https://aka.ms/vs/17/release/vs_BuildTools.exe");
        return "";
    }

} // namespace axe