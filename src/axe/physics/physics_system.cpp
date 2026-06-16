#include "physics_system.hpp"
#include "axe/log/log.hpp"
#include <cstdarg>

// Config do Jolt DEVE vir antes de qualquer header do Jolt
#include "axe/physics/jolt_config.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>

JPH_SUPPRESS_WARNINGS

// ==================== Layers ====================

namespace Layers
{
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr uint32_t         NUM_LAYERS = 2;
}

namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr uint32_t NUM_LAYERS = 2;
}

// ==================== Filters ====================

class BPLayerInterfaceImpl : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl()
    {
        m_ObjToBP[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        m_ObjToBP[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override { return m_ObjToBP[l]; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override
    {
        return (JPH::BroadPhaseLayer::Type)l == 0 ? "NON_MOVING" : "MOVING";
    }
#endif
private:
    JPH::BroadPhaseLayer m_ObjToBP[Layers::NUM_LAYERS];
};

class ObjVsBPFilter : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer l1, JPH::BroadPhaseLayer l2) const override
    {
        if (l1 == Layers::NON_MOVING) return l2 == BroadPhaseLayers::MOVING;
        if (l1 == Layers::MOVING)     return true;
        return false;
    }
};

class ObjVsObjFilter : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer l1, JPH::ObjectLayer l2) const override
    {
        if (l1 == Layers::NON_MOVING) return l2 == Layers::MOVING;
        if (l1 == Layers::MOVING)     return true;
        return false;
    }
};

// ==================== Contact Listener ====================
// Recebe eventos de colisão e trigger do Jolt e os repassa via callbacks.
// BodyID.GetIndexAndSequenceNumber() retorna um uint32 que usamos como
// identificador de entity (mapeado em physics_world.cpp).

class AXEContactListener : public JPH::ContactListener
{
public:
    std::function<void(uint32_t, uint32_t)> OnCollisionEnter;
    std::function<void(uint32_t, uint32_t)> OnTriggerEnter;
    std::function<void(uint32_t, uint32_t)> OnTriggerExit;

    JPH::ValidateResult OnContactValidate(
        const JPH::Body& inBody1, const JPH::Body& inBody2,
        JPH::RVec3Arg, const JPH::CollideShapeResult&) override
    {
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(
        const JPH::Body& inBody1, const JPH::Body& inBody2,
        const JPH::ContactManifold&, JPH::ContactSettings& settings) override
    {
        // UserData guarda o entt::entity cast para uint64
        uint32_t e1 = (uint32_t)inBody1.GetUserData();
        uint32_t e2 = (uint32_t)inBody2.GetUserData();

        bool t1 = inBody1.IsSensor();
        bool t2 = inBody2.IsSensor();

        if (t1 || t2)
        {
            if (OnTriggerEnter) OnTriggerEnter(e1, e2);
        }
        else
        {
            if (OnCollisionEnter) OnCollisionEnter(e1, e2);
        }
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
    {
        // Para triggers, precisaríamos rastrear o par — simplificado por enquanto
    }
};

// ==================== namespace axe ====================

namespace axe
{

    static AXEContactListener* GetCL(void* p) { return static_cast<AXEContactListener*>(p); }

    // Casts seguros dos void*
    static JPH::PhysicsSystem* GetPS(void* p) { return static_cast<JPH::PhysicsSystem*>(p); }
    static JPH::TempAllocatorImpl* GetTA(void* p) { return static_cast<JPH::TempAllocatorImpl*>(p); }
    static JPH::JobSystemThreadPool* GetJS(void* p) { return static_cast<JPH::JobSystemThreadPool*>(p); }
    static BPLayerInterfaceImpl* GetBP(void* p) { return static_cast<BPLayerInterfaceImpl*>(p); }
    static ObjVsBPFilter* GetOBP(void* p) { return static_cast<ObjVsBPFilter*>(p); }
    static ObjVsObjFilter* GetOO(void* p) { return static_cast<ObjVsObjFilter*>(p); }

    void PhysicsSystem::Initialize()
    {
        if (m_Initialized) return;

        // Trace e AssertFailed devem ser configurados ANTES de RegisterTypes
        JPH::Trace = [](const char* inFMT, ...)
            {
                va_list args;
                va_start(args, inFMT);
                char buf[1024];
                vsnprintf(buf, sizeof(buf), inFMT, args);
                va_end(args);
                AXE_CORE_INFO("Jolt: {}", buf);
            };

#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = [](const char* inExpression, const char* inMessage,
            const char* inFile, JPH::uint inLine) -> bool
            {
                AXE_CORE_ERROR("Jolt Assert: {} {} {}:{}", inExpression,
                    inMessage ? inMessage : "", inFile, inLine);
                return false; // false = nao quebra no debugger
            };
#endif

        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        m_TempAllocPtr = new JPH::TempAllocatorImpl(32 * 1024 * 1024);
        m_JobSystemPtr = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 2);
        m_BPLayerInterface = new BPLayerInterfaceImpl();
        m_ObjVsBPFilter = new ObjVsBPFilter();
        m_ObjVsObjFilter = new ObjVsObjFilter();

        auto* ps = new JPH::PhysicsSystem();
        ps->Init(s_MaxBodies, 0, s_MaxBodyPairs, s_MaxContactConstraints,
            *GetBP(m_BPLayerInterface), *GetOBP(m_ObjVsBPFilter), *GetOO(m_ObjVsObjFilter));
        ps->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

        // Registra o ContactListener — recebe todas as colisões e triggers
        auto* cl = new AXEContactListener();
        cl->OnCollisionEnter = [this](uint32_t a, uint32_t b) { if (m_OnCollision)   m_OnCollision(a, b); };
        cl->OnTriggerEnter = [this](uint32_t a, uint32_t b) { if (m_OnTriggerEnter) m_OnTriggerEnter(a, b); };
        cl->OnTriggerExit = [this](uint32_t a, uint32_t b) { if (m_OnTriggerExit)  m_OnTriggerExit(a, b); };
        ps->SetContactListener(cl);
        m_ContactListenerPtr = cl;

        m_PhysicsSystemPtr = ps;
        m_Initialized = true;
        AXE_CORE_INFO("PhysicsSystem: inicializado (Jolt Physics).");
    }

