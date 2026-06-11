#pragma once
#include "axe/utils/glm_config.hpp"
#include <cstdint>
#include <functional>

namespace axe
{
    // ==================== Rigidbody ====================

    enum class BodyType
    {
        Static,      // não se move, colide com tudo
        Dynamic,     // afetado pela física
        Kinematic    // movido por código, colide com dynamic
    };

    struct RigidbodyComponent
    {
        BodyType  Type = BodyType::Dynamic;
        float     Mass = 1.0f;
        float     Friction = 0.5f;
        float     Restitution = 0.0f;  // 0 = sem bounce, 1 = bounce total
        float     LinearDamping = 0.05f;
        float     AngularDamping = 0.05f;
        bool      UseGravity = true;
        bool      LockRotX = false;
        bool      LockRotY = false;
        bool      LockRotZ = false;

        // Runtime — ID do body no Jolt (cInvalidBodyID = não criado)
        uint32_t  BodyID = 0xffffffff; // JPH::BodyID::cInvalidBodyID
        bool      IsCreated = false;

        // ── Pending commands do ScriptRigidbodyProxy ──────────────────────────
        // Consumidos pelo PhysicsWorld::OnUpdate no mesmo frame que são escritos.
        glm::vec3 PendingForce = {};
        bool      NeedsForceApply = false;

        glm::vec3 PendingVelocity = {};
        bool      NeedsVelocitySet = false;
    };

    // ==================== Collider ====================

    enum class ColliderShape
    {
        Box,
        Sphere,
        Capsule,
        Mesh,       // Mesh exato — Static only
        ConvexHull, // Convex hull — Dynamic/Kinematic
    };

    struct ColliderComponent
    {
        ColliderShape Shape = ColliderShape::Box;
        glm::vec3     Offset = { 0, 0, 0 };

        // Box
        glm::vec3     HalfExtent = { 0.5f, 0.5f, 0.5f };

        // Sphere
        float         Radius = 1.0f;

        // Capsule
        float         Height = 1.8f;   // altura total
        float         CapsuleRadius = 0.3f;

        bool          IsTrigger = false; // trigger não colide, só detecta
        bool          ShowDebug = false; // mostra wireframe no viewport
    };

    // ==================== Character Controller ====================

    struct CharacterControllerComponent
    {
        float Height = 1.8f;
        float Radius = 0.3f;
        float MaxSlopeAngle = 45.0f;    // graus
        float StepHeight = 0.3f;       // altura máxima de degrau
        float MaxSpeed = 5.0f;
        float JumpForce = 5.0f;

        // Runtime
        glm::vec3 Velocity = {};
        bool      IsGrounded = false;
        bool      WantsJump = false;
        uint32_t  CharacterID = 0;
        bool      IsCreated = false;
    };

    // ==================== Trigger Volume ====================

    struct TriggerComponent
    {
        ColliderShape Shape = ColliderShape::Box;
        glm::vec3     HalfExtent = { 1, 1, 1 };
        float         Radius = 1.0f;

        // Callbacks — conectar via script ou editor
        std::function<void(uint32_t otherBodyID)> OnEnter;
        std::function<void(uint32_t otherBodyID)> OnExit;

        // Runtime
        uint32_t BodyID = 0;
        bool     IsCreated = false;
    };

} // namespace axe