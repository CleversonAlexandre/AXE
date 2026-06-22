// script_base.cpp
// Implementação real de ScriptBase — antes deste fix, este arquivo continha
// (por engano, provavelmente copiado de script_asset.cpp em algum momento)
// uma cópia exata do conteúdo de script_asset.cpp. Isso causava DOIS
// problemas simultâneos:
//   1) "already defined in script_asset.obj" para toda função de ScriptAsset/
//      ScriptComponentDef — porque este .obj definia os MESMOS símbolos.
//   2) "unresolved external symbol" para ScriptBase::SetContext/PreUpdate (e
//      silenciosamente para os outros métodos não-inline da classe) — porque
//      a implementação de verdade nunca existiu em lugar nenhum do projeto.
//
// Os proxies (ScriptTransformProxy::GetPosition, ScriptRigidbodyProxy::
// AddForce, etc.) já estão implementados abaixo, incluindo
// ScriptEventBusProxy::Send/Broadcast — usa ScriptWorld::DispatchEvent, que
// é stateless (nenhum membro de instância em ScriptWorld), então um
// ScriptWorld{} temporário construído aqui mesmo já resolve, sem singleton.

#include "axe/script/script_base.hpp"
#include "axe/script/script_world.hpp"
#include "axe/script/script_component.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include "axe/physics/physics_components.hpp"
#include "axe/physics/physics_system.hpp"
#include <vector>
#include <algorithm>

// Jolt — necessário para DestroyEntitySafe remover bodies do simulador
#ifdef JPH_DEBUG_RENDERER
#undef JPH_DEBUG_RENDERER
#endif
#include "axe/physics/jolt_config.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

namespace axe
{
    // ── Mensagens on-screen (Print String) ──────────────────────────────────
    // Estado estático compartilhado por TODAS as instâncias de ScriptBase
    // (e por todas as DLLs de script, já que isso vive em axe.dll) — é
    // exatamente a razão de PrintOnScreen/GetScreenMessages/etc. serem
    // static: o editor lê essa lista pra desenhar os textos no viewport,
    // sem precisar conhecer qual script específico chamou.
    static std::vector<ScriptScreenMessage> s_ScreenMessages;

    void ScriptBase::PrintOnScreen(const char* msg, float duration)
    {
        if (!msg) return;
        ScriptScreenMessage m;
        m.Text = msg;
        m.TimeLeft = duration;
        s_ScreenMessages.push_back(std::move(m));
    }

    const std::vector<ScriptScreenMessage>& ScriptBase::GetScreenMessages()
    {
        return s_ScreenMessages;
    }

    void ScriptBase::TickScreenMessages(float dt)
    {
        for (auto& m : s_ScreenMessages)
            m.TimeLeft -= dt;

        s_ScreenMessages.erase(
            std::remove_if(s_ScreenMessages.begin(), s_ScreenMessages.end(),
                [](const ScriptScreenMessage& m) { return m.TimeLeft <= 0.0f; }),
            s_ScreenMessages.end());
    }

    void ScriptBase::ClearScreenMessages()
    {
        s_ScreenMessages.clear();
    }

    // ── Input ────────────────────────────────────────────────────────────────
    // Não-inline de propósito (ver comentário no header): garante que o
    // offset de m_Context seja sempre resolvido pela axe.dll, nunca pela DLL
    // do script — evita o bug clássico de layout cross-DLL se ScriptContext
    // mudar de tamanho entre recompilações.

    void ScriptBase::PreUpdate(const bool* keys, const bool* prevKeys)
    {
        m_Context.Input.Keys = keys;
        m_Context.Input.PrevKeys = prevKeys;
    }

    void ScriptBase::SetInputPointers(const bool* keys, const bool* prevKeys)
    {
        m_Context.Input.Keys = keys;
        m_Context.Input.PrevKeys = prevKeys;
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
        // 0 = Horizontal, 1 = Vertical (ver comentário na declaração, header)
        return m_Context.Input.GetAxis(axis == 0 ? "Horizontal" : "Vertical");
    }

