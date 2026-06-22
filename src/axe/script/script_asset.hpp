#pragma once
#include "axe/core/types.hpp"
#include "axe/asset/asset.hpp"
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <nlohmann/json.hpp>

// Forward declaration — evita include circular com imgui_node_editor
namespace axe { class ScriptGraph; }

namespace axe
{
    // ── Tipo de Script — define os componentes default criados ────────────────
    enum class ScriptClassType
    {
        Entity,       // Objeto genérico com Transform
        Agent,        // Controlável pelo player/AI (sem CharacterController)
        Character,    // Agent + CharacterController automático
        StaticObject, // Só visual, sem física
        Trigger,      // Colisão invisível, dispara eventos
    };

    static std::string ScriptClassTypeToString(ScriptClassType t)
    {
        switch (t)
        {
        case ScriptClassType::Entity:       return "Entity";
        case ScriptClassType::Agent:        return "Agent";
        case ScriptClassType::Character:    return "Character";
        case ScriptClassType::StaticObject: return "StaticObject";
        case ScriptClassType::Trigger:      return "Trigger";
        default: return "Entity";
        }
    }

    static ScriptClassType ScriptClassTypeFromString(const std::string& s)
    {
        if (s == "Agent")        return ScriptClassType::Agent;
        if (s == "Character")    return ScriptClassType::Character;
        if (s == "StaticObject") return ScriptClassType::StaticObject;
        if (s == "Trigger")      return ScriptClassType::Trigger;
        return ScriptClassType::Entity;
    }

    // ── Componente definido no Script Asset ───────────────────────────────────
    struct ScriptComponentDef
    {
        std::string Type;       // "Mesh", "Material", "Rigidbody", "Collider", etc.

        // Mesh / Material
        std::string AssetUUID;  // UUID do asset referenciado

        // Rigidbody
        std::string BodyType = "Dynamic"; // "Static", "Dynamic", "Kinematic"

        // Collider
        std::string ColliderShape = "Box";
        float       ColliderSizeX = 0.5f;
        float       ColliderSizeY = 0.5f;
        float       ColliderSizeZ = 0.5f;
        float       ColliderRadius = 1.0f;
        float       ColliderHeight = 1.8f;
        float       ColliderCapsuleRadius = 0.3f;
        float       ColliderOffsetX = 0.f;
        float       ColliderOffsetY = 0.f;
        float       ColliderOffsetZ = 0.f;
        bool        IsTrigger = false;
        bool        ShowDebug = true;   // wireframe visível no preview

        // Rigidbody extra
        float Mass = 1.f;
        float Friction = 0.5f;
        float Restitution = 0.f;
        float LinearDamping = 0.05f;
        float AngularDamping = 0.05f;
        bool  UseGravity = true;
        bool  LockRotX = false;
        bool  LockRotY = false;
        bool  LockRotZ = false;

        // CharacterController
        float CCHeight = 1.8f;
        float CCRadius = 0.3f;
        float CCMaxSlope = 45.f;
        float CCStepHeight = 0.3f;
        float CCMaxSpeed = 5.f;
        float CCJumpForce = 5.f;

        // CharacterController extra
        bool  CCGravity = true;

        // ── SpringArm ─────────────────────────────────────────────────────────
        float SALength = 300.0f; // comprimento do braço (unidades)
        float SAHeightOffset = 0.0f;  // offset vertical relativo ao pawn
        float SASocketOffX = 0.0f;
        float SASocketOffY = 0.0f;
        float SASocketOffZ = 0.0f;
        float SALagSpeed = 8.0f;
        bool  SAEnableLag = true;
        bool  SAMouseRotates = true;

        // ── Camera ────────────────────────────────────────────────────────────
        float CamFov = 60.0f;
        float CamNearClip = 0.1f;
        float CamFarClip = 1000.0f;
        float CamSensitivity = 0.1f;
        bool  CamIsPrimary = true;