    void PhysicsSystem::Shutdown()
    {
        if (!m_Initialized) return;

        delete GetPS(m_PhysicsSystemPtr); m_PhysicsSystemPtr = nullptr;
        delete GetCL(m_ContactListenerPtr); m_ContactListenerPtr = nullptr;
        delete GetJS(m_JobSystemPtr);     m_JobSystemPtr = nullptr;
        delete GetTA(m_TempAllocPtr);     m_TempAllocPtr = nullptr;
        delete GetBP(m_BPLayerInterface); m_BPLayerInterface = nullptr;
        delete GetOBP(m_ObjVsBPFilter);   m_ObjVsBPFilter = nullptr;
        delete GetOO(m_ObjVsObjFilter);   m_ObjVsObjFilter = nullptr;

        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;

        m_Initialized = false;
        AXE_CORE_INFO("PhysicsSystem: finalizado.");
    }

    void PhysicsSystem::Step(float deltaTime)
    {
        if (!m_Initialized) return;

        m_Accumulator += deltaTime;
        while (m_Accumulator >= s_FixedTimeStep)
        {
            GetPS(m_PhysicsSystemPtr)->Update(
                s_FixedTimeStep, 1,
                GetTA(m_TempAllocPtr),
                GetJS(m_JobSystemPtr));
            m_Accumulator -= s_FixedTimeStep;
        }
    }

    void* PhysicsSystem::GetBodyInterfacePtr()
    {
        if (!m_Initialized) return nullptr;
        return &GetPS(m_PhysicsSystemPtr)->GetBodyInterface();
    }

    RaycastHit PhysicsSystem::Raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist)
    {
        RaycastHit result;
        if (!m_Initialized) return result;

        auto* ps = GetPS(m_PhysicsSystemPtr);

        JPH::RRayCast ray{
            JPH::RVec3(origin.x, origin.y, origin.z),
            JPH::Vec3(dir.x, dir.y, dir.z) * maxDist
        };

        JPH::RayCastResult hit;
        if (ps->GetNarrowPhaseQuery().CastRay(ray, hit))
        {
            result.Hit = true;
            result.Distance = hit.mFraction * maxDist;
            result.BodyID = hit.mBodyID.GetIndexAndSequenceNumber();

            auto pt = ray.GetPointOnRay(hit.mFraction);
            result.Point = { (float)pt.GetX(), (float)pt.GetY(), (float)pt.GetZ() };

            JPH::BodyLockRead lock(ps->GetBodyLockInterface(), hit.mBodyID);
            if (lock.Succeeded())
            {
                auto n = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, JPH::RVec3Arg(pt));
                result.Normal = { n.GetX(), n.GetY(), n.GetZ() };
            }
        }

        return result;
    }

    void PhysicsSystem::SetTriggerCallbacks(TriggerEnterCallback onEnter, TriggerExitCallback onExit)
    {
        m_OnTriggerEnter = onEnter;
        m_OnTriggerExit = onExit;
        // Atualiza o listener se já foi criado
        if (m_ContactListenerPtr)
        {
            GetCL(m_ContactListenerPtr)->OnTriggerEnter = [this](uint32_t a, uint32_t b) { if (m_OnTriggerEnter) m_OnTriggerEnter(a, b); };
            GetCL(m_ContactListenerPtr)->OnTriggerExit = [this](uint32_t a, uint32_t b) { if (m_OnTriggerExit)  m_OnTriggerExit(a, b); };
        }
    }

    void PhysicsSystem::SetCollisionCallback(CollisionCallback onCollision)
    {
        m_OnCollision = onCollision;
        if (m_ContactListenerPtr)
            GetCL(m_ContactListenerPtr)->OnCollisionEnter = [this](uint32_t a, uint32_t b) { if (m_OnCollision) m_OnCollision(a, b); };
    }

} // namespace axe