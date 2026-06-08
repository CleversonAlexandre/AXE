#include "script_world.hpp"
#include "script_component.hpp"
#include "dll_loader.hpp"
#include "axe/scene/scene.hpp"
#include "axe/log/log.hpp"

namespace axe
{
    void ScriptWorld::OnSceneStart(Scene& scene)
    {
        auto& registry = scene.GetRegistry();

        registry.view<ScriptComponent>().each([&](entt::entity entity, ScriptComponent& sc)
            {
                // Carrega a DLL se ainda não estiver carregada
                if (!sc.IsLoaded && !sc.DllPath.empty())
                {
                    sc.DllHandle = DllLoader::Load(sc.DllPath);
                    if (sc.DllHandle)
                    {
                        ScriptBase* raw = DllLoader::CreateInstance(sc.DllHandle);
                        if (raw)
                        {
                            sc.Instance = std::shared_ptr<ScriptBase>(raw);
                            sc.IsLoaded = true;
                        }
                    }
                }

                if (!sc.Instance) return;

                // Injeta contexto
                ScriptContext ctx;
                ctx.Entity = entity;
                ctx.ScenePtr = &scene;
                sc.Instance->SetContext(ctx);

                // Chama OnStart
                sc.Instance->OnStart();
                AXE_CORE_INFO("ScriptWorld: OnStart — entity {}", (uint32_t)entity);
            });
    }

    void ScriptWorld::OnSceneUpdate(Scene& scene, float deltaTime)
    {
        auto& registry = scene.GetRegistry();

        registry.view<ScriptComponent>().each([&](entt::entity entity, ScriptComponent& sc)
            {
                if (!sc.Instance) return;
                sc.Instance->OnUpdate(deltaTime);
            });
    }

    void ScriptWorld::OnSceneStop(Scene& scene)
    {
        auto& registry = scene.GetRegistry();

        registry.view<ScriptComponent>().each([&](entt::entity entity, ScriptComponent& sc)
            {
                if (sc.Instance)
                {
                    sc.Instance->OnEnd();
                    sc.Instance.reset();
                }

                if (sc.DllHandle)
                {
                    DllLoader::Unload(sc.DllHandle);
                    sc.DllHandle = nullptr;
                }

                sc.IsLoaded = false;
                AXE_CORE_INFO("ScriptWorld: OnEnd — entity {}", (uint32_t)entity);
            });
    }

    void ScriptWorld::DispatchEvent(Scene& scene, entt::entity target,
        const std::string& eventName, float value)
    {
        auto& registry = scene.GetRegistry();

        if (auto* sc = registry.try_get<ScriptComponent>(target))
        {
            if (sc->Instance)
                sc->Instance->OnEvent(eventName, value);
        }
    }

    void ScriptWorld::DispatchCollision(Scene& scene,
        entt::entity a, entt::entity b)
    {
        auto& registry = scene.GetRegistry();

        if (auto* sc = registry.try_get<ScriptComponent>(a))
            if (sc->Instance) sc->Instance->OnCollision(b);

        if (auto* sc = registry.try_get<ScriptComponent>(b))
            if (sc->Instance) sc->Instance->OnCollision(a);
    }

} // namespace axe