    // ── Contexto ──────────────────────────────────────────────────────────────

    void ScriptBase::SetContext(const ScriptContext& ctx)
    {
        m_Context = ctx;
    }

    const ScriptContext& ScriptBase::GetContext() const
    {
        return m_Context;
    }

    // ── Accessors de componente (constroem o proxy; a lógica de cada
    //    operação vive nos métodos do próprio proxy — ainda não
    //    implementados, ver nota no topo do arquivo) ──────────────────────────

    ScriptTransformProxy ScriptBase::GetTransform()
    {
        return ScriptTransformProxy{ m_Context.Entity, m_Context.ScenePtr };
    }

    ScriptRigidbodyProxy ScriptBase::GetRigidbody()
    {
        return ScriptRigidbodyProxy{ m_Context.Entity, m_Context.ScenePtr };
    }

    ScriptCharacterProxy ScriptBase::GetCharacter()
    {
        return ScriptCharacterProxy{ m_Context.Entity, m_Context.ScenePtr };
    }

    ScriptEventBusProxy ScriptBase::GetEventBus()
    {
        return ScriptEventBusProxy{ m_Context.Entity, m_Context.ScenePtr };
    }

    ScriptRigidbodyProxy ScriptBase::GetPhysics()
    {
        return ScriptRigidbodyProxy{ m_Context.Entity, m_Context.ScenePtr };
    }

    // ── ScriptTransformProxy ──────────────────────────────────────────────────
    // Rotation é exposta em GRAUS (ver header), mas Transform::Rotation
    // internamente guarda RADIANOS (usado direto em glm::quat(Rotation), que
    // trata o vec3 como euler em radianos) — por isso a conversão aqui.

    glm::vec3 ScriptTransformProxy::GetPosition() const
    {
        auto* tc = ScenePtr ? ScenePtr->GetRegistry().try_get<TransformComponent>(Entity) : nullptr;
        return tc ? tc->Data.Position : glm::vec3(0.0f);
    }

    glm::vec3 ScriptTransformProxy::GetRotation() const
    {
        auto* tc = ScenePtr ? ScenePtr->GetRegistry().try_get<TransformComponent>(Entity) : nullptr;
        return tc ? glm::degrees(tc->Data.Rotation) : glm::vec3(0.0f);
    }

    glm::vec3 ScriptTransformProxy::GetScale() const
    {
        auto* tc = ScenePtr ? ScenePtr->GetRegistry().try_get<TransformComponent>(Entity) : nullptr;
        return tc ? tc->Data.Scale : glm::vec3(1.0f);
    }

    void ScriptTransformProxy::SetPosition(const glm::vec3& pos)
    {
        auto* tc = ScenePtr ? ScenePtr->GetRegistry().try_get<TransformComponent>(Entity) : nullptr;
        if (tc) tc->Data.Position = pos;
    }

    void ScriptTransformProxy::SetRotation(const glm::vec3& euler)
    {
        auto* tc = ScenePtr ? ScenePtr->GetRegistry().try_get<TransformComponent>(Entity) : nullptr;
        if (tc) tc->Data.Rotation = glm::radians(euler);
    }

    void ScriptTransformProxy::SetScale(const glm::vec3& scale)
    {
        auto* tc = ScenePtr ? ScenePtr->GetRegistry().try_get<TransformComponent>(Entity) : nullptr;
        if (tc) tc->Data.Scale = scale;
    }

    void ScriptTransformProxy::Translate(const glm::vec3& delta)
    {
        auto* tc = ScenePtr ? ScenePtr->GetRegistry().try_get<TransformComponent>(Entity) : nullptr;
        if (tc) tc->Data.Position += delta;
    }