        // ── Hierarquia — índice do componente pai (-1 = raiz) ─────────────────
        int   ParentIndex = -1;

        // Transform local do componente
        float PosX = 0, PosY = 0, PosZ = 0;
        float RotX = 0, RotY = 0, RotZ = 0;
        float ScaleX = 1, ScaleY = 1, ScaleZ = 1;

        nlohmann::json Serialize() const;
        void           Deserialize(const nlohmann::json& j);
    };

    // ── Variable — membro do script acessível no grafo ───────────────────────
    // Os 9 primeiros são escalares; os 9 últimos são as versões "Array" de cada
    // um (ex.: FloatArray é um array de Float). Mantidos como tipos distintos no
    // enum (em vez de uma flag IsArray separada) para que o switch/case já
    // existente em código mais antigo continue exaustivo e force atualização
    // em tempo de compilação sempre que algo novo for adicionado aqui.
    enum class ScriptVarType {
        Float, Bool, Int, Vec3, String, Vec2, Vec4, Quat, Entity,
        FloatArray, BoolArray, IntArray, Vec3Array, StringArray,
        Vec2Array, Vec4Array, QuatArray, EntityArray,
    };

    // BUGFIX: as 3 funções abaixo faziam ARITMÉTICA com os valores do enum
    // (offset = (int)t - (int)FloatArray), assumindo que os 9 tipos
    // escalares (Float..Entity) e os 9 tipos array (FloatArray..EntityArray)
    // ficam na MESMA ORDEM RELATIVA dentro de cada grupo. Funciona hoje
    // porque essa ordem paralela está certa, mas é uma armadilha silenciosa:
    // inserir um tipo escalar novo sem inserir o array correspondente na
    // posição relativa certa (ou vice-versa) quebraria isso sem nenhum erro
    // de compilação. Reescrito como switch explícito — sem depender de
    // posição nenhuma, e exaustivo o suficiente pra avisar (via -Wswitch)
    // se um tipo novo for adicionado ao enum e esquecido aqui.

    inline bool IsArrayType(ScriptVarType t)
    {
        switch (t)
        {
        case ScriptVarType::Float: case ScriptVarType::Bool: case ScriptVarType::Int:
        case ScriptVarType::Vec3:  case ScriptVarType::String: case ScriptVarType::Vec2:
        case ScriptVarType::Vec4:  case ScriptVarType::Quat:  case ScriptVarType::Entity:
            return false;
        case ScriptVarType::FloatArray: case ScriptVarType::BoolArray: case ScriptVarType::IntArray:
        case ScriptVarType::Vec3Array:  case ScriptVarType::StringArray: case ScriptVarType::Vec2Array:
        case ScriptVarType::Vec4Array:  case ScriptVarType::QuatArray: case ScriptVarType::EntityArray:
            return true;
        }
        return false;
    }

    // Para um tipo Array, retorna o tipo escalar correspondente (ex.:
    // Vec3Array -> Vec3). Para um tipo já escalar, retorna ele mesmo
    // (idempotente — seguro de chamar sem checar IsArrayType antes).
    inline ScriptVarType GetElementType(ScriptVarType t)
    {
        switch (t)
        {
        case ScriptVarType::FloatArray:  return ScriptVarType::Float;
        case ScriptVarType::BoolArray:   return ScriptVarType::Bool;
        case ScriptVarType::IntArray:    return ScriptVarType::Int;
        case ScriptVarType::Vec3Array:   return ScriptVarType::Vec3;
        case ScriptVarType::StringArray: return ScriptVarType::String;
        case ScriptVarType::Vec2Array:   return ScriptVarType::Vec2;
        case ScriptVarType::Vec4Array:   return ScriptVarType::Vec4;
        case ScriptVarType::QuatArray:   return ScriptVarType::Quat;
        case ScriptVarType::EntityArray: return ScriptVarType::Entity;
            // Já escalar — idempotente, retorna ele mesmo
        case ScriptVarType::Float: case ScriptVarType::Bool: case ScriptVarType::Int:
        case ScriptVarType::Vec3:  case ScriptVarType::String: case ScriptVarType::Vec2:
        case ScriptVarType::Vec4:  case ScriptVarType::Quat:  case ScriptVarType::Entity:
            return t;
        }
        return t;
    }

