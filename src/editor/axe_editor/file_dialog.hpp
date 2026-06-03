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
    };

} // namespace axe