    void ScriptTransformProxy::Rotate(const glm::vec3& axis, float radians)
    {
        auto* tc = ScenePtr ? ScenePtr->GetRegistry().try_get<TransformComponent>(Entity) : nullptr;
        if (!tc) return;
        // Combina a rotação atual (euler, radianos) com a rotação adicional
        // via quaternion, pra não acumular erro de gimbal lock — mesmo
        // cuidado já tomado em Transform::GetMatrix().
        glm::quat cur = glm::quat(tc->Data.Rotation);
        glm::quat delta = glm::angleAxis(radians, glm::normalize(axis));
        tc->Data.Rotation = glm::eulerAngles(delta * cur);
    }

    // ── ScriptRigidbodyProxy ──────────────────────────────────────────────────
    // AddForce/SetVelocity NÃO tocam o Jolt direto (o proxy não tem acesso a
    // ele) — só marcam PendingForce/PendingVelocity + a flag Needs*, que
    // PhysicsWorld::OnUpdate consome no início do frame seguinte, antes do
    // Step (ver physics_world.cpp). GetVelocity lê CurrentVelocity, que esse
    // mesmo OnUpdate mantém sincronizado com o Jolt todo frame.

    glm::vec3 ScriptRigidbodyProxy::GetVelocity() const
    {
        auto* rb = ScenePtr ? ScenePtr->GetRegistry().try_get<RigidbodyComponent>(Entity) : nullptr;
        return rb ? rb->CurrentVelocity : glm::vec3(0.0f);
    }

    float ScriptRigidbodyProxy::GetMass() const
    {
        auto* rb = ScenePtr ? ScenePtr->GetRegistry().try_get<RigidbodyComponent>(Entity) : nullptr;
        return rb ? rb->Mass : 0.0f;
    }

    void ScriptRigidbodyProxy::SetVelocity(const glm::vec3& vel)
    {
        auto* rb = ScenePtr ? ScenePtr->GetRegistry().try_get<RigidbodyComponent>(Entity) : nullptr;
        if (!rb) return;
        rb->PendingVelocity = vel;
        rb->NeedsVelocitySet = true;
    }

    void ScriptRigidbodyProxy::AddForce(const glm::vec3& force)
    {
        auto* rb = ScenePtr ? ScenePtr->GetRegistry().try_get<RigidbodyComponent>(Entity) : nullptr;
        if (!rb) return;
        rb->PendingForce = force;
        rb->NeedsForceApply = true;
    }

    // ── ScriptCharacterProxy ──────────────────────────────────────────────────
    // Move só seta cc.Velocity.x/z — PhysicsWorld::OnUpdate lê isso, aplica no
    // CharacterVirtual do Jolt e zera de novo no fim do frame (ver
    // physics_world.cpp), então precisa ser chamado a cada frame que o
    // personagem deva continuar se movendo (mesmo padrão de "input contínuo"
    // que qualquer character controller usa). Jump usa cc.JumpForce como o
    // valor de impulso vertical — aqui sobrescrevemos com o "force" recebido
    // antes de marcar WantsJump, já que não existe um campo "PendingJumpForce"
    // separado no componente.

    bool ScriptCharacterProxy::IsGrounded() const
    {
        auto* cc = ScenePtr ? ScenePtr->GetRegistry().try_get<CharacterControllerComponent>(Entity) : nullptr;
        return cc ? cc->IsGrounded : false;
    }

    glm::vec3 ScriptCharacterProxy::GetVelocity() const
    {
        auto* cc = ScenePtr ? ScenePtr->GetRegistry().try_get<CharacterControllerComponent>(Entity) : nullptr;
        return cc ? cc->Velocity : glm::vec3(0.0f);
    }

    float ScriptCharacterProxy::GetMaxSpeed() const
    {
        auto* cc = ScenePtr ? ScenePtr->GetRegistry().try_get<CharacterControllerComponent>(Entity) : nullptr;
        return cc ? cc->MaxSpeed : 0.0f;
    }