    // Inverso de GetElementType — para um tipo escalar, retorna a versão Array
    // correspondente (ex.: Vec3 -> Vec3Array). Chamar com um tipo já-Array
    // retorna ele mesmo sem alterar (idempotente).
    inline ScriptVarType GetArrayType(ScriptVarType t)
    {
        switch (t)
        {
        case ScriptVarType::Float:  return ScriptVarType::FloatArray;
        case ScriptVarType::Bool:   return ScriptVarType::BoolArray;
        case ScriptVarType::Int:    return ScriptVarType::IntArray;
        case ScriptVarType::Vec3:   return ScriptVarType::Vec3Array;
        case ScriptVarType::String: return ScriptVarType::StringArray;
        case ScriptVarType::Vec2:   return ScriptVarType::Vec2Array;
        case ScriptVarType::Vec4:   return ScriptVarType::Vec4Array;
        case ScriptVarType::Quat:   return ScriptVarType::QuatArray;
        case ScriptVarType::Entity: return ScriptVarType::EntityArray;
            // Já é array — idempotente, retorna ele mesmo
        case ScriptVarType::FloatArray: case ScriptVarType::BoolArray: case ScriptVarType::IntArray:
        case ScriptVarType::Vec3Array:  case ScriptVarType::StringArray: case ScriptVarType::Vec2Array:
        case ScriptVarType::Vec4Array:  case ScriptVarType::QuatArray: case ScriptVarType::EntityArray:
            return t;
        }
        return t;
    }

    static std::string ScriptVarTypeToString(ScriptVarType t)
    {
        switch (t) {
        case ScriptVarType::Float:  return "Float";
        case ScriptVarType::Bool:   return "Bool";
        case ScriptVarType::Int:    return "Int";
        case ScriptVarType::Vec3:   return "Vec3";
        case ScriptVarType::String: return "String";
        case ScriptVarType::Vec2:   return "Vec2";
        case ScriptVarType::Vec4:   return "Vec4";
        case ScriptVarType::Quat:   return "Quat";
        case ScriptVarType::Entity: return "Entity";
        case ScriptVarType::FloatArray:  return "FloatArray";
        case ScriptVarType::BoolArray:   return "BoolArray";
        case ScriptVarType::IntArray:    return "IntArray";
        case ScriptVarType::Vec3Array:   return "Vec3Array";
        case ScriptVarType::StringArray: return "StringArray";
        case ScriptVarType::Vec2Array:   return "Vec2Array";
        case ScriptVarType::Vec4Array:   return "Vec4Array";
        case ScriptVarType::QuatArray:   return "QuatArray";
        case ScriptVarType::EntityArray: return "EntityArray";
        default: return "Float";
        }
    }
    static ScriptVarType ScriptVarTypeFromString(const std::string& s)
    {
        if (s == "Bool")   return ScriptVarType::Bool;
        if (s == "Int")    return ScriptVarType::Int;
        if (s == "Vec2")   return ScriptVarType::Vec2;
        if (s == "Vec4")   return ScriptVarType::Vec4;
        if (s == "Quat")   return ScriptVarType::Quat;
        if (s == "Entity") return ScriptVarType::Entity;
        if (s == "Vec3")   return ScriptVarType::Vec3;
        if (s == "String") return ScriptVarType::String;
        if (s == "FloatArray")  return ScriptVarType::FloatArray;
        if (s == "BoolArray")   return ScriptVarType::BoolArray;
        if (s == "IntArray")    return ScriptVarType::IntArray;
        if (s == "Vec3Array")   return ScriptVarType::Vec3Array;
        if (s == "StringArray") return ScriptVarType::StringArray;
        if (s == "Vec2Array")   return ScriptVarType::Vec2Array;
        if (s == "Vec4Array")   return ScriptVarType::Vec4Array;
        if (s == "QuatArray")   return ScriptVarType::QuatArray;
        if (s == "EntityArray") return ScriptVarType::EntityArray;
        return ScriptVarType::Float;
    }

