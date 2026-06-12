#include "physics_world.hpp"
#include "axe/scene/components.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/log/log.hpp"

#ifdef JPH_DEBUG_RENDERER
#undef JPH_DEBUG_RENDERER
#endif

// Config do Jolt DEVE vir antes de qualquer header do Jolt
#include "axe/physics/jolt_config.hpp"

#include <Jolt/Jolt.h>
// glm component_wise para compMin/compMax
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/component_wise.hpp>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <unordered_map>

namespace Layers
{
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
}

namespace axe
{

    // Conversões
    static JPH::Vec3  ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
    static glm::vec3  FromJolt(const JPH::RVec3& v) { return { (float)v.GetX(), (float)v.GetY(), (float)v.GetZ() }; }
    static glm::vec3  FromJoltRot(const JPH::Quat& q)
    {
        glm::quat gq(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
        return glm::eulerAngles(gq);
    }
    static JPH::Quat ToJoltQuat(const glm::vec3& eulerRad)
    {
        glm::quat q(eulerRad);
        return JPH::Quat(q.x, q.y, q.z, q.w);
    }

    // Acesso seguro ao BodyInterface
    static JPH::BodyInterface& GetBI()
    {
        return *static_cast<JPH::BodyInterface*>(PhysicsSystem::Get().GetBodyInterfacePtr());
    }

    static JPH::BodyID ToBodyID(uint32_t stored) { return JPH::BodyID(stored); }

    static JPH::TempAllocatorImpl* GetTA()
    {
        return static_cast<JPH::TempAllocatorImpl*>(PhysicsSystem::Get().GetTempAllocPtr());
    }
    static JPH::PhysicsSystem* GetPS()
    {
        return static_cast<JPH::PhysicsSystem*>(PhysicsSystem::Get().GetJoltSystemPtr());
    }

    // Mapa global de CharacterVirtual por entity
    static std::unordered_map<entt::entity, JPH::Ref<JPH::CharacterVirtual>> s_Characters;

    // ==================== Shape ====================

    static JPH::ShapeRefC CreateShape(const ColliderComponent& col, const glm::vec3& scale,
        const Mesh* mesh = nullptr)
    {
        JPH::ShapeRefC shape;
        switch (col.Shape)
        {
        case ColliderShape::Box:
        {
            glm::vec3 halfExt = col.HalfExtent * scale;
            // Garante espessura mínima de 0.05 em qualquer eixo
            // Evita que floors ultra-finos (Scale Y = 0.001) sejam ignorados pelo Jolt
            halfExt = glm::max(halfExt, glm::vec3(0.05f));
            JPH::BoxShapeSettings s(ToJolt(halfExt));
            s.mConvexRadius = std::min(0.02f, glm::compMin(halfExt) * 0.1f);
            auto r = s.Create();
            if (r.IsValid()) shape = r.Get();
            break;
        }
        case ColliderShape::Sphere:
        {
            float scaledRadius = col.Radius * glm::compMax(scale);
            JPH::SphereShapeSettings s(std::max(scaledRadius, 0.001f));
            auto r = s.Create();
            if (r.IsValid()) shape = r.Get();
            break;
        }
        case ColliderShape::Capsule:
        {
            float scaledRadius = col.CapsuleRadius * glm::compMax(glm::vec2(scale.x, scale.z));
            float scaledHeight = col.Height * scale.y;
            float halfH = std::max(scaledHeight * 0.5f - scaledRadius, 0.01f);
            JPH::CapsuleShapeSettings s(halfH, std::max(scaledRadius, 0.001f));
            auto r = s.Create();
            if (r.IsValid()) shape = r.Get();
            break;
        }
        case ColliderShape::Mesh:
        {
            // Mesh exato — apenas para Static
            if (!mesh) break;
            JPH::TriangleList triangles;
            const auto& verts = mesh->GetVertices();
            const auto& indices = mesh->GetIndices();
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                auto toVec = [&](uint32_t idx) {
                    const auto& v = verts[idx];
                    return JPH::Float3(v.Position.x * scale.x,
                        v.Position.y * scale.y,
                        v.Position.z * scale.z);
                    };
                triangles.push_back(JPH::Triangle(
                    toVec(indices[i]),
                    toVec(indices[i + 1]),
                    toVec(indices[i + 2])));
            }
            if (triangles.empty()) break;
            JPH::MeshShapeSettings s(std::move(triangles));
            auto r = s.Create();
            if (r.IsValid()) shape = r.Get();
            break;
        }
        case ColliderShape::ConvexHull:
        {
            // Convex hull — para Dynamic/Kinematic
            if (!mesh) break;
            JPH::Array<JPH::Vec3> points;
            const auto& verts = mesh->GetVertices();
            for (const auto& v : verts)
                points.push_back(JPH::Vec3(v.Position.x * scale.x,
                    v.Position.y * scale.y,
                    v.Position.z * scale.z));
            if (points.empty()) break;
            JPH::ConvexHullShapeSettings s(points);
            s.mMaxConvexRadius = 0.05f;
            auto r = s.Create();
            if (r.IsValid()) shape = r.Get();
            break;
        }
        }
        if (shape && glm::length(col.Offset) > 0.001f)
        {
            glm::vec3 scaledOffset = col.Offset * scale;
            JPH::RotatedTranslatedShapeSettings os(ToJolt(scaledOffset), JPH::Quat::sIdentity(), shape);
            auto r = os.Create();
            if (r.IsValid()) shape = r.Get();
        }
        return shape;
    }

