#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <string>
#include <functional>
#include <entt/entt.hpp>

namespace axe
{
    class Scene;

    // Contexto injetado no script — acesso a tudo que o engine oferece
    struct ScriptContext
    {
        entt::entity Entity = entt::null;
        Scene* ScenePtr = nullptr;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Proxies de componente
    // Cada proxy busca o componente via ScenePtr no momento da chamada,
    // evitando guardar referência direta (entt invalida refs após
    // addComponent/removeComponent).
    // ─────────────────────────────────────────────────────────────────────────

    // ── Proxy de Transform ────────────────────────────────────────────────────
    struct AXE_API ScriptTransformProxy
    {
        entt::entity Entity;
        Scene* ScenePtr;

        glm::vec3 GetPosition() const;
        glm::vec3 GetRotation() const;  // euler em graus
        glm::vec3 GetScale()    const;

        void SetPosition(const glm::vec3& pos);
        void SetRotation(const glm::vec3& euler);  // euler em graus
        void SetScale(const glm::vec3& scale);

        // Helpers de movimento — usados pelos nodes Move / Rotate do grafo
        void Translate(const glm::vec3& delta);
        void Rotate(const glm::vec3& axis, float radians);
    };

    // ── Proxy de Rigidbody ────────────────────────────────────────────────────
    struct AXE_API ScriptRigidbodyProxy
    {
        entt::entity Entity;
        Scene* ScenePtr;

        glm::vec3 GetVelocity() const;
        float     GetMass()     const;

        void SetVelocity(const glm::vec3& vel);
        void AddForce(const glm::vec3& force);
    };

    // ── Proxy de CharacterController ─────────────────────────────────────────
    struct AXE_API ScriptCharacterProxy
    {
        entt::entity Entity;
        Scene* ScenePtr;

        bool      IsGrounded()  const;
        glm::vec3 GetVelocity() const;
        float     GetMaxSpeed() const;

        void Move(const glm::vec3& direction, float speed);
        void Jump(float force);
    };

    // ── Proxy de EventBus ─────────────────────────────────────────────────────
    struct AXE_API ScriptEventBusProxy
    {
        entt::entity SenderEntity;
        Scene* ScenePtr;

        // Envia evento para uma entidade específica
        void Send(entt::entity target, const std::string& eventName, float value = 0.0f);

        // Broadcast para todas as entidades com ScriptComponent
        void Broadcast(const std::string& eventName, float value = 0.0f);
    };

    // ─────────────────────────────────────────────────────────────────────────
    // ScriptBase — classe que todo script gerado herda
    // ─────────────────────────────────────────────────────────────────────────
    class AXE_API ScriptBase
    {
    public:
        virtual ~ScriptBase() = default;

        // ── Ciclo de vida ─────────────────────────────────────────────────────
        virtual void OnStart() {}
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnEnd() {}

        // ── Eventos de física ─────────────────────────────────────────────────
        virtual void OnCollision(entt::entity other) {}
        virtual void OnTriggerEnter(entt::entity other) {}
        virtual void OnTriggerExit(entt::entity other) {}

        // ── Eventos customizados entre objetos ────────────────────────────────
        virtual void OnEvent(const std::string& eventName, float value) {}

        // ── Contexto — injetado pelo ScriptWorld antes de qualquer chamada ────
        void SetContext(const ScriptContext& ctx) { m_Context = ctx; }
        const ScriptContext& GetContext() const { return m_Context; }

        // ── Accessors de componente para uso no código gerado ─────────────────
        ScriptTransformProxy  GetTransform() { return { m_Context.Entity, m_Context.ScenePtr }; }
        ScriptRigidbodyProxy  GetRigidbody() { return { m_Context.Entity, m_Context.ScenePtr }; }
        ScriptCharacterProxy  GetCharacter() { return { m_Context.Entity, m_Context.ScenePtr }; }
        ScriptEventBusProxy   GetEventBus() { return { m_Context.Entity, m_Context.ScenePtr }; }

        // GetPhysics() — alias de GetRigidbody(), mantém compatibilidade com
        // código gerado por versões anteriores do ScriptGraphCompiler
        ScriptRigidbodyProxy  GetPhysics() { return GetRigidbody(); }

    protected:
        ScriptContext m_Context;
    };

    // Assinatura da função exportada pela DLL
    // extern "C" ScriptBase* CreateScript();
    using CreateScriptFn = ScriptBase * (*)();

} // namespace axe