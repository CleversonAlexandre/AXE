#include "file_dialog.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "Comdlg32.lib")
#endif

namespace axe
{
    static std::wstring Utf8ToWide(const char* str)
    {
        if (!str || !str[0]) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
        std::wstring wstr(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, str, -1, &wstr[0], size);
        return wstr;
    }

//    std::filesystem::path FileDialog::Open(
//        const char* filter, const char* title, const char* defaultExt)
//    {
//#ifdef _WIN32
//        char szFile[MAX_PATH] = { 0 };
//
//        OPENFILENAMEA ofn;
//        ZeroMemory(&ofn, sizeof(ofn));
//        ofn.lStructSize = sizeof(ofn);
//        ofn.hwndOwner = nullptr;
//        ofn.lpstrFile = szFile;
//        ofn.nMaxFile = sizeof(szFile);
//        ofn.lpstrFilter = filter;
//        ofn.lpstrTitle = title;
//        ofn.lpstrDefExt = defaultExt;
//        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
//
//        if (GetOpenFileNameA(&ofn))
//            return std::filesystem::path(szFile);
//#endif
//        return {};
//    }
    std::filesystem::path FileDialog::Open(const char* filter, const char* title, const char* defaultExt)
    {
        // 1. Usa wchar_t ao invés de char
        wchar_t filename[260] = { 0 };

        // 2. Converte os parâmetros que chegam como const char* para Wide
        std::wstring wFilter = Utf8ToWide(filter);
        std::wstring wTitle = Utf8ToWide(title);

        // 3. Usa a versão W da struct
        OPENFILENAMEW ofn = { 0 };
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = GetActiveWindow(); // Pode precisar passar a janela do ImGui aqui se tiver
        ofn.lpstrFilter = wFilter.c_str();
        ofn.lpstrFile = filename;
        ofn.nMaxFile = 260;
        ofn.lpstrTitle = wTitle.c_str();
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

        // 4. Chama a versão W
        if (GetOpenFileNameW(&ofn))
        {
            // 5. A LINHA 30 QUE CRASHAVA AGORA É ESTA:
            // Como 'filename' JÁ É wchar_t*, o std::filesystem::path constrói 
            // perfeitamente sem fazer conversão ANSI -> Wide. Fim do crash!
            return std::filesystem::path(filename);
        }

        return ""; // Retorno vazio se o usuário cancelar
    }

    std::filesystem::path FileDialog::Save(
        const char* filter, const char* title, const char* defaultExt)
    {
#ifdef _WIN32
        char szFile[MAX_PATH] = { 0 };

        OPENFILENAMEA ofn;
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filter;
        ofn.lpstrTitle = title;
        ofn.lpstrDefExt = defaultExt;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

        if (GetSaveFileNameA(&ofn))
            return std::filesystem::path(szFile);
#endif
        return {};
    }

   

} // namespace axe