    struct ScriptVariable
    {
        std::string   Name = "NewVar";
        ScriptVarType Type = ScriptVarType::Float;
        std::string   Category = "";    // categoria para agrupamento no painel (vazio = sem categoria)
        std::string   Description = ""; // descrição livre, exibida apenas na aba Node do Script Details
        float         DefaultFloat = 0.f;
        bool          DefaultBool = false;
        int           DefaultInt = 0;
        float         DefaultVec3[3] = { 0,0,0 };
        float         DefaultVec2[2] = { 0,0 };
        float         DefaultVec4[4] = { 0,0,0,1 };  // w=1 por padrão (quaternion identity)
        float         DefaultQuat[4] = { 0,0,0,1 };  // x,y,z,w — identity
        std::string   DefaultString;
        bool          Exposed = false;  // visível no Inspector em runtime

        // Arrays não têm um "default value" único editável campo-a-campo no
        // painel (different de um Float ter 1 valor) — apenas um tamanho
        // inicial e, opcionalmente, um valor de preenchimento (reaproveita os
        // campos Default* acima conforme GetElementType(Type) for usado para
        // preencher cada elemento inicial; ver script_details.cpp).
        int           DefaultArraySize = 0;
    };

    // ── Custom Event (Dispatch) ───────────────────────────────────────────────
    struct ScriptCustomEvent
    {
        std::string Name = "OnMyEvent";
        // Parâmetros futuros podem ser adicionados aqui
    };

    // ── Script Function (estilo Function da Unreal) ──────────────────────────
    // Um parâmetro de entrada ou saída de uma ScriptFunction — só nome + tipo,
    // sem valor default (diferente de ScriptVariable; parâmetros de função não
    // têm "default global", o valor vem sempre do Call ou do Return Node).
    struct ScriptFunctionParam
    {
        std::string   Name = "Param";
        ScriptVarType Type = ScriptVarType::Float;
    };

    // Cada ScriptFunction tem seu PRÓPRIO ScriptGraph (igual ao grafo principal
    // do ScriptAsset, só que menor e isolado) — dentro dele vivem exatamente um
    // node "Function Entry" (expõe Inputs como pins de saída) e um "Return
    // Node" (expõe Outputs como pins de entrada), criados automaticamente por
    // ScriptAsset::AddFunction. shared_ptr pelo mesmo motivo do m_Graph do
    // ScriptAsset: ScriptGraph não é copiável (unique_ptr<ScriptNode> interno).
    struct ScriptFunction
    {
        std::string Name = "NewFunction";
        std::vector<ScriptFunctionParam> Inputs;
        std::vector<ScriptFunctionParam> Outputs;
        // BUGFIX: sem inicializador padrão de propósito. `= std::make_shared
        // <ScriptGraph>()` aqui exigiria o tipo COMPLETO de ScriptGraph em
        // QUALQUER .cpp que default-construa um ScriptFunction (mesmo só de
        // passagem, ex: std::vector<ScriptFunction>::resize/copy) — e este
        // header só tem um forward declare de ScriptGraph (de propósito,
        // evita include circular com imgui_node_editor.h). Isso já causou
        // erro de link real ("no instance of overloaded function
        // std::make_shared matches the argument list") em arquivos do editor
        // que não incluíam script_graph.hpp antes de tocar num ScriptFunction.
        // AddFunction() e DeserializeScriptFunction() (em script_asset.cpp,
        // que SEMPRE inclui script_graph.hpp) já atribuem isso explicitamente
        // logo após construir o objeto — não depende deste inicializador.
        std::shared_ptr<ScriptGraph> Graph;
    };

