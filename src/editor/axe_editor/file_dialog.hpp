#pragma once
#include <string>
#include <filesystem>

namespace axe
{
    // Utilitário de file dialogs nativos.
    // Retorna o path selecionado, ou string vazia se cancelado.
    class FileDialog
    {
    public:
        // Abre dialog de "Abrir arquivo"
        // filter ex: "AXE Scene\0*.axescene\0All Files\0*.*\0"
        static std::filesystem::path Open(
            const char* filter = "All Files\0*.*\0",
            const char* title = "Abrir",
            const char* defaultExt = nullptr);

        // Abre dialog de "Salvar arquivo"
        static std::filesystem::path Save(
            const char* filter = "All Files\0*.*\0",
            const char* title = "Salvar",
            const char* defaultExt = nullptr);

        // PROJECT_NEW_V2 — escolher uma PASTA, nao um arquivo.
        //
        // O launcher ja tinha essa rotina, privada e duplicada dentro dele. O
        // dialogo de novo projeto do menu File precisava da mesma coisa, e uma
        // segunda copia de codigo Win32 seria a pior de todas para manter.
        // Caminho vazio = o usuario cancelou.
        static std::filesystem::path PickFolder(
            const char* title = "Selecione a pasta");
    };

} // namespace axe