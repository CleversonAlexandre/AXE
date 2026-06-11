#pragma once
#include "axe/core/types.hpp"
#include <string>
#include <filesystem>
#include <memory>

namespace axe
{
    // ── GameModeAsset (.axegamemode) ──────────────────────────────────────────
    // Define as regras do jogo: qual pawn o jogador controla e qual
    // GameController processa o input.
    // Criado no Asset Browser → Novo → Game Mode.
    class AXE_API GameModeAsset
    {
    public:
        std::string Name;

        // UUID do ScriptAsset do pawn padrão (Character/Agent)
        std::string DefaultPawnScriptUUID;

        // UUID do GameControllerAsset (futuro)
        // Vazio = sem GameController (pawn recebe input direto)
        std::string GameControllerUUID;

        // Cria um novo GameModeAsset vazio
        static std::shared_ptr<GameModeAsset> Create(const std::string& name);

        // Carrega de arquivo .axegamemode
        static std::shared_ptr<GameModeAsset> LoadFromFile(const std::filesystem::path& path);

        // Salva em arquivo .axegamemode
        bool Save(const std::filesystem::path& path) const;

        const std::string& GetName() const { return Name; }
        bool IsValid() const { return !Name.empty(); }
        bool HasDefaultPawn() const { return !DefaultPawnScriptUUID.empty(); }
    };

} // namespace axe