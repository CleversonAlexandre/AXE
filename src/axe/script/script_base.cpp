#include "script_base.hpp"
#include "script_component.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/physics/physics_components.hpp"
#include "axe/physics/physics_system.hpp"
#include "axe/log/log.hpp"
#include <algorithm>

// Jolt — necessário para DestroyEntitySafe remover bodies do simulador
#ifdef JPH_DEBUG_RENDERER
#undef JPH_DEBUG_RENDERER
#endif
#include "axe/physics/jolt_config.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/rotate_vector.hpp>

namespace axe
{
    // ─────────────────────────────────────────────────────────────────────────
    // ScriptTransformProxy
    // ─────────────────────────────────────────────────────────────────────────

    static TransformComponent* GetTC(entt::entity e, Scene* s)
    {
        if (!s || e == entt::null) return nullptr;
        auto& reg = s->GetRegistry();
        if (!reg.all_of<TransformComponent>(e)) return nullptr;
        return &reg.get<TransformComponent>(e);
    }

    glm::vec3 ScriptTransformProxy::GetPosition() const
    {
        auto* tc = GetTC(Entity, ScenePtr);
        return tc ? tc->Data.Position : glm::vec3(0);
    }

    glm::vec3 ScriptTransformProxy::GetRotation() const
    {
        auto* tc = GetTC(Entity, ScenePtr);
        return tc ? glm::degrees(tc->Data.Rotation) : glm::vec3(0);
    }

    glm::vec3 ScriptTransformProxy::GetScale() const
    {
        auto* tc = GetTC(Entity, ScenePtr);
        return tc ? tc->Data.Scale : glm::vec3(1);
    }

    void ScriptTransformProxy::SetPosition(const glm::vec3& pos)
    {
        auto* tc = GetTC(Entity, ScenePtr);
        if (tc) tc->Data.Position = pos;
    }

    void ScriptTransformProxy::SetRotation(const glm::vec3& euler)
    {
        auto* tc = GetTC(Entity, ScenePtr);
        if (tc) tc->Data.Rotation = glm::radians(euler);
    }

    void ScriptTransformProxy::SetScale(const glm::vec3& scale)
    {
        auto* tc = GetTC(Entity, ScenePtr);
        if (tc) tc->Data.Scale = scale;
    }

    void ScriptTransformProxy::Translate(const glm::vec3& delta)
    {
        auto* tc = GetTC(Entity, ScenePtr);
        if (tc) tc->Data.Position += delta;
    }

    void ScriptTransformProxy::Rotate(const glm::vec3& axis, float radians)
    {
        auto* tc = GetTC(Entity, ScenePtr);
        if (!tc) return;
        // Acumula na rotação euler — usa o mesmo eixo para simplicidade
        // (para rotações compostas complexas o script pode usar SetRotation)
        glm::quat current = glm::quat(tc->Data.Rotation);
        glm::quat delta = glm::angleAxis(radians, glm::normalize(axis));
        glm::quat result = delta * current;
        tc->Data.Rotation = glm::eulerAngles(result);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ScriptRigidbodyProxy
    // ─────────────────────────────────────────────────────────────────────────

    glm::vec3 ScriptRigidbodyProxy::GetVelocity() const
    {
        if (!ScenePtr || Entity == entt::null) return {};
        auto& reg = ScenePtr->GetRegistry();
        if (!reg.all_of<RigidbodyComponent>(Entity)) return {};
        // Velocity não é guardada diretamente no componente — retorna zero por
        // enquanto; para obter em runtime precisaria de acesso ao PhysicsSystem.
        // TODO: expor GetLinearVelocity no PhysicsWorld e chamar aqui.
        return {};
    }

    float ScriptRigidbodyProxy::GetMass() const
    {
        if (!ScenePtr || Entity == entt::null) return 0.f;
        auto& reg = ScenePtr->GetRegistry();
        if (!reg.all_of<RigidbodyComponent>(Entity)) return 0.f;
        return reg.get<RigidbodyComponent>(Entity).Mass;
    }

    void ScriptRigidbodyProxy::SetVelocity(const glm::vec3& vel)
    {
        if (!ScenePtr || Entity == entt::null) return;
        auto& reg = ScenePtr->GetRegistry();
        if (!reg.all_of<RigidbodyComponent>(Entity)) return;
        // Marca o component para que o PhysicsWorld aplique na próxima sync.
        // A flag NeedsVelocitySet + PendingVelocity são adicionadas ao
        // RigidbodyComponent na seção "Extensões necessárias" do CHANGELOG.
        auto& rb = reg.get<RigidbodyComponent>(Entity);
        rb.PendingVelocity = vel;
        rb.NeedsVelocitySet = true;
    }

    void ScriptRigidbodyProxy::AddForce(const glm::vec3& force)
    {
        if (!ScenePtr || Entity == entt::null) return;
        auto& reg = ScenePtr->GetRegistry();
        if (!reg.all_of<RigidbodyComponent>(Entity)) return;
        auto& rb = reg.get<RigidbodyComponent>(Entity);
        rb.PendingForce += force;
        rb.NeedsForceApply = true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ScriptCharacterProxy
    // ─────────────────────────────────────────────────────────────────────────

    bool ScriptCharacterProxy::IsGrounded() const
    {
        if (!ScenePtr || Entity == entt::null) return false;
        auto& reg = ScenePtr->GetRegistry();
        if (!reg.all_of<CharacterControllerComponent>(Entity)) return false;
        return reg.get<CharacterControllerComponent>(Entity).IsGrounded;
    }

    glm::vec3 ScriptCharacterProxy::GetVelocity() const
    {
        if (!ScenePtr || Entity == entt::null) return {};
        auto& reg = ScenePtr->GetRegistry();
        if (!reg.all_of<CharacterControllerComponent>(Entity)) return {};
        return reg.get<CharacterControllerComponent>(Entity).Velocity;
    }

    float ScriptCharacterProxy::GetMaxSpeed() const
    {
        if (!ScenePtr || Entity == entt::null) return 0.f;
        auto& reg = ScenePtr->GetRegistry();
        if (!reg.all_of<CharacterControllerComponent>(Entity)) return 0.f;
        return reg.get<CharacterControllerComponent>(Entity).MaxSpeed;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ScriptBase — Input
    // Todas implementadas aqui (axe.dll), nunca inline na DLL do script.
    // Isso garante que o acesso a m_Context.Input usa sempre o offset correto
    // de axe.dll, eliminando o bug de layout entre compilações.
    // ─────────────────────────────────────────────────────────────────────────

    void ScriptBase::PreUpdate(const bool* keys, const bool* prevKeys)
    {
        m_Context.Input.Keys = keys;
        m_Context.Input.PrevKeys = prevKeys;

        static int s_Count = 0;
        if (s_Count++ < 3)
            AXE_CORE_INFO("ScriptBase::PreUpdate (axe.dll) keys={} W={} A={} S={} D={}",
                (void*)keys,
                keys ? (bool)keys[87] : false,   // W
                keys ? (bool)keys[65] : false,   // A
                keys ? (bool)keys[83] : false,   // S
                keys ? (bool)keys[68] : false);  // D
    }

    void ScriptBase::SetInputPointers(const bool* keys, const bool* prevKeys)
    {
        // Mantido para compatibilidade com código antigo — delega para PreUpdate
        PreUpdate(keys, prevKeys);
    }

    bool ScriptBase::_GetKey(int k) const
    {
        return m_Context.Input.GetKey(k);
    }

    bool ScriptBase::_GetKeyDown(int k) const
    {
        return m_Context.Input.GetKeyDown(k);
    }

    bool ScriptBase::_GetKeyUp(int k) const
    {
        return m_Context.Input.GetKeyUp(k);
    }

    float ScriptBase::_GetAxis(int axis) const
    {
        // 0 = Horizontal (A/D), 1 = Vertical (W/S)
        if (axis == 0)
            return (m_Context.Input.GetKey(68) ? 1.f : 0.f)  // D
            - (m_Context.Input.GetKey(65) ? 1.f : 0.f); // A
        if (axis == 1)
            return (m_Context.Input.GetKey(87) ? 1.f : 0.f)  // W
            - (m_Context.Input.GetKey(83) ? 1.f : 0.f); // S
        return 0.f;
    }

    void ScriptCharacterProxy::Move(const glm::vec3& direction, float speed)
    {
        // Log sempre para diagnóstico
        AXE_CORE_INFO("ScriptCharacterProxy::Move called: ScenePtr={} Entity={} dir=({:.2f},{:.2f},{:.2f}) spd={:.1f}",
            (void*)ScenePtr, (uint32_t)Entity, direction.x, direction.y, direction.z, speed);

        if (!ScenePtr || Entity == entt::null)
        {
            AXE_CORE_WARN("ScriptCharacterProxy::Move: contexto inválido!");
            return;
        }
        auto& reg = ScenePtr->GetRegistry();
        bool hasCC = reg.all_of<CharacterControllerComponent>(Entity);
        AXE_CORE_INFO("  hasCharacterController={}", hasCC);
        if (!hasCC) return;

        auto& cc = reg.get<CharacterControllerComponent>(Entity);
        glm::vec3 dir = (glm::length(direction) > 0.0001f)
            ? glm::normalize(direction) : glm::vec3(0);
        cc.Velocity.x = dir.x * speed;
        cc.Velocity.z = dir.z * speed;
        AXE_CORE_INFO("  Velocity set to ({:.2f},{:.2f})", cc.Velocity.x, cc.Velocity.z);
    }

    void ScriptCharacterProxy::Jump(float force)
    {
        if (!ScenePtr || Entity == entt::null) return;
        auto& reg = ScenePtr->GetRegistry();
        if (!reg.all_of<CharacterControllerComponent>(Entity)) return;
        auto& cc = reg.get<CharacterControllerComponent>(Entity);
        if (cc.IsGrounded)
        {
            cc.WantsJump = true;
            cc.JumpForce = force;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ScriptEventBusProxy
    // ─────────────────────────────────────────────────────────────────────────

    void ScriptEventBusProxy::Send(entt::entity target,
        const std::string& eventName, float value)
    {
        if (!ScenePtr || target == entt::null) return;
        auto& reg = ScenePtr->GetRegistry();
        if (!reg.all_of<ScriptComponent>(target)) return;
        auto& sc = reg.get<ScriptComponent>(target);
        if (sc.Instance)
            sc.Instance->OnEvent(eventName, value);
    }

    void ScriptEventBusProxy::Broadcast(const std::string& eventName, float value)
    {
        if (!ScenePtr) return;
        auto& reg = ScenePtr->GetRegistry();
        reg.view<ScriptComponent>().each([&](entt::entity e, ScriptComponent& sc)
            {
                if (sc.Instance && e != SenderEntity)
                    sc.Instance->OnEvent(eventName, value);
            });
    }


    // ─────────────────────────────────────────────────────────────────────────
    // ScriptBase — Context e Accessors (não-inline)
    // ─────────────────────────────────────────────────────────────────────────

    void ScriptBase::SetContext(const ScriptContext& ctx)
    {
        m_Context = ctx;
    }

    const ScriptContext& ScriptBase::GetContext() const
    {
        return m_Context;
    }

    ScriptTransformProxy ScriptBase::GetTransform()
    {
        return { m_Context.Entity, m_Context.ScenePtr };
    }

    ScriptRigidbodyProxy ScriptBase::GetRigidbody()
    {
        return { m_Context.Entity, m_Context.ScenePtr };
    }

    ScriptCharacterProxy ScriptBase::GetCharacter()
    {
        return { m_Context.Entity, m_Context.ScenePtr };
    }

    ScriptEventBusProxy ScriptBase::GetEventBus()
    {
        return { m_Context.Entity, m_Context.ScenePtr };
    }

    ScriptRigidbodyProxy ScriptBase::GetPhysics()
    {
        return GetRigidbody();
    }

    void ScriptBase::DestroyEntitySafe(entt::entity target)
    {
        if (!m_Context.ScenePtr || target == entt::null) return;
        auto& reg = m_Context.ScenePtr->GetRegistry();
        if (!reg.valid(target)) return;

        // Remove o Rigidbody body do Jolt antes de destruir a entity
        if (auto* rb = reg.try_get<RigidbodyComponent>(target))
        {
            if (rb->IsCreated)
            {
                auto* bi = static_cast<JPH::BodyInterface*>(
                    PhysicsSystem::Get().GetBodyInterfacePtr());
                if (bi)
                {
                    JPH::BodyID id(rb->BodyID);
                    if (!id.IsInvalid()) { bi->RemoveBody(id); bi->DestroyBody(id); }
                }
                rb->IsCreated = false;
            }
        }

        // Remove o static body implícito (ColliderComponent sem Rigidbody)
        if (auto* col = reg.try_get<ColliderComponent>(target))
        {
            if (col->IsStaticCreated)
            {
                auto* bi = static_cast<JPH::BodyInterface*>(
                    PhysicsSystem::Get().GetBodyInterfacePtr());
                if (bi)
                {
                    JPH::BodyID id(col->StaticBodyID);
                    if (!id.IsInvalid()) { bi->RemoveBody(id); bi->DestroyBody(id); }
                }
                col->IsStaticCreated = false;
            }
        }

        m_Context.ScenePtr->DestroyEntity(target);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // On-screen messages — fila estática compartilhada por todos os scripts
    // ─────────────────────────────────────────────────────────────────────────
    static std::vector<ScriptScreenMessage> s_ScreenMessages;

    void ScriptBase::PrintOnScreen(const char* msg, float duration)
    {
        if (!msg) return;
        s_ScreenMessages.push_back({ std::string(msg), duration });
        AXE_CORE_INFO("[Script] {}", msg);
    }

    const std::vector<ScriptScreenMessage>& ScriptBase::GetScreenMessages()
    {
        return s_ScreenMessages;
    }

    void ScriptBase::TickScreenMessages(float dt)
    {
        for (auto& m : s_ScreenMessages) m.TimeLeft -= dt;
        s_ScreenMessages.erase(
            std::remove_if(s_ScreenMessages.begin(), s_ScreenMessages.end(),
                [](const ScriptScreenMessage& m) { return m.TimeLeft <= 0.f; }),
            s_ScreenMessages.end());
    }

    void ScriptBase::ClearScreenMessages()
    {
        s_ScreenMessages.clear();
    }

} // namespace axe