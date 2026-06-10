#include "axe/script/script_asset.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/log/log.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

namespace axe
{
    using json = nlohmann::json;

    // ── ScriptComponentDef ────────────────────────────────────────────────────

    json ScriptComponentDef::Serialize() const
    {
        json j;
        j["type"] = Type;
        j["asset_uuid"] = AssetUUID;

        // Transform local
        j["pos"] = { PosX, PosY, PosZ };
        j["rot"] = { RotX, RotY, RotZ };
        j["scale"] = { ScaleX, ScaleY, ScaleZ };

        // Rigidbody
        j["body_type"] = BodyType;
        j["mass"] = Mass;
        j["friction"] = Friction;
        j["restitution"] = Restitution;
        j["linear_damping"] = LinearDamping;
        j["angular_damping"] = AngularDamping;
        j["use_gravity"] = UseGravity;
        j["lock_rot_x"] = LockRotX;
        j["lock_rot_y"] = LockRotY;
        j["lock_rot_z"] = LockRotZ;

        // Collider
        j["collider_shape"] = ColliderShape;
        j["collider_size"] = { ColliderSizeX, ColliderSizeY, ColliderSizeZ };
        j["collider_radius"] = ColliderRadius;
        j["collider_height"] = ColliderHeight;
        j["collider_capsule_radius"] = ColliderCapsuleRadius;
        j["collider_offset"] = { ColliderOffsetX, ColliderOffsetY, ColliderOffsetZ };
        j["is_trigger"] = IsTrigger;
        j["show_debug"] = ShowDebug;

        // CharacterController
        j["cc_height"] = CCHeight;
        j["cc_radius"] = CCRadius;
        j["cc_max_slope"] = CCMaxSlope;
        j["cc_step_height"] = CCStepHeight;
        j["cc_max_speed"] = CCMaxSpeed;
        j["cc_jump_force"] = CCJumpForce;

        return j;
    }

    void ScriptComponentDef::Deserialize(const json& j)
    {
        Type = j.value("type", "Mesh");
        AssetUUID = j.value("asset_uuid", "");

        // Transform local
        if (j.contains("pos") && j["pos"].size() == 3)
        {
            PosX = j["pos"][0]; PosY = j["pos"][1]; PosZ = j["pos"][2];
        }
        if (j.contains("rot") && j["rot"].size() == 3)
        {
            RotX = j["rot"][0]; RotY = j["rot"][1]; RotZ = j["rot"][2];
        }
        if (j.contains("scale") && j["scale"].size() == 3)
        {
            ScaleX = j["scale"][0]; ScaleY = j["scale"][1]; ScaleZ = j["scale"][2];
        }

        // Rigidbody
        BodyType = j.value("body_type", "Dynamic");
        Mass = j.value("mass", 1.f);
        Friction = j.value("friction", 0.5f);
        Restitution = j.value("restitution", 0.f);
        LinearDamping = j.value("linear_damping", 0.05f);
        AngularDamping = j.value("angular_damping", 0.05f);
        UseGravity = j.value("use_gravity", true);
        LockRotX = j.value("lock_rot_x", false);
        LockRotY = j.value("lock_rot_y", false);
        LockRotZ = j.value("lock_rot_z", false);

        // Collider
        ColliderShape = j.value("collider_shape", "Box");
        ColliderRadius = j.value("collider_radius", 1.f);
        ColliderHeight = j.value("collider_height", 1.8f);
        ColliderCapsuleRadius = j.value("collider_capsule_radius", 0.3f);
        IsTrigger = j.value("is_trigger", false);
        ShowDebug = j.value("show_debug", true);

        if (j.contains("collider_size") && j["collider_size"].size() == 3)
        {
            ColliderSizeX = j["collider_size"][0]; ColliderSizeY = j["collider_size"][1]; ColliderSizeZ = j["collider_size"][2];
        }
        if (j.contains("collider_offset") && j["collider_offset"].size() == 3)
        {
            ColliderOffsetX = j["collider_offset"][0]; ColliderOffsetY = j["collider_offset"][1]; ColliderOffsetZ = j["collider_offset"][2];
        }

        // CharacterController
        CCHeight = j.value("cc_height", 1.8f);
        CCRadius = j.value("cc_radius", 0.3f);
        CCMaxSlope = j.value("cc_max_slope", 45.f);
        CCStepHeight = j.value("cc_step_height", 0.3f);
        CCMaxSpeed = j.value("cc_max_speed", 5.f);
        CCJumpForce = j.value("cc_jump_force", 5.f);
    }