    // ==================== PhysicsWorld ====================

    void PhysicsWorld::CreateBody(entt::entity entity, Scene& scene)
    {
        auto& registry = scene.GetRegistry();
        auto* rb = registry.try_get<RigidbodyComponent>(entity);
        auto* col = registry.try_get<ColliderComponent>(entity);
        auto* tc = registry.try_get<TransformComponent>(entity);
        if (!rb || !col || !tc || rb->IsCreated) return;

        // Scale do transform aplicado ao collider
        glm::vec3 scale = tc->Data.Scale;

        // Pega o mesh se disponível (para Mesh e ConvexHull shapes)
        const Mesh* mesh = nullptr;
        if (auto* mc = registry.try_get<MeshComponent>(entity))
            if (mc->Data) mesh = mc->Data.get();

        auto shape = CreateShape(*col, scale, mesh);
        if (!shape) { AXE_CORE_ERROR("PhysicsWorld: falha ao criar shape."); return; }

        JPH::ObjectLayer layer = (rb->Type == BodyType::Static) ? Layers::NON_MOVING : Layers::MOVING;
        JPH::EMotionType motion;
        switch (rb->Type)
        {
        case BodyType::Static:    motion = JPH::EMotionType::Static;    break;
        case BodyType::Kinematic: motion = JPH::EMotionType::Kinematic; break;
        default:                  motion = JPH::EMotionType::Dynamic;   break;
        }

        JPH::BodyCreationSettings settings(
            shape,
            JPH::RVec3(tc->Data.Position.x, tc->Data.Position.y, tc->Data.Position.z),
            ToJoltQuat(tc->Data.Rotation),
            motion, layer);

        settings.mFriction = rb->Friction;
        settings.mRestitution = rb->Restitution;
        settings.mLinearDamping = rb->LinearDamping;
        settings.mAngularDamping = rb->AngularDamping;
        settings.mGravityFactor = rb->UseGravity ? 1.0f : 0.0f;

        if (rb->LockRotX) settings.mAllowedDOFs = settings.mAllowedDOFs & ~JPH::EAllowedDOFs::RotationX;
        if (rb->LockRotY) settings.mAllowedDOFs = settings.mAllowedDOFs & ~JPH::EAllowedDOFs::RotationY;
        if (rb->LockRotZ) settings.mAllowedDOFs = settings.mAllowedDOFs & ~JPH::EAllowedDOFs::RotationZ;

        auto& bi = GetBI();
        JPH::Body* body = bi.CreateBody(settings);
        if (!body) { AXE_CORE_ERROR("PhysicsWorld: falha ao criar body."); return; }

        JPH::BodyID id = body->GetID();
        bi.AddBody(id, JPH::EActivation::Activate);

        rb->BodyID = id.GetIndexAndSequenceNumber();
        rb->IsCreated = true;
        AXE_CORE_INFO("PhysicsWorld: body criado (entity {}).", (uint32_t)entity);
    }

    void PhysicsWorld::DestroyBody(entt::entity entity, Scene& scene)
    {
        auto* rb = scene.GetRegistry().try_get<RigidbodyComponent>(entity);
        if (!rb || !rb->IsCreated) return;

        auto& bi = GetBI();
        JPH::BodyID id = ToBodyID(rb->BodyID);
        if (!id.IsInvalid()) { bi.RemoveBody(id); bi.DestroyBody(id); }

        rb->IsCreated = false;
        rb->BodyID = JPH::BodyID::cInvalidBodyID;
    }

