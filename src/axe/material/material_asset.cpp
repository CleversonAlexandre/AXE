#include "material_asset.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace axe
{

    using json = nlohmann::json;

    std::shared_ptr<MaterialAsset> MaterialAsset::Create(const std::string& name)
    {
        auto asset = std::make_shared<MaterialAsset>();
        asset->m_Name = name;
        asset->m_Material = std::make_shared<Material>(nullptr, name);
        asset->m_Material->UsePBR = true;
        return asset;
    }

    std::shared_ptr<MaterialAsset> MaterialAsset::LoadFromFile(const std::filesystem::path& filepath)
    {
        auto asset = std::make_shared<MaterialAsset>();
        if (!asset->Load(filepath))
            return nullptr;
        return asset;
    }

    bool MaterialAsset::Save(const std::filesystem::path& filepath) const
    {
        if (!m_Material) return false;

        json root;
        root["name"] = m_Name;
        root["version"] = "1.0";
        root["mode"] = m_Material->UsePBR ? "pbr" : "blinn_phong";

        root["parameters"]["metallic"] = m_Material->Metallic;
        root["parameters"]["roughness"] = m_Material->Roughness;
        root["parameters"]["ao"] = m_Material->AO;
        root["parameters"]["color"] = {
            m_Material->Color.r, m_Material->Color.g,
            m_Material->Color.b, m_Material->Color.a
        };
        root["parameters"]["specular_strength"] = m_Material->SpecularStrength;
        root["parameters"]["shininess"] = m_Material->Shininess;

        root["textures"]["albedo"] = m_Material->AlbedoUUID;
        root["textures"]["normal"] = m_Material->NormalUUID;
        root["textures"]["roughness"] = m_Material->RoughnessUUID;
        root["textures"]["metallic"] = m_Material->MetallicUUID;
        root["textures"]["ao"] = m_Material->AOUUID;

        std::filesystem::create_directories(filepath.parent_path());
        std::ofstream file(filepath);
        if (!file.is_open())
        {
            AXE_CORE_ERROR("MaterialAsset: falha ao salvar '{}'", filepath.string());
            return false;
        }

        file << root.dump(4);
        AXE_CORE_INFO("MaterialAsset: salvo em '{}'", filepath.string());
        return true;
    }

    bool MaterialAsset::Load(const std::filesystem::path& filepath)
    {
        if (!std::filesystem::exists(filepath))
        {
            AXE_CORE_ERROR("MaterialAsset: arquivo '{}' não encontrado.", filepath.string());
            return false;
        }

        std::ifstream file(filepath);
        json root;
        try { root = json::parse(file); }
        catch (const json::exception& e)
        {
            AXE_CORE_ERROR("MaterialAsset: erro ao parsear '{}': {}", filepath.string(), e.what());
            return false;
        }

        m_Name = root["name"].get<std::string>();
        m_FilePath = filepath;
        m_Material = std::make_shared<Material>(nullptr, m_Name);

        std::string mode = root["mode"].get<std::string>();
        m_Material->UsePBR = (mode == "pbr");

        auto& params = root["parameters"];
        m_Material->Metallic = params["metallic"];
        m_Material->Roughness = params["roughness"];
        m_Material->AO = params["ao"];
        m_Material->Color = {
            params["color"][0], params["color"][1],
            params["color"][2], params["color"][3]
        };
        m_Material->SpecularStrength = params["specular_strength"];
        m_Material->Shininess = params["shininess"];

        // Carrega texturas pelos UUIDs
        auto LoadTex = [&](const std::string& key, std::string& uuid,
            std::shared_ptr<Texture2D>& tex)
            {
                if (!root["textures"].contains(key)) return;
                uuid = root["textures"][key].get<std::string>();
                if (uuid.empty()) return;

                const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
                if (record && std::filesystem::exists(record->FilePath))
                    tex = Texture2D::Create(record->FilePath.string());
                else
                    AXE_CORE_WARN("MaterialAsset: textura '{}' não encontrada.", uuid);
            };

        LoadTex("albedo", m_Material->AlbedoUUID, m_Material->AlbedoMap);
        LoadTex("normal", m_Material->NormalUUID, m_Material->NormalMap);
        LoadTex("roughness", m_Material->RoughnessUUID, m_Material->RoughnessMap);
        LoadTex("metallic", m_Material->MetallicUUID, m_Material->MetallicMap);
        LoadTex("ao", m_Material->AOUUID, m_Material->AOMap);

        //AXE_CORE_INFO("MaterialAsset: carregado '{}'", m_Name);
        return true;
    }

} // namespace axe