    // ── Script Asset — arquivo .axescript ────────────────────────────────────
    class AXE_API ScriptAsset
    {
    public:
        ScriptAsset() = default;

        // Metadados
        const std::string& GetName()      const { return m_Name; }
        void               SetName(const std::string& n) { m_Name = n; }

        ScriptClassType    GetClassType() const { return m_ClassType; }
        void               SetClassType(ScriptClassType t) { m_ClassType = t; }

        const std::filesystem::path& GetFilePath() const { return m_FilePath; }

        // Componentes definidos no editor
        std::vector<ScriptComponentDef>& GetComponents() { return m_Components; }
        const std::vector<ScriptComponentDef>& GetComponents() const { return m_Components; }
        void AddComponent(const ScriptComponentDef& def) { m_Components.push_back(def); }
        void RemoveComponent(int index);

        // Variables
        std::vector<ScriptVariable>& GetVariables() { return m_Variables; }
        const std::vector<ScriptVariable>& GetVariables() const { return m_Variables; }
        void AddVariable(const ScriptVariable& v) { m_Variables.push_back(v); }
        void RemoveVariable(int i) { if (i >= 0 && i < (int)m_Variables.size()) m_Variables.erase(m_Variables.begin() + i); }

        // Custom Events (Dispatch)
        std::vector<ScriptCustomEvent>& GetCustomEvents() { return m_CustomEvents; }
        const std::vector<ScriptCustomEvent>& GetCustomEvents() const { return m_CustomEvents; }
        void AddCustomEvent(const ScriptCustomEvent& e) { m_CustomEvents.push_back(e); }
        void RemoveCustomEvent(int i) { if (i >= 0 && i < (int)m_CustomEvents.size()) m_CustomEvents.erase(m_CustomEvents.begin() + i); }

        // Functions — cada uma com seu próprio ScriptGraph (ver ScriptFunction)
        std::vector<ScriptFunction>& GetFunctions() { return m_Functions; }
        const std::vector<ScriptFunction>& GetFunctions() const { return m_Functions; }
        // Cria a função, já populando seu Graph com "Function Entry" + "Return
        // Node" — retorna ponteiro estável (m_Functions usa reserve implícito
        // do vector; cuidado ao chamar AddFunction com referências antigas
        // ainda em uso, igual qualquer vector<T>::push_back).
        ScriptFunction* AddFunction(const std::string& name);
        void RemoveFunction(int i) { if (i >= 0 && i < (int)m_Functions.size()) m_Functions.erase(m_Functions.begin() + i); }
        ScriptFunction* FindFunction(const std::string& name);

        // Node graph
        std::shared_ptr<ScriptGraph> GetGraph() { return m_Graph; }

        // DLL compilada
        std::string DllPath;
        bool        IsCompiled = false;

        // Serialização
        bool Save(const std::filesystem::path& filepath);
        bool Load(const std::filesystem::path& filepath);
        std::string SaveToString();           // snapshot para undo/redo
        bool        LoadFromString(const std::string& json);

        static std::shared_ptr<ScriptAsset> Create(const std::string& name,
            ScriptClassType type = ScriptClassType::Entity);
        static std::shared_ptr<ScriptAsset> LoadFromFile(const std::filesystem::path& path);

    private:
        std::string             m_Name = "NewScript";
        ScriptClassType         m_ClassType = ScriptClassType::Entity;
        std::filesystem::path   m_FilePath;
        std::vector<ScriptComponentDef> m_Components;
        std::vector<ScriptVariable>     m_Variables;
        std::vector<ScriptCustomEvent>  m_CustomEvents;
        std::vector<ScriptFunction>     m_Functions;
        std::shared_ptr<ScriptGraph>    m_Graph = std::make_shared<ScriptGraph>();
    };

} // namespace axe