    void PhysicsWorld::CreateCharacter(entt::entity entity, Scene& scene)
    {
        auto& registry = scene.GetRegistry();
        auto* cc = registry.try_get<CharacterControllerComponent>(entity);
        auto* tc = registry.try_get<TransformComponent>(entity);
        if (!cc || !tc || cc->IsCreated) return;

        JPH::PhysicsSystem* ps = GetPS();
        if (!ps) return;

        // Cápsula: Height é a altura total, Radius é o raio
        float halfHeight = std::max(cc->Height * 0.5f - cc->Radius, 0.01f);
        JPH::CapsuleShapeSettings capsule(halfHeight, std::max(cc->Radius, 0.01f));
        auto shapeResult = capsule.Create();
        if (!shapeResult.IsValid())
        {
            AXE_CORE_ERROR("PhysicsWorld: falha ao criar shape do CharacterController.");
            return;
        }

        JPH::CharacterVirtualSettings settings;
        settings.mShape = shapeResult.Get();
        settings.mMaxSlopeAngle = glm::radians(cc->MaxSlopeAngle);
        settings.mMaxStrength = 100.0f;
        settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        settings.mPredictiveContactDistance = 0.1f;
        settings.mPenetrationRecoverySpeed = 1.0f;

        JPH::RVec3 pos(tc->Data.Position.x, tc->Data.Position.y, tc->Data.Position.z);
        auto character = new JPH::CharacterVirtual(&settings, pos, JPH::Quat::sIdentity(), 0, ps);
        s_Characters[entity] = character;

        cc->IsCreated = true;
        cc->CharacterID = (uint32_t)entity; // usa entity ID como handle
        AXE_CORE_INFO("PhysicsWorld: CharacterController criado (entity {}).", (uint32_t)entity);
    }

    void PhysicsWorld::DestroyCharacter(entt::entity entity, Scene& scene)
    {
        auto it = s_Characters.find(entity);
        if (it != s_Characters.end())
        {
            s_Characters.erase(it);
            auto* cc = scene.GetRegistry().try_get<CharacterControllerComponent>(entity);
            if (cc) { cc->IsCreated = false; cc->CharacterID = 0; }
            AXE_CORE_INFO("PhysicsWorld: CharacterController destruído (entity {}).", (uint32_t)entity);
        }
    }

    void PhysicsWorld::OnSceneStart(Scene& scene)
    {
        PhysicsSystem::Get().Initialize();
        auto& registry = scene.GetRegistry();
        registry.view<RigidbodyComponent, ColliderComponent>().each(
            [&](entt::entity entity, RigidbodyComponent&, ColliderComponent&)
            {
                // Entidades com CharacterController não usam Rigidbody body —
                // o CharacterVirtual já é o body de física delas
                if (registry.all_of<CharacterControllerComponent>(entity)) return;
                CreateBody(entity, scene);
            });
        // Cria static bodies para entidades com Collider mas sem Rigidbody
        // (ex: floors, paredes estáticas sem componente Rigidbody explícito)
        registry.view<ColliderComponent>().each(
            [&](entt::entity entity, ColliderComponent& col)
            {
                // Já tem Rigidbody — será criado pelo loop acima
                if (registry.all_of<RigidbodyComponent>(entity)) return;
                // CharacterController — tem seu próprio body
                if (registry.all_of<CharacterControllerComponent>(entity)) return;
                // Cria um body estático sintético
                auto* tc = registry.try_get<TransformComponent>(entity);
                if (!tc) return;
                glm::vec3 scale = tc->Data.Scale;
                const Mesh* mesh = nullptr;
                if (auto* mc = registry.try_get<MeshComponent>(entity))
                    if (mc->Data) mesh = mc->Data.get();
                auto shape = CreateShape(col, scale, mesh);
                if (!shape) return;
                auto& bi = GetBI();
                JPH::BodyCreationSettings settings(
                    shape,
                    JPH::RVec3(tc->Data.Position.x, tc->Data.Position.y, tc->Data.Position.z),
                    ToJoltQuat(tc->Data.Rotation),
                    JPH::EMotionType::Static, Layers::NON_MOVING);
                JPH::Body* body = bi.CreateBody(settings);
                if (!body) return;
                bi.AddBody(body->GetID(), JPH::EActivation::DontActivate);
                AXE_CORE_INFO("PhysicsWorld: static body implícito criado (entity {}).", (uint32_t)entity);
            });

        // Cria CharacterControllers
        registry.view<CharacterControllerComponent>().each(
            [&](entt::entity entity, CharacterControllerComponent&)
            {
                CreateCharacter(entity, scene);
            });

        AXE_CORE_INFO("PhysicsWorld: cena iniciada.");
    }

    void PhysicsWorld::OnSceneStop(Scene& scene)
    {
        auto& registry = scene.GetRegistry();
        registry.view<RigidbodyComponent>().each(
            [&](entt::entity entity, RigidbodyComponent&)
            {
                DestroyBody(entity, scene);
            });
        // Destroi CharacterControllers
        registry.view<CharacterControllerComponent>().each(
            [&](entt::entity entity, CharacterControllerComponent&)
            {
                DestroyCharacter(entity, scene);
            });
        s_Characters.clear();

        AXE_CORE_INFO("PhysicsWorld: cena encerrada.");
    }

