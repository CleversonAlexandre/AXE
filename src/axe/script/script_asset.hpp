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

        // Node graph
        std::shared_ptr<ScriptGraph> GetGraph() { return m_Graph; }

        // DLL compilada
        std::string DllPath;
        bool        IsCompiled = false;

        // Serialização
        bool Save(const std::filesystem::path& filepath);
        bool Load(const std::filesystem::path& filepath);

        static std::shared_ptr<ScriptAsset> Create(const std::string& name,
            ScriptClassType type = ScriptClassType::Entity);
        static std::shared_ptr<ScriptAsset> LoadFromFile(const std::filesystem::path& path);

    private:
        std::string             m_Name = "NewScript";
        ScriptClassType         m_ClassType = ScriptClassType::Entity;
        std::filesystem::path   m_FilePath;
        std::vector<ScriptComponentDef> m_Components;
        std::shared_ptr<ScriptGraph>    m_Graph = std::make_shared<ScriptGraph>();
    };

} // namespace axe