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

        // SpringArm
        j["sa_length"] = SALength;
        j["sa_height_offset"] = SAHeightOffset;
        j["sa_socket_off"] = { SASocketOffX, SASocketOffY, SASocketOffZ };
        j["sa_lag_speed"] = SALagSpeed;
        j["sa_enable_lag"] = SAEnableLag;
        j["sa_mouse_rotates"] = SAMouseRotates;

        // Camera
        j["cam_fov"] = CamFov;
        j["cam_near"] = CamNearClip;
        j["cam_far"] = CamFarClip;
        j["cam_sensitivity"] = CamSensitivity;
        j["cam_is_primary"] = CamIsPrimary;

        // Hierarquia
        j["parent_index"] = ParentIndex;

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

        // SpringArm
        SALength = j.value("sa_length", 300.0f);
        SAHeightOffset = j.value("sa_height_offset", 0.0f);
        SALagSpeed = j.value("sa_lag_speed", 8.0f);
        SAEnableLag = j.value("sa_enable_lag", true);
        SAMouseRotates = j.value("sa_mouse_rotates", true);
        if (j.contains("sa_socket_off") && j["sa_socket_off"].size() == 3)
        {
            SASocketOffX = j["sa_socket_off"][0];
            SASocketOffY = j["sa_socket_off"][1];
            SASocketOffZ = j["sa_socket_off"][2];
        }

        // Camera
        CamFov = j.value("cam_fov", 60.0f);
        CamNearClip = j.value("cam_near", 0.1f);
        CamFarClip = j.value("cam_far", 1000.0f);
        CamSensitivity = j.value("cam_sensitivity", 0.1f);
        CamIsPrimary = j.value("cam_is_primary", true);

        // Hierarquia
        ParentIndex = j.value("parent_index", -1);
    }

    // ── ScriptFunction (serialização) ────────────────────────────────────────
    // Helpers únicos reutilizados nos 4 pontos de serialização do asset
    // (Save/Load/SaveToString/LoadFromString) — função tem um grafo aninhado
    // (mais complexo que os campos simples de ScriptVariable), então duplicar
    // isso 4x à mão seria bem mais arriscado do que o padrão já duplicado
    // usado pras variáveis.
    static json SerializeScriptFunction(const ScriptFunction& f)
    {
        json jf;
        jf["name"] = f.Name;
        json ins = json::array();
        for (auto& p : f.Inputs) ins.push_back(json{ {"name", p.Name}, {"type", ScriptVarTypeToString(p.Type)} });
        jf["inputs"] = ins;
        json outs = json::array();
        for (auto& p : f.Outputs) outs.push_back(json{ {"name", p.Name}, {"type", ScriptVarTypeToString(p.Type)} });
        jf["outputs"] = outs;
        jf["graph"] = f.Graph->Serialize();
        return jf;
    }

    static ScriptFunction DeserializeScriptFunction(const json& jf)
    {
        ScriptFunction f;
        f.Name = jf.value("name", "NewFunction");
        if (jf.contains("inputs") && jf["inputs"].is_array())
            for (auto& ji : jf["inputs"])
                f.Inputs.push_back({ ji.value("name", "Param"), ScriptVarTypeFromString(ji.value("type", "Float")) });
        if (jf.contains("outputs") && jf["outputs"].is_array())
            for (auto& jo : jf["outputs"])
                f.Outputs.push_back({ jo.value("name", "Param"), ScriptVarTypeFromString(jo.value("type", "Float")) });
        f.Graph = std::make_shared<ScriptGraph>();
        if (jf.contains("graph")) f.Graph->Deserialize(jf["graph"]);
        return f;
    }

    // ── ScriptAsset ───────────────────────────────────────────────────────────

    void ScriptAsset::RemoveComponent(int index)
    {
        if (index >= 0 && index < (int)m_Components.size())
            m_Components.erase(m_Components.begin() + index);
    }

    ScriptFunction* ScriptAsset::AddFunction(const std::string& name)
    {
        ScriptFunction f;
        f.Name = name;
        f.Graph = std::make_shared<ScriptGraph>();
        // Toda função nasce com exatamente um Entry e um Return — não dá pra
        // adicionar outro de cada pelo catálogo do editor (são auto-geridos),
        // mantendo o codegen simples: 1 ponto de entrada, 1 ponto de saída.
        f.Graph->AddNode("FunctionEntry");
        f.Graph->AddNode("ReturnNode");
        m_Functions.push_back(std::move(f));
        return &m_Functions.back();
    }

    ScriptFunction* ScriptAsset::FindFunction(const std::string& name)
    {
        for (auto& f : m_Functions) if (f.Name == name) return &f;
        return nullptr;
    }

    std::string ScriptAsset::SaveToString()
    {
        // Reuse Save logic but write to string instead of file
        json root;
        root["name"] = m_Name;
        root["class_type"] = ScriptClassTypeToString(m_ClassType);
        root["dll_path"] = DllPath;
        root["compiled"] = IsCompiled;

        json comps = json::array();
        for (auto& c : m_Components) comps.push_back(c.Serialize());
        root["components"] = comps;

        json vars = json::array();
        for (auto& v : m_Variables)
        {
            json jv;
            jv["name"] = v.Name;
            jv["type"] = ScriptVarTypeToString(v.Type);
            jv["f"] = v.DefaultFloat;
            jv["b"] = v.DefaultBool;
            jv["i"] = v.DefaultInt;
            jv["v3"] = { v.DefaultVec3[0], v.DefaultVec3[1], v.DefaultVec3[2] };
            jv["v2"] = { v.DefaultVec2[0], v.DefaultVec2[1] };
            jv["v4"] = { v.DefaultVec4[0], v.DefaultVec4[1], v.DefaultVec4[2], v.DefaultVec4[3] };
            jv["vq"] = { v.DefaultQuat[0], v.DefaultQuat[1], v.DefaultQuat[2], v.DefaultQuat[3] };
            jv["s"] = v.DefaultString;
            jv["cat"] = v.Category;
            jv["desc"] = v.Description;
            jv["exposed"] = v.Exposed;
            vars.push_back(jv);
        }
        root["variables"] = vars;

        json evts = json::array();
        for (auto& e : m_CustomEvents) evts.push_back(json{ {"name", e.Name} });
        root["custom_events"] = evts;

        json funcs = json::array();
        for (auto& f : m_Functions) funcs.push_back(SerializeScriptFunction(f));
        root["functions"] = funcs;

        root["graph"] = m_Graph->Serialize();
        return root.dump();
    }

    bool ScriptAsset::LoadFromString(const std::string& jsonStr)
    {
        json root;
        try { root = json::parse(jsonStr); }
        catch (...) { return false; }

        m_Name = root.value("name", m_Name);
        m_ClassType = ScriptClassTypeFromString(root.value("class_type", "Entity"));
        DllPath = root.value("dll_path", "");
        IsCompiled = root.value("compiled", false);

        m_Components.clear();
        if (root.contains("components") && root["components"].is_array())
            for (auto& jc : root["components"])
            {
                ScriptComponentDef def; def.Deserialize(jc); m_Components.push_back(def);
            }

        m_Variables.clear();
        if (root.contains("variables") && root["variables"].is_array())
            for (auto& jv : root["variables"])
            {
                ScriptVariable v;
                v.Name = jv.value("name", "NewVar");
                v.Type = ScriptVarTypeFromString(jv.value("type", "Float"));
                v.DefaultFloat = jv.value("f", 0.f);
                v.DefaultBool = jv.value("b", false);
                v.DefaultInt = jv.value("i", 0);
                if (jv.contains("v3") && jv["v3"].is_array() && jv["v3"].size() == 3)
                {
                    v.DefaultVec3[0] = jv["v3"][0]; v.DefaultVec3[1] = jv["v3"][1]; v.DefaultVec3[2] = jv["v3"][2];
                    if (jv.contains("v2") && jv["v2"].is_array() && jv["v2"].size() >= 2)
                    {
                        v.DefaultVec2[0] = jv["v2"][0]; v.DefaultVec2[1] = jv["v2"][1];
                    }
                    if (jv.contains("v4") && jv["v4"].is_array() && jv["v4"].size() >= 4)
                    {
                        v.DefaultVec4[0] = jv["v4"][0]; v.DefaultVec4[1] = jv["v4"][1]; v.DefaultVec4[2] = jv["v4"][2]; v.DefaultVec4[3] = jv["v4"][3];
                    }
                    if (jv.contains("vq") && jv["vq"].is_array() && jv["vq"].size() >= 4)
                    {
                        v.DefaultQuat[0] = jv["vq"][0]; v.DefaultQuat[1] = jv["vq"][1]; v.DefaultQuat[2] = jv["vq"][2]; v.DefaultQuat[3] = jv["vq"][3];
                    }
                }
                v.DefaultString = jv.value("s", "");
                v.Category = jv.value("cat", "");
                v.Description = jv.value("desc", "");
                v.Exposed = jv.value("exposed", false);
                m_Variables.push_back(v);
            }

        m_CustomEvents.clear();
        if (root.contains("custom_events") && root["custom_events"].is_array())
            for (auto& je : root["custom_events"])
                m_CustomEvents.push_back({ je.value("name", "OnMyEvent") });

        m_Functions.clear();
        if (root.contains("functions") && root["functions"].is_array())
            for (auto& jf : root["functions"])
                m_Functions.push_back(DeserializeScriptFunction(jf));

        m_Graph = std::make_shared<ScriptGraph>();
        if (root.contains("graph")) m_Graph->Deserialize(root["graph"]);
        return true;
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

        // Variables
        json vars = json::array();
        for (auto& v : m_Variables)
        {
            json jv;
            jv["name"] = v.Name;
            jv["type"] = ScriptVarTypeToString(v.Type);
            jv["f"] = v.DefaultFloat;
            jv["b"] = v.DefaultBool;
            jv["i"] = v.DefaultInt;
            jv["v3"] = { v.DefaultVec3[0], v.DefaultVec3[1], v.DefaultVec3[2] };
            jv["v2"] = { v.DefaultVec2[0], v.DefaultVec2[1] };
            jv["v4"] = { v.DefaultVec4[0], v.DefaultVec4[1], v.DefaultVec4[2], v.DefaultVec4[3] };
            jv["vq"] = { v.DefaultQuat[0], v.DefaultQuat[1], v.DefaultQuat[2], v.DefaultQuat[3] };
            jv["s"] = v.DefaultString;
            jv["cat"] = v.Category;
            jv["desc"] = v.Description;
            jv["exposed"] = v.Exposed;
            vars.push_back(jv);
        }
        root["variables"] = vars;

        // Custom Events
        json evts = json::array();
        for (auto& e : m_CustomEvents) evts.push_back(json{ {"name", e.Name} });
        root["custom_events"] = evts;

        json funcs = json::array();
        for (auto& fn : m_Functions) funcs.push_back(SerializeScriptFunction(fn));
        root["functions"] = funcs;

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

        // Variables
        m_Variables.clear();
        if (root.contains("variables") && root["variables"].is_array())
            for (auto& jv : root["variables"])
            {
                ScriptVariable v;
                v.Name = jv.value("name", "NewVar");
                v.Type = ScriptVarTypeFromString(jv.value("type", "Float"));
                v.DefaultFloat = jv.value("f", 0.f);
                v.DefaultBool = jv.value("b", false);
                v.DefaultInt = jv.value("i", 0);
                if (jv.contains("v3") && jv["v3"].is_array() && jv["v3"].size() == 3)
                {
                    v.DefaultVec3[0] = jv["v3"][0]; v.DefaultVec3[1] = jv["v3"][1]; v.DefaultVec3[2] = jv["v3"][2];
                    if (jv.contains("v2") && jv["v2"].is_array() && jv["v2"].size() >= 2)
                    {
                        v.DefaultVec2[0] = jv["v2"][0]; v.DefaultVec2[1] = jv["v2"][1];
                    }
                    if (jv.contains("v4") && jv["v4"].is_array() && jv["v4"].size() >= 4)
                    {
                        v.DefaultVec4[0] = jv["v4"][0]; v.DefaultVec4[1] = jv["v4"][1]; v.DefaultVec4[2] = jv["v4"][2]; v.DefaultVec4[3] = jv["v4"][3];
                    }
                    if (jv.contains("vq") && jv["vq"].is_array() && jv["vq"].size() >= 4)
                    {
                        v.DefaultQuat[0] = jv["vq"][0]; v.DefaultQuat[1] = jv["vq"][1]; v.DefaultQuat[2] = jv["vq"][2]; v.DefaultQuat[3] = jv["vq"][3];
                    }
                }
                v.DefaultString = jv.value("s", "");
                v.Category = jv.value("cat", "");
                v.Description = jv.value("desc", "");
                m_Variables.push_back(v);
            }

        // Custom Events
        m_CustomEvents.clear();
        if (root.contains("custom_events") && root["custom_events"].is_array())
            for (auto& je : root["custom_events"])
                m_CustomEvents.push_back({ je.value("name", "OnMyEvent") });

        // Functions
        m_Functions.clear();
        if (root.contains("functions") && root["functions"].is_array())
            for (auto& jf : root["functions"])
                m_Functions.push_back(DeserializeScriptFunction(jf));

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