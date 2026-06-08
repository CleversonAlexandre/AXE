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

    // Classe base gerada pelo ScriptGraphCompiler e compilada em DLL
    // Todo script herda desta e sobrescreve os métodos que usar
    class ScriptBase
    {
    public:
        virtual ~ScriptBase() = default;

        // Ciclo de vida
        virtual void OnStart() {}
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnEnd() {}

        // Eventos de física
        virtual void OnCollision(entt::entity other) {}
        virtual void OnTriggerEnter(entt::entity other) {}
        virtual void OnTriggerExit(entt::entity other) {}

        // Eventos customizados entre objetos
        virtual void OnEvent(const std::string& eventName, float value) {}

        // Contexto injetado pelo ScriptWorld antes de qualquer chamada
        void SetContext(const ScriptContext& ctx) { m_Context = ctx; }
        const ScriptContext& GetContext() const { return m_Context; }

    protected:
        ScriptContext m_Context;
    };

    // Assinatura da função exportada pela DLL
    // extern "C" ScriptBase* CreateScript();
    using CreateScriptFn = ScriptBase * (*)();

} // namespace axe