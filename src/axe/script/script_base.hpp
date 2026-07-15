#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <string>
#include <functional>
#include <entt/entt.hpp>

namespace axe
{
    class Scene;
    class GameCamera;

    // Contexto injetado no script — acesso a tudo que o engine oferece
    // Ponteiros para os arrays de teclas da axe.dll
    // Evita cópia de 1024 bytes por frame e elimina problema de instâncias estáticas separadas
    struct AXE_API ScriptInputSnapshot
    {
        const bool* Keys = nullptr; // aponta para Input::s_CurrentKeys  da axe.dll
        const bool* PrevKeys = nullptr; // aponta para Input::s_PreviousKeys da axe.dll

        bool  GetKey(int k)     const { return Keys && k >= 0 && k < 512 ? Keys[k] : false; }
        bool  GetKeyDown(int k) const { return Keys && k >= 0 && k < 512 ? Keys[k] && !PrevKeys[k] : false; }
        bool  GetKeyUp(int k)   const { return PrevKeys && k >= 0 && k < 512 ? !Keys[k] && PrevKeys[k] : false; }
        float GetAxis(const std::string& n) const
        {
            if (n == "Horizontal") return (GetKey(68) ? 1.f : 0.f) - (GetKey(65) ? 1.f : 0.f);
            if (n == "Vertical")   return (GetKey(87) ? 1.f : 0.f) - (GetKey(83) ? 1.f : 0.f);
            return 0.f;
        }
    };

    struct ScriptContext
    {
        entt::entity        Entity = entt::null;
        Scene* ScenePtr = nullptr;
        ScriptInputSnapshot Input;
        class GameCamera* CameraPtr = nullptr; // set pelo EditorLayer em Play
    };

    // ─────────────────────────────────────────────────────────────────────────

    // ── Proxy de Camera ───────────────────────────────────────────────────────
    struct AXE_API ScriptCameraProxy
    {
        GameCamera* CameraPtr;
        Scene* ScenePtr;

        void Shake(float intensity, float duration);
        void Follow(entt::entity target);
        void StopFollow();
        void SetFOV(float fov);
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

    // ── Proxy de Animação (AnimGraph) ─────────────────────────────────────────
    //
    // A ÚNICA porta entre o gameplay e a animação.
    //
    // O script NÃO conhece estados, clipes nem transições — ele só escreve
    // valores: "Speed = 320", "IsGrounded = false", dispara "Attack". O
    // AnimGraph decide o resto.
    //
    // É essa separação que permite reeditar a máquina de estados inteira, no
    // editor de nós, sem tocar numa linha de script. Se o script chamasse
    // Play("run") direto, a lógica de animação estaria espalhada pelo
    // gameplay — e é exatamente o que o AnimGraph existe pra evitar.
    struct AXE_API ScriptAnimProxy
    {
        entt::entity Entity;
        Scene* ScenePtr;

        void SetFloat(const std::string& name, float value);
        void SetInt(const std::string& name, int value);
        void SetBool(const std::string& name, bool value);

        // Pulso, não flag. Disparado uma vez, consumido pela transição que o
        // usou. O script NÃO precisa lembrar de desligar no frame seguinte.
        void SetTrigger(const std::string& name);

        float GetFloat(const std::string& name) const;
        bool  GetBool(const std::string& name) const;

        // Nome do estado atual — útil pra debug e pra lógica que depende do
        // que o personagem está fazendo (ex: não pular durante um ataque).
        std::string GetCurrentState() const;
    };

    // ── Proxy de Particle System ──────────────────────────────────────────────
    struct AXE_API ScriptParticleProxy
    {
        entt::entity Entity;
        Scene* ScenePtr;

        void Play();           // liga Playing = true
        void Stop();           // liga Playing = false
        void Restart();        // limpa todos os pools e retoma emissão
        void Burst(int emitterIndex, int count); // dispara N partículas num emitter específico
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
    // ─────────────────────────────────────────────────────────────────────────
    // Mensagem on-screen — escrita pelos scripts via PrintOnScreen,
    // lida pelo editor e renderizada no viewport durante o Play.
    // ─────────────────────────────────────────────────────────────────────────
    struct AXE_API ScriptScreenMessage
    {
        std::string Text;
        float       TimeLeft = 3.0f;
    };