    // ── ScriptAsset ───────────────────────────────────────────────────────────

    void ScriptAsset::RemoveComponent(int index)
    {
        if (index >= 0 && index < (int)m_Components.size())
            m_Components.erase(m_Components.begin() + index);
    }

    bool ScriptAsset::Save(const std::filesystem::path& filepath)
    {
        m_FilePath = filepath;
        json root;
        root["name"] = m_Name;
        root["class_type"] = ScriptClassTypeToString(m_ClassType);
        root["dll_path"] = DllPath;
        root["compiled"] = IsCompiled;

        json comps = json::array();
        for (auto& c : m_Components) comps.push_back(c.Serialize());
        root["components"] = comps;
        root["graph"] = m_Graph->Serialize();

        std::ofstream f(filepath);
        if (!f.is_open())
        {
            AXE_CORE_ERROR("ScriptAsset: falha ao salvar {}", filepath.string()); return false;
        }
        f << root.dump(4);
        return true;
    }

    bool ScriptAsset::Load(const std::filesystem::path& filepath)
    {
        m_FilePath = filepath;
        std::ifstream f(filepath);
        if (!f.is_open())
        {
            AXE_CORE_ERROR("ScriptAsset: arquivo nao encontrado {}", filepath.string()); return false;
        }

        json root;
        try { root = json::parse(f); }
        catch (const json::exception& e)
        {
            AXE_CORE_ERROR("ScriptAsset: JSON invalido: {}", e.what()); return false;
        }

        m_Name = root.value("name", filepath.stem().string());
        m_ClassType = ScriptClassTypeFromString(root.value("class_type", "Entity"));
        DllPath = root.value("dll_path", "");
        IsCompiled = root.value("compiled", false);

        m_Components.clear();
        if (root.contains("components") && root["components"].is_array())
            for (auto& jc : root["components"])
            {
                ScriptComponentDef def; def.Deserialize(jc); m_Components.push_back(def);
            }

        if (root.contains("graph"))
            m_Graph->Deserialize(root["graph"]);

        return true;
    }

    std::shared_ptr<ScriptAsset> ScriptAsset::Create(const std::string& name, ScriptClassType type)
    {
        auto asset = std::make_shared<ScriptAsset>();
        asset->m_Name = name;
        asset->m_ClassType = type;

        switch (type)
        {
        case ScriptClassType::Entity:
        case ScriptClassType::Agent:
        {
            ScriptComponentDef mesh; mesh.Type = "Mesh";
            asset->m_Components.push_back(mesh);
            break;
        }
        case ScriptClassType::Character:
        {
            ScriptComponentDef mesh; mesh.Type = "Mesh";
            asset->m_Components.push_back(mesh);
            ScriptComponentDef cc;   cc.Type = "CharacterController";
            asset->m_Components.push_back(cc);
            break;
        }
        case ScriptClassType::StaticObject:
        {
            ScriptComponentDef mesh; mesh.Type = "Mesh";
            asset->m_Components.push_back(mesh);
            ScriptComponentDef col;  col.Type = "Collider"; col.ColliderShape = "Box";
            asset->m_Components.push_back(col);
            break;
        }
        case ScriptClassType::Trigger:
        {
            ScriptComponentDef col; col.Type = "Collider"; col.ColliderShape = "Box";
            col.IsTrigger = true;
            asset->m_Components.push_back(col);
            break;
        }
        }
        return asset;
    }

    std::shared_ptr<ScriptAsset> ScriptAsset::LoadFromFile(const std::filesystem::path& path)
    {
        auto asset = std::make_shared<ScriptAsset>();
        if (!asset->Load(path)) return nullptr;
        return asset;
    }

} // namespace axe