    void PhysicsWorld::OnUpdate(Scene& scene, float deltaTime)
    {
        if (!PhysicsSystem::Get().IsInitialized()) return;

        PhysicsSystem::Get().Step(deltaTime);

        auto& registry = scene.GetRegistry();
        auto& bi = GetBI();

        registry.view<RigidbodyComponent, TransformComponent>().each(
            [&](entt::entity entity, RigidbodyComponent& rb, TransformComponent& tc)
            {
                if (!rb.IsCreated || rb.Type != BodyType::Dynamic) return;

                JPH::BodyID id = ToBodyID(rb.BodyID);
                if (id.IsInvalid() || !bi.IsAdded(id)) return;

                tc.Data.Position = FromJolt(bi.GetPosition(id));
                tc.Data.Rotation = FromJoltRot(bi.GetRotation(id));
                tc.Data.UseWorldMatrix = false;
            });


        // ── CharacterController update ──────────────────────────────────────────
        registry.view<CharacterControllerComponent, TransformComponent>().each(
            [&](entt::entity entity, CharacterControllerComponent& cc, TransformComponent& tc)
            {
                auto it = s_Characters.find(entity);
                if (it == s_Characters.end() || !it->second) return;

                auto& ch = *it->second;
                JPH::PhysicsSystem* ps = GetPS();
                JPH::TempAllocatorImpl* ta = GetTA();
                if (!ps || !ta) return;

                // Monta velocidade: XZ vem do script, Y da gravidade
                JPH::Vec3 curVel = ch.GetLinearVelocity();
                JPH::Vec3 gravity = ps->GetGravity();

                // Velocidade horizontal do script
                float vx = cc.Velocity.x;
                float vz = cc.Velocity.z;

                if (std::abs(vx) > 0.001f || std::abs(vz) > 0.001f)
                    AXE_CORE_INFO("PhysicsWorld CC: entity={} vel=({:.2f},{:.2f})", (uint32_t)entity, vx, vz);

                // Pulo
                bool grounded = ch.IsSupported();
                cc.IsGrounded = grounded;
                float vy = curVel.GetY();

                if (cc.WantsJump && grounded)
                {
                    vy = cc.JumpForce;
                    cc.WantsJump = false;
                }
                else
                {
                    // Aplica gravidade
                    vy += gravity.GetY() * deltaTime;
                }

                ch.SetLinearVelocity(JPH::Vec3(vx, vy, vz));

                // ExtendedUpdate — move, sobe degraus, gruda no chão
                JPH::CharacterVirtual::ExtendedUpdateSettings euSettings;
                euSettings.mStickToFloorStepDown = JPH::Vec3(0, -cc.StepHeight, 0);
                euSettings.mWalkStairsStepUp = JPH::Vec3(0, cc.StepHeight, 0);

                ch.ExtendedUpdate(
                    deltaTime,
                    gravity,
                    euSettings,
                    ps->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                    ps->GetDefaultLayerFilter(Layers::MOVING),
                    {},  // BodyFilter
                    {},  // ShapeFilter
                    *ta);

                // Sync posição de volta para o TransformComponent
                JPH::RVec3 newPos = ch.GetPosition();
                tc.Data.Position = glm::vec3((float)newPos.GetX(), (float)newPos.GetY(), (float)newPos.GetZ());
                tc.Data.UseWorldMatrix = false;

                // Reset velocidade XZ (será reescrita pelo script no próximo frame)
                cc.Velocity.x = 0;
                cc.Velocity.z = 0;
            });
    }

    void PhysicsWorld::AddForce(entt::entity entity, Scene& scene, const glm::vec3& force)
    {
        auto* rb = scene.GetRegistry().try_get<RigidbodyComponent>(entity);
        if (!rb || !rb->IsCreated) return;
        JPH::BodyID id = ToBodyID(rb->BodyID);
        if (!id.IsInvalid()) GetBI().AddForce(id, ToJolt(force));
    }

    void PhysicsWorld::AddImpulse(entt::entity entity, Scene& scene, const glm::vec3& impulse)
    {
        auto* rb = scene.GetRegistry().try_get<RigidbodyComponent>(entity);
        if (!rb || !rb->IsCreated) return;
        JPH::BodyID id = ToBodyID(rb->BodyID);
        if (!id.IsInvalid()) GetBI().AddImpulse(id, ToJolt(impulse));
    }

    RaycastHit PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist)
    {
        return PhysicsSystem::Get().Raycast(origin, dir, maxDist);
    }

} // namespace axe