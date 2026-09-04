#include "file_dialog.hpp"


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ShlObj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <commdlg.h>
#pragma comment(lib, "Comdlg32.lib")

// PROJECT_NEW_V2c — o IFileDialog e o CLSID_FileOpenDialog vem daqui. Sem
// estes dois o codigo COMPILA e nao LINKA, que e o pior momento para
// descobrir uma dependencia.
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Uuid.lib")
#pragma comment(lib, "Shell32.lib")
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




    // ═══════════════════════════════════════════════════════════════════════
    //  PROJECT_NEW_V2c — a trava de verdade: MTA + BIF_NEWDIALOGSTYLE
    //
    //  ── O QUE EU ERREI NA TENTATIVA ANTERIOR ───────────────────────────────
    //
    //  Eu tratei `RPC_E_CHANGED_MODE` como "COM esta pronto, pode usar o
    //  dialogo novo". Esta errado, e e o oposto: esse retorno significa que a
    //  thread JA tinha COM inicializado em MTA (multithreaded). E
    //  BIF_NEWDIALOGSTYLE exige STA (apartment). Pedir o dialogo novo numa
    //  thread MTA e exatamente o caso em que ele pendura em vez de falhar.
    //
    //  Ou seja: a primeira correcao resolveu o dialogo nascer atras da janela,
    //  e manteve viva a causa da trava. Sintomas parecidos, causas diferentes.
    //
    //  ── O QUE ESTA VERSAO FAZ ──────────────────────────────────────────────
    //
    //  STA disponivel  -> IFileDialog com FOS_PICKFOLDERS. E a API moderna:
    //                     navegador de pasta de verdade, com campo de caminho
    //                     e atalhos, e sem as armadilhas do SHBrowseForFolder.
    //  Thread em MTA   -> SHBrowseForFolderW SEM o estilo novo. O dialogo
    //                     antigo e feio e funciona em MTA, que e o que importa
    //                     quando a alternativa e travar.
    //
    //  ── E MAIS DOIS CUIDADOS ───────────────────────────────────────────────
    //
    //  ReleaseCapture() antes de abrir: o clique que abriu o dialogo pode ter
    //  deixado o mouse capturado pela janela da engine, e um modal que nao
    //  recebe mouse parece travado do mesmo jeito.
    //
    //  A janela dona vem do handle REAL, e nao de GetActiveWindow(): esta
    //  ultima devolve nulo quando a janela ativa nao pertence a fila de
    //  mensagens desta thread — e nulo aqui e o bug da rodada passada de volta.
    // ═══════════════════════════════════════════════════════════════════════
#ifdef _WIN32
    static HWND AxeOwnerWindow()
    {
        // GetActiveWindow primeiro (barato e correto no caso normal), com
        // GetForegroundWindow como rede. Nulo nos dois casos e melhor do que
        // um handle de outro processo: um dono errado e pior que dono nenhum.
        if (HWND h = GetActiveWindow()) return h;

        HWND fg = GetForegroundWindow();
        if (!fg) return nullptr;

        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);

        return (pid == GetCurrentProcessId()) ? fg : nullptr;
    }

    static std::filesystem::path PickFolderModern(HWND owner, const wchar_t* title)
    {
        std::filesystem::path result;

        IFileDialog* dialog = nullptr;

        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog))))
            return result;


        DWORD options = 0;

        if (SUCCEEDED(dialog->GetOptions(&options)))
        {
            // FOS_PICKFOLDERS transforma o dialogo de arquivo em seletor de
            // PASTA. FOS_FORCEFILESYSTEM descarta locais que nao tem caminho
            // real (bibliotecas, dispositivos MTP) — o projeto precisa de uma
            // pasta que exista no disco.
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }

        if (title) dialog->SetTitle(title);

        if (SUCCEEDED(dialog->Show(owner)))
        {
            IShellItem* item = nullptr;

            if (SUCCEEDED(dialog->GetResult(&item)))
            {
                PWSTR path = nullptr;

                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    result = std::filesystem::path(path);
                    CoTaskMemFree(path);
                }

                item->Release();
            }
        }

        dialog->Release();
        return result;
    }

    static std::filesystem::path PickFolderLegacy(HWND owner, const wchar_t* title)
    {
        BROWSEINFOW bi = { 0 };
        bi.hwndOwner = owner;
        bi.lpszTitle = title;

        // SEM BIF_NEWDIALOGSTYLE: e justamente ele que exige STA. Este caminho
        // so roda quando NAO temos STA.
        bi.ulFlags = BIF_RETURNONLYFSDIRS;

        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (!pidl) return {};

        std::filesystem::path result;
        wchar_t buffer[MAX_PATH] = { 0 };

        if (SHGetPathFromIDListW(pidl, buffer))
            result = std::filesystem::path(buffer);

        // Sempre libera, inclusive quando SHGetPathFromIDList falha: a lista
        // foi alocada de qualquer jeito.
        CoTaskMemFree(pidl);
        return result;
    }
