#include "game_mode_asset.hpp"
#include "axe/log/log.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace axe
{
    using json = nlohmann::json;

    std::shared_ptr<GameModeAsset> GameModeAsset::Create(const std::string& name)
    {
        auto asset = std::make_shared<GameModeAsset>();
        asset->Name = name;
        return asset;
    }

    std::shared_ptr<GameModeAsset> GameModeAsset::LoadFromFile(const std::filesystem::path& path)
    {
        std::ifstream f(path);
        if (!f.is_open())
        {
            AXE_CORE_ERROR("GameModeAsset: arquivo '{}' não encontrado.", path.string());
            return nullptr;
        }

        try
        {
            json j = json::parse(f);
            auto asset = std::make_shared<GameModeAsset>();
            asset->Name = j.value("name", path.stem().string());
            asset->DefaultPawnScriptUUID = j.value("default_pawn_uuid", "");
            asset->GameControllerUUID = j.value("game_controller_uuid", "");
            return asset;
        }
        catch (const std::exception& e)
        {
            AXE_CORE_ERROR("GameModeAsset: erro ao ler '{}': {}", path.string(), e.what());
            return nullptr;
        }
    }

    bool GameModeAsset::Save(const std::filesystem::path& path) const
    {
        json j;
        j["name"] = Name;
        j["default_pawn_uuid"] = DefaultPawnScriptUUID;
        j["game_controller_uuid"] = GameControllerUUID;

        std::ofstream f(path);
        if (!f.is_open())
        {
            AXE_CORE_ERROR("GameModeAsset: não foi possível escrever '{}'.", path.string());
            return false;
        }
        f << j.dump(4);
        AXE_CORE_INFO("GameModeAsset: '{}' salvo.", path.string());
        return true;
    }

} // namespace axe