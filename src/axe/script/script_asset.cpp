#include "axe/script/script_asset.hpp"
#include "axe/script/script_graph.hpp"
#include "axe/log/log.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

namespace axe
{

    // ─────────────────────────────────────────────────────────────────────────
    //  SC17 — ScriptValue
    // ─────────────────────────────────────────────────────────────────────────

    nlohmann::json ScriptValue::Serialize() const
    {
        nlohmann::json j = nlohmann::json::object();
        if (Bool)          j["b"] = Bool;
        if (Int != 0)      j["i"] = Int;
        if (Float != 0.0f) j["f"] = Float;
        if (!Str.empty())  j["s"] = Str;

        // O w nasce em 1 (quaternion identidade), entao o vetor "vazio" nao e
        // {0,0,0,0}. Comparar contra o default correto evita gravar um bloco
        // "v" em todo pin escalar do arquivo.
        if (Vec != glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))
            j["v"] = { Vec.x, Vec.y, Vec.z, Vec.w };

        return j;
    }

    void ScriptValue::Deserialize(const nlohmann::json& j)
    {
        if (!j.is_object()) return;
        Bool = j.value("b", false);
        Int = j.value("i", 0);
        Float = j.value("f", 0.0f);
        Str = j.value("s", std::string(""));
        if (j.contains("v") && j["v"].is_array() && j["v"].size() >= 4)
            Vec = { j["v"][0], j["v"][1], j["v"][2], j["v"][3] };
        else
            Vec = { 0.0f, 0.0f, 0.0f, 1.0f };
    }

    void ScriptValue::DeserializeLegacyPin(const nlohmann::json& jPin)
    {
        Float = jPin.value("default_float", 0.0f);
        Bool = jPin.value("default_bool", false);
        Int = jPin.value("default_int", 0);
        Str = jPin.value("default_string", std::string(""));

        // O formato antigo so tinha vec3; o w fica na identidade para que um
        // pin que vire Quat depois nao nasca com quaternion degenerado (0,0,0,0),
        // que normaliza para NaN na primeira operacao.
        if (jPin.contains("default_vec3") && jPin["default_vec3"].is_array() &&
            jPin["default_vec3"].size() >= 3)
        {
            Vec = { jPin["default_vec3"][0], jPin["default_vec3"][1],
                    jPin["default_vec3"][2], 1.0f };
        }
    }

    std::string ScriptValue::ToCppLiteral(ScriptVarType t) const
    {
        auto f = [](float v) { return std::to_string(v) + "f"; };

        switch (t)
        {
        case ScriptVarType::Float:  return f(Float);
        case ScriptVarType::Int:    return std::to_string(Int);
        case ScriptVarType::Bool:   return Bool ? "true" : "false";

        case ScriptVarType::String:
        {
            // Escapa o que quebraria o literal no .cpp gerado. Uma aspa dupla
            // digitada num Print String derrubava a compilacao com um erro
            // apontando para codigo que o autor nunca escreveu.
            std::string out;
            out.reserve(Str.size() + 2);
            for (char c : Str)
            {
                if (c == '\\' || c == '"') out += '\\';
                if (c == '\n') { out += "\\n"; continue; }
                if (c == '\r') { out += "\\r"; continue; }
                if (c == '\t') { out += "\\t"; continue; }
                out += c;
            }
            return "\"" + out + "\"";
        }

        case ScriptVarType::Vec2: return "glm::vec2(" + f(Vec.x) + ", " + f(Vec.y) + ")";
        case ScriptVarType::Vec3: return "glm::vec3(" + f(Vec.x) + ", " + f(Vec.y) + ", " + f(Vec.z) + ")";
        case ScriptVarType::Vec4: return "glm::vec4(" + f(Vec.x) + ", " + f(Vec.y) + ", " +
            f(Vec.z) + ", " + f(Vec.w) + ")";

            // glm::quat recebe (w, x, y, z) — nesta ordem, e nao xyzw. Trocar
            // isso da um quaternion silenciosamente errado: compila, roda, e a
            // rotacao sai torta sem nenhum erro em lugar nenhum.
        case ScriptVarType::Quat: return "glm::quat(" + f(Vec.w) + ", " + f(Vec.x) + ", " +
            f(Vec.y) + ", " + f(Vec.z) + ")";

            // "{}" sozinho nao e expressao valida em "x != entt::null".
        case ScriptVarType::Entity: return "entt::null";

        default: return "{}";
        }
    }

    using json = nlohmann::json;

    // ── ScriptComponentDef ────────────────────────────────────────────────────

    json ScriptComponentDef::Serialize() const
    {
        json j;
        j["type"] = Type;
        j["asset_uuid"] = AssetUUID;
        j["anim_graph_uuid"] = AnimGraphUUID;
        j["show_skeleton"] = ShowSkeleton;

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
        j["cc_orient_to_movement"] = CCOrientToMovement;
        j["cc_rotation_rate"] = CCRotationRate;
        j["cc_gravity"] = CCGravity;   // faltava: editar a gravidade nao persistia
        j["cc_show_debug"] = CCShowDebug;
        j["cc_offset"] = { CCOffsetX, CCOffsetY, CCOffsetZ };

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
        AnimGraphUUID = j.value("anim_graph_uuid", "");
        ShowSkeleton = j.value("show_skeleton", false);

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
        CCOrientToMovement = j.value("cc_orient_to_movement", false);
        CCRotationRate = j.value("cc_rotation_rate", 720.f);
        CCGravity = j.value("cc_gravity", CCGravity);
        CCShowDebug = j.value("cc_show_debug", true);

        if (j.contains("cc_offset") && j["cc_offset"].is_array() && j["cc_offset"].size() == 3)
        {
            CCOffsetX = j["cc_offset"][0].get<float>();
            CCOffsetY = j["cc_offset"][1].get<float>();
            CCOffsetZ = j["cc_offset"][2].get<float>();
        }

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
        if (index < 0 || index >= (int)m_Components.size())
            return;

        m_Components.erase(m_Components.begin() + index);

        // ── Reajuste dos ParentIndex ─────────────────────────────────────
        //
        // ParentIndex guarda o INDICE do pai no vector. Ao apagar um
        // componente, todos os indices depois dele deslocam — e quem nao
        // reajusta passa a apontar para o vizinho errado. Era isso que
        // fazia a Camera "pular" pra dentro do Material: ela apontava para
        // o indice que o SpringArm ocupava ANTES da remocao.
        //
        // Filho do removido volta a ser raiz (-1); quem apontava depois
        // dele anda uma casa pra tras.
        for (auto& c : m_Components)
        {
            if (c.ParentIndex == index)      c.ParentIndex = -1;
            else if (c.ParentIndex > index)  --c.ParentIndex;
        }
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
        // SC4 — dll_path nao e mais gravado: e derivado do local do projeto nesta
        // maquina, e ScriptPaths::ResolveDll o reconstroi. Gravar o absoluto
        // funcionava so ate mover a pasta ou abrir o projeto em outro PC.
        root["compiled"] = IsCompiled;

        // Transform RAIZ (painel Object do Script Editor) — antes so existia
        // na entidade do preview e sumia no Save.
        root["root_transform"] = {
            {"pos",   { RootPosX,   RootPosY,   RootPosZ   }},
            {"rot",   { RootRotX,   RootRotY,   RootRotZ   }},
            {"scale", { RootScaleX, RootScaleY, RootScaleZ }},
        };

        json comps = json::array();
        for (auto& c : m_Components) comps.push_back(c.Serialize());
        root["components"] = comps;

        json vars = json::array();
        for (auto& v : m_Variables)
        {
            json jv;
            jv["name"] = v.Name;
            jv["type"] = ScriptVarTypeToString(v.Type);
            // SC30 — so grava quando ha qualificador, para nao poluir toda
            // variavel escalar do arquivo com uma chave vazia.
            if (!v.TypeQualifier.empty()) jv["type_qualifier"] = v.TypeQualifier;
            // SC19 — um bloco "default" no lugar de f/b/i/v3/v2/v4/vq/s.
            jv["default"] = v.Default.Serialize();
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
        // dll_path de arquivo antigo e descartado de proposito (ver Save).
        DllPath.clear();
        IsCompiled = root.value("compiled", false);

        if (root.contains("root_transform"))
        {
            const auto& rt = root["root_transform"];

            auto rd3 = [&rt](const char* key, float& a, float& b, float& c)
                {
                    if (rt.contains(key) && rt[key].is_array() && rt[key].size() == 3)
                    {
                        a = rt[key][0].get<float>();
                        b = rt[key][1].get<float>();
                        c = rt[key][2].get<float>();
                    }
                };

            rd3("pos", RootPosX, RootPosY, RootPosZ);
            rd3("rot", RootRotX, RootRotY, RootRotZ);
            rd3("scale", RootScaleX, RootScaleY, RootScaleZ);
        }

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
                v.TypeQualifier = jv.value("type_qualifier", std::string(""));
                // SC19 — formato novo se existir; senao le os campos soltos
                // do formato anterior. Todo .axescript gravado antes deste
                // patch cai no segundo ramo, uma vez.
                if (jv.contains("default"))
                {
                    v.Default.Deserialize(jv["default"]);
                }
                else
                {
                    v.Default.Float = jv.value("f", 0.f);
                    v.Default.Bool = jv.value("b", false);
                    v.Default.Int = jv.value("i", 0);
                    v.Default.Str = jv.value("s", std::string(""));

                    // Havia QUATRO blocos separados (v3/v2/v4/vq) para um
                    // vetor so. Qual deles vale depende do tipo da variavel —
                    // ler todos em sequencia deixaria o ultimo sobrescrever os
                    // anteriores e um Vec3 abriria com o valor do Quat.
                    auto readVec = [&](const char* key, int n, float wDefault)
                        {
                            if (!jv.contains(key) || !jv[key].is_array()) return false;
                            const auto& a = jv[key];
                            if ((int)a.size() < n) return false;
                            v.Default.Vec = { 0.f, 0.f, 0.f, wDefault };
                            float* c = &v.Default.Vec.x;
                            for (int k = 0; k < n; k++) c[k] = a[k];
                            return true;
                        };

                    switch (v.Type)
                    {
                    case ScriptVarType::Vec2: case ScriptVarType::Vec2Array:
                        readVec("v2", 2, 1.f); break;
                    case ScriptVarType::Vec4: case ScriptVarType::Vec4Array:
                        readVec("v4", 4, 1.f); break;
                    case ScriptVarType::Quat: case ScriptVarType::QuatArray:
                        readVec("vq", 4, 1.f); break;
                    default:
                        readVec("v3", 3, 1.f); break;
                    }
                }
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

    // Resumo legivel dos componentes — usado no log de Save/Load para
    // responder de olho no console as duas perguntas que codigo nenhum
    // responde daqui: "gravou no arquivo QUE EU acho?" e "gravou o VALOR
    // que eu acabei de marcar?".
    static std::string DescribeComponents(const std::vector<ScriptComponentDef>& comps)
    {
        std::string out;

        for (const auto& c : comps)
        {
            if (!out.empty()) out += ", ";

            out += c.Type;

            if (c.Type == "CharacterController")
                out += "(orient=" + std::string(c.CCOrientToMovement ? "1" : "0")
                + " rate=" + std::to_string((int)c.CCRotationRate) + ")";
            else if (c.Type == "SkeletalMesh")
                out += "(skel=" + (c.AssetUUID.empty() ? std::string("-") : c.AssetUUID.substr(0, 8))
                + " graph=" + (c.AnimGraphUUID.empty() ? std::string("-") : c.AnimGraphUUID.substr(0, 8)) + ")";
        }

        return out.empty() ? "(nenhum)" : out;
    }

    bool ScriptAsset::Save(const std::filesystem::path& filepath)
    {
        m_FilePath = filepath;

        {
            std::error_code ec;
            AXE_CORE_INFO("[SCRIPT_IO_V1] Save -> {} | {}",
                std::filesystem::absolute(filepath, ec).lexically_normal().string(),
                DescribeComponents(m_Components));
        }
        json root;
        root["name"] = m_Name;
        root["class_type"] = ScriptClassTypeToString(m_ClassType);
        // SC4 — dll_path nao e mais gravado: e derivado do local do projeto nesta
        // maquina, e ScriptPaths::ResolveDll o reconstroi. Gravar o absoluto
        // funcionava so ate mover a pasta ou abrir o projeto em outro PC.
        root["compiled"] = IsCompiled;

        // Transform RAIZ (painel Object do Script Editor) — antes so existia
        // na entidade do preview e sumia no Save.
        root["root_transform"] = {
            {"pos",   { RootPosX,   RootPosY,   RootPosZ   }},
            {"rot",   { RootRotX,   RootRotY,   RootRotZ   }},
            {"scale", { RootScaleX, RootScaleY, RootScaleZ }},
        };

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
            // SC30 — so grava quando ha qualificador, para nao poluir toda
            // variavel escalar do arquivo com uma chave vazia.
            if (!v.TypeQualifier.empty()) jv["type_qualifier"] = v.TypeQualifier;
            // SC19 — um bloco "default" no lugar de f/b/i/v3/v2/v4/vq/s.
            jv["default"] = v.Default.Serialize();
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
        // dll_path de arquivo antigo e descartado de proposito (ver Save).
        DllPath.clear();
        IsCompiled = root.value("compiled", false);

        if (root.contains("root_transform"))
        {
            const auto& rt = root["root_transform"];

            auto rd3 = [&rt](const char* key, float& a, float& b, float& c)
                {
                    if (rt.contains(key) && rt[key].is_array() && rt[key].size() == 3)
                    {
                        a = rt[key][0].get<float>();
                        b = rt[key][1].get<float>();
                        c = rt[key][2].get<float>();
                    }
                };

            rd3("pos", RootPosX, RootPosY, RootPosZ);
            rd3("rot", RootRotX, RootRotY, RootRotZ);
            rd3("scale", RootScaleX, RootScaleY, RootScaleZ);
        }

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
                v.TypeQualifier = jv.value("type_qualifier", std::string(""));
                // SC19 — formato novo se existir; senao le os campos soltos
                // do formato anterior. Todo .axescript gravado antes deste
                // patch cai no segundo ramo, uma vez.
                if (jv.contains("default"))
                {
                    v.Default.Deserialize(jv["default"]);
                }
                else
                {
                    v.Default.Float = jv.value("f", 0.f);
                    v.Default.Bool = jv.value("b", false);
                    v.Default.Int = jv.value("i", 0);
                    v.Default.Str = jv.value("s", std::string(""));

                    // Havia QUATRO blocos separados (v3/v2/v4/vq) para um
                    // vetor so. Qual deles vale depende do tipo da variavel —
                    // ler todos em sequencia deixaria o ultimo sobrescrever os
                    // anteriores e um Vec3 abriria com o valor do Quat.
                    auto readVec = [&](const char* key, int n, float wDefault)
                        {
                            if (!jv.contains(key) || !jv[key].is_array()) return false;
                            const auto& a = jv[key];
                            if ((int)a.size() < n) return false;
                            v.Default.Vec = { 0.f, 0.f, 0.f, wDefault };
                            float* c = &v.Default.Vec.x;
                            for (int k = 0; k < n; k++) c[k] = a[k];
                            return true;
                        };

                    switch (v.Type)
                    {
                    case ScriptVarType::Vec2: case ScriptVarType::Vec2Array:
                        readVec("v2", 2, 1.f); break;
                    case ScriptVarType::Vec4: case ScriptVarType::Vec4Array:
                        readVec("v4", 4, 1.f); break;
                    case ScriptVarType::Quat: case ScriptVarType::QuatArray:
                        readVec("vq", 4, 1.f); break;
                    default:
                        readVec("v3", 3, 1.f); break;
                    }
                }
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

        {
            std::error_code ec;
            AXE_CORE_INFO("[SCRIPT_IO_V1] Load <- {} | {}",
                std::filesystem::absolute(filepath, ec).lexically_normal().string(),
                DescribeComponents(m_Components));
        }

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