#endif

    std::filesystem::path FileDialog::PickFolder(const char* title)
    {
        // `_WIN32` e nao `AXE_PLATFORM_WINDOWS`: e a guarda que o RESTO deste
        // arquivo usa, inclusive o #include de <ShlObj.h>.
#ifdef _WIN32
        // O clique que abriu este dialogo pode ter deixado o mouse capturado.
        ReleaseCapture();

        const std::wstring wtitle = Utf8ToWide(title);
        HWND owner = AxeOwnerWindow();

        // COINIT_APARTMENTTHREADED e o que os dialogos do shell querem.
        // S_FALSE = ja estava em STA (tambem serve). RPC_E_CHANGED_MODE = a
        // thread esta em MTA, e ai NAO da para usar nem o IFileDialog nem o
        // estilo novo do SHBrowseForFolder.
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool haveSTA = SUCCEEDED(hr);
        const bool weOwnCom = (hr == S_OK);   // S_FALSE = ja estava, nao e nosso

        // ═══════════════════════════════════════════════════════════════════
        //  PROJECT_NEW_V2e — a thread STA foi embora, e eis o porque
        //
        //  ── O DEADLOCK QUE EU CRIEI ────────────────────────────────────────
        //
        //  A tentativa anterior abria o IFileDialog numa thread STA dedicada e
        //  esperava com join(), passando como OWNER a janela da engine — que
        //  vive na thread PRINCIPAL.
        //
        //  Isso trava sempre, e por construcao: para exibir um modal, o shell
        //  manda mensagens SINCRONAS (SendMessage) para a janela dona,
        //  inclusive para desabilita-la. Essas mensagens so sao atendidas se a
        //  thread dona estiver bombeando — e ela estava parada dentro do
        //  join(), esperando o dialogo. Cada lado esperando o outro.
        //
        //  Tirar o owner evitaria o deadlock e traria de volta o defeito
        //  original: dialogo nascendo atras da janela.
        //
        //  ── O QUE FICA ─────────────────────────────────────────────────────
        //
        //  Nada de thread. O dialogo abre na MESMA thread que e dona da janela,
        //  que e a unica combinacao sem armadilha:
        //
        //    STA -> IFileDialog (o seletor moderno)
        //    MTA -> SHBrowseForFolderW sem BIF_NEWDIALOGSTYLE
        //
        //  O caminho MTA e o que roda nesta engine hoje, e e o dialogo antigo:
        //  sem botao "Nova pasta". Isso deixou de importar — a pasta do projeto
        //  e criada pelo NOME, e aqui so se escolhe onde ela vai nascer. Nao ha
        //  motivo para criar pasta nenhuma neste dialogo.
        //
        //  Perseguir o seletor bonito custou duas travadas. Ele nao valia isso.
        // ═══════════════════════════════════════════════════════════════════
        std::filesystem::path result;

        if (haveSTA)
            result = PickFolderModern(owner, wtitle.c_str());
        else
            result = PickFolderLegacy(owner, wtitle.c_str());

        if (weOwnCom)
            CoUninitialize();

        return result;
#else
        (void)title;
        return {};
#endif
    }

} // namespace axe