    void ScriptCharacterProxy::Move(const glm::vec3& direction, float speed)
    {
        auto* cc = ScenePtr ? ScenePtr->GetRegistry().try_get<CharacterControllerComponent>(Entity) : nullptr;
        if (!cc) return;
        cc->Velocity.x = direction.x * speed;
        cc->Velocity.z = direction.z * speed;
    }

    void ScriptCharacterProxy::Jump(float force)
    {
        auto* cc = ScenePtr ? ScenePtr->GetRegistry().try_get<CharacterControllerComponent>(Entity) : nullptr;
        if (!cc) return;
        cc->JumpForce = force;
        cc->WantsJump = true;
    }

    // ── ScriptEventBusProxy ───────────────────────────────────────────────────
    // ScriptWorld::DispatchEvent já existe e faz exatamente o que precisamos
    // (acha o ScriptComponent da entity alvo, chama Instance->OnEvent) — e é
    // stateless de verdade: nenhum método de ScriptWorld lê/escreve nenhum
    // membro de instância (não tem nenhum `m_` na classe inteira). Por isso
    // dá pra construir um ScriptWorld{} temporário aqui mesmo, sem precisar
    // de singleton nem de um ponteiro guardado na Scene — qualquer instância
    // de ScriptWorld é equivalente a qualquer outra.
    void ScriptEventBusProxy::Send(entt::entity target, const std::string& eventName, float value)
    {
        if (!ScenePtr || target == entt::null) return;
        ScriptWorld{}.DispatchEvent(*ScenePtr, target, eventName, value);
    }

    void ScriptEventBusProxy::Broadcast(const std::string& eventName, float value)
    {
        if (!ScenePtr) return;
        // Pra TODA entity com ScriptComponent na cena — inclui o próprio
        // remetente de propósito (broadcast é "todo mundo escutando",
        // sem exceção implícita pra quem disparou; se o script quiser se
        // auto-excluir, ele mesmo decide isso checando o evento recebido).
        auto& reg = ScenePtr->GetRegistry();
        ScriptWorld world;
        for (auto entity : reg.view<ScriptComponent>())
            world.DispatchEvent(*ScenePtr, entity, eventName, value);
    }

    // ── Destruição segura ─────────────────────────────────────────────────────
    // Remove o body do Jolt ANTES de destruir a entity — sem isso, o
    // simulador físico fica com um "ghost body" referenciando uma entity que
    // não existe mais (Scene::DestroyEntity só faz m_Registry.destroy, não
    // sabe nada sobre Jolt). Cobre os dois casos: Rigidbody (corpo dinâmico)
    // e o static body implícito criado pra um Collider sem Rigidbody.
    // PhysicsSystem::Get() é um singleton global — não precisa de nenhum
    // acessor novo na Scene pra chegar nele.
    void ScriptBase::DestroyEntitySafe(entt::entity target)
    {
        if (!m_Context.ScenePtr || target == entt::null) return;
        auto& reg = m_Context.ScenePtr->GetRegistry();
        if (!reg.valid(target)) return;

        auto* bi = static_cast<JPH::BodyInterface*>(PhysicsSystem::Get().GetBodyInterfacePtr());

        if (auto* rb = reg.try_get<RigidbodyComponent>(target))
        {
            if (rb->IsCreated && bi)
            {
                JPH::BodyID id(rb->BodyID);
                if (!id.IsInvalid()) { bi->RemoveBody(id); bi->DestroyBody(id); }
                rb->IsCreated = false;
            }
        }

        if (auto* col = reg.try_get<ColliderComponent>(target))
        {
            if (col->IsStaticCreated && bi)
            {
                JPH::BodyID id(col->StaticBodyID);
                if (!id.IsInvalid()) { bi->RemoveBody(id); bi->DestroyBody(id); }
                col->IsStaticCreated = false;
            }
        }

        m_Context.ScenePtr->DestroyEntity(target);
    }

} // namespace axe