    // ScriptBase — classe que todo script gerado herda
    // ─────────────────────────────────────────────────────────────────────────
    class AXE_API ScriptBase
    {
    public:
        virtual ~ScriptBase() = default;

        // ── On-screen print (Print String node) ──────────────────────────────
        // Usa const char* para evitar incompatibilidade de layout de std::string
        // cross-DLL quando script DLL e axe.dll usam CRTs diferentes.
        static void PrintOnScreen(const char* msg, float duration = 3.0f);
        static const std::vector<ScriptScreenMessage>& GetScreenMessages();
        static void TickScreenMessages(float dt);
        static void ClearScreenMessages();

        // ── Ciclo de vida ─────────────────────────────────────────────────────
        virtual void OnStart() {}
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnEnd() {}

        // PreUpdate: chamado antes de OnUpdate pelo ScriptWorld.
        // Atualiza m_Context.Input — caminho único, sem membros duplicados.
        // Não-inline: implementado em axe.dll para garantir acesso ao offset
        // correto de m_Context, independente de quando a DLL do script foi compilada.
        void PreUpdate(const bool* keys, const bool* prevKeys);

        // _GetKey / _GetKeyDown / _GetKeyUp / _GetAxis — usados pelo código gerado.
        // Não-inline: delegam para m_Context.Input via axe.dll.
        // Isso garante que a DLL do script nunca acesse offsets de membros diretamente,
        // eliminando o bug de layout quando axe.dll é recompilada.
        bool  _GetKey(int k)     const;
        bool  _GetKeyDown(int k) const;
        bool  _GetKeyUp(int k)   const;
        float _GetAxis(int axis) const;  // 0=Horizontal, 1=Vertical

        // ── Eventos de física ─────────────────────────────────────────────────
        virtual void OnCollision(entt::entity other) {}
        virtual void OnTriggerEnter(entt::entity other) {}
        virtual void OnTriggerExit(entt::entity other) {}

        // ── Eventos customizados entre objetos ────────────────────────────────
        virtual void OnEvent(const std::string& eventName, float value) {}

        // ── Contexto — injetado pelo ScriptWorld antes de qualquer chamada ────
        // Não-inline: implementados em axe.dll para garantir acesso ao offset
        // correto de m_Context, independente de quando a DLL do script foi compilada.
        void SetContext(const ScriptContext& ctx);
        const ScriptContext& GetContext() const;
        void SetInputPointers(const bool* keys, const bool* prevKeys);

        // ── Accessors de componente para uso no código gerado ─────────────────
        ScriptTransformProxy  GetTransform();
        ScriptRigidbodyProxy  GetRigidbody();
        ScriptAnimProxy       GetAnim();
        ScriptParticleProxy   GetParticleSystem();
        ScriptCameraProxy     GetCamera();

        // Chamado pelo ScriptWorld antes de cada OnUpdate pra manter
        // o CameraPtr atualizado sem recriar o contexto inteiro.
        void UpdateCameraInContext(class GameCamera* cam) { m_Context.CameraPtr = cam; }
        ScriptCharacterProxy  GetCharacter();
        ScriptEventBusProxy   GetEventBus();
        ScriptRigidbodyProxy  GetPhysics();

        // Destrói uma entity removendo também o body físico do Jolt.
        // Usar no lugar de ScenePtr->DestroyEntity() para evitar deixar
        // ghost bodies na simulação.
        void DestroyEntitySafe(entt::entity target);

    protected:
        ScriptContext    m_Context;
        // Nota: NÃO há m_Keys / m_PrevKeys aqui.
        // Todo o input passa por m_Context.Input (ScriptInputSnapshot).
        // Isso evita o bug de layout entre DLLs: se axe.dll for recompilada
        // e o tamanho de ScriptContext mudar, a DLL do script não precisa ser
        // recompilada para que _GetKey/_GetAxis funcionem corretamente,
        // pois eles são não-inline e resolvidos em axe.dll.
    };

    // Assinatura da função exportada pela DLL
    // extern "C" ScriptBase* CreateScript();
    using CreateScriptFn = ScriptBase * (*)();

} // namespace axe