#include "script_base.hpp"
#include "script_component.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/physics/physics_components.hpp"
#include "axe/log/log.hpp"

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

    void ScriptCharacterProxy::Move(const glm::vec3& direction, float speed)
    {
        if (!ScenePtr || Entity == entt::null) return;
        auto& reg = ScenePtr->GetRegistry();
        if (!reg.all_of<CharacterControllerComponent>(Entity)) return;
        auto& cc = reg.get<CharacterControllerComponent>(Entity);
        // Acumula velocidade desejada — o PhysicsWorld lê e aplica ao JPH Character
        glm::vec3 dir = (glm::length(direction) > 0.0001f)
            ? glm::normalize(direction) : glm::vec3(0);
        cc.Velocity.x = dir.x * speed;
        cc.Velocity.z = dir.z * speed;
        // Y preservado para gravidade gerenciada pelo Jolt
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

} // namespace axe