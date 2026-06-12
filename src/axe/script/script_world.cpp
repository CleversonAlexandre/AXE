#include "script_world.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include "script_component.hpp"
#include "dll_loader.hpp"
#include "axe/scene/scene.hpp"
#include "axe/physics/physics_components.hpp"
#include "axe/log/log.hpp"
#include "axe/input/input.hpp"

namespace axe
{
    void ScriptWorld::OnSceneStart(Scene& scene)
    {
        auto& registry = scene.GetRegistry();

        int scriptCount = 0;
        registry.view<ScriptComponent>().each([&](entt::entity, ScriptComponent&) { scriptCount++; });
        AXE_CORE_INFO("ScriptWorld::OnSceneStart — {} entidade(s) com ScriptComponent.", scriptCount);

        registry.view<ScriptComponent>().each([&](entt::entity entity, ScriptComponent& sc)
            {
                AXE_CORE_INFO("ScriptWorld: entity={} name='{}' dll='{}' compiled={}",
                    (uint32_t)entity, sc.ScriptName, sc.DllPath, sc.IsCompiled);

                // Sempre reseta para garantir estado limpo ao iniciar Play
                if (sc.Instance) { sc.Instance->OnEnd(); sc.Instance.reset(); }
                if (sc.DllHandle) { DllLoader::Unload(sc.DllHandle); sc.DllHandle = nullptr; }
                sc.IsLoaded = false;

                // Carrega a DLL
                if (!sc.IsLoaded)
                {
                    if (sc.DllPath.empty())
                    {
                        // DllPath vazio — tenta encontrar a DLL pelo nome do script
                        // na pasta temp_scripts ao lado do executável
                        char exeBuf[512] = {};
                        GetModuleFileNameA(nullptr, exeBuf, 512);
                        std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
                        std::filesystem::path candidate = exeDir / "temp_scripts" / (sc.ScriptName + ".dll");
                        if (std::filesystem::exists(candidate))
                        {
                            sc.DllPath = candidate.string();
                            AXE_CORE_INFO("ScriptWorld: DllPath reconstruído → '{}'", sc.DllPath);
                        }
                        else
                        {
                            AXE_CORE_WARN("ScriptWorld: DLL não encontrada para '{}'. Compile o script antes do Play.", sc.ScriptName);
                        }
                    }

                    if (!sc.DllPath.empty())
                    {
                        sc.DllHandle = DllLoader::Load(sc.DllPath);
                        if (sc.DllHandle)
                        {
                            ScriptBase* raw = DllLoader::CreateInstance(sc.DllHandle);
                            if (raw)
                            {
                                sc.Instance = std::shared_ptr<ScriptBase>(raw);
                                sc.IsLoaded = true;
                                AXE_CORE_INFO("ScriptWorld: DLL '{}' carregada.", sc.ScriptName);
                            }
                            else
                            {
                                AXE_CORE_ERROR("ScriptWorld: CreateScript() falhou em '{}'.", sc.DllPath);
                            }
                        }
                        else
                        {
                            AXE_CORE_ERROR("ScriptWorld: falha ao carregar DLL '{}'.", sc.DllPath);
                        }
                    }
                }

                if (!sc.Instance) return;

                // Injeta contexto
                ScriptContext ctx;
                ctx.Entity = entity;
                ctx.ScenePtr = &scene;
                sc.Instance->SetContext(ctx);

                sc.Instance->OnStart();
                AXE_CORE_INFO("ScriptWorld: OnStart — entity {} Instance={}",
                    (uint32_t)entity, (void*)sc.Instance.get());
            });
    }

    void ScriptWorld::OnSceneUpdate(Scene& scene, float deltaTime)
    {
        auto& registry = scene.GetRegistry();

        // ── Atualiza scripts ──────────────────────────────────────────────────
        int updated = 0;
        registry.view<ScriptComponent>().each([&](entt::entity entity, ScriptComponent& sc)
            {
                static int s_Frame = 0;
                s_Frame++;
                if (s_Frame <= 3)  // só loga nos primeiros 3 frames
                    AXE_CORE_INFO("ScriptWorld::OnSceneUpdate frame={} entity={} Instance={}",
                        s_Frame, (uint32_t)entity, (void*)sc.Instance.get());

                if (!sc.Instance)
                {
                    AXE_CORE_WARN("ScriptWorld::OnSceneUpdate: entity {} sem Instance!", (uint32_t)entity);
                    return;
                }
                updated++;
                // PreUpdate: copia ponteiros diretamente para m_Keys/m_PrevKeys da DLL
                const bool* cur = axe::Input::GetCurrentKeys();
                const bool* prev = axe::Input::GetPreviousKeys();
                static int s_pre = 0;
                if (s_pre++ < 3)
                    AXE_CORE_INFO("PreUpdate cur={} W={}", (void*)cur, cur ? (bool)cur[87] : false);
                sc.Instance->PreUpdate(cur, prev);
                sc.Instance->OnUpdate(deltaTime);
            });
        if (updated == 0)
            AXE_CORE_WARN("ScriptWorld::OnSceneUpdate — 0 scripts rodando!");

        // ── Consome pending physics commands gerados pelos proxies ────────────
        // (AddForce / SetVelocity escritos pelo ScriptRigidbodyProxy durante
        //  OnUpdate; aplicados aqui depois de todos os scripts rodarem,
        //  antes do PhysicsWorld::OnUpdate do mesmo frame)
        registry.view<RigidbodyComponent>().each([&](entt::entity entity, RigidbodyComponent& rb)
            {
                if (!rb.IsCreated) return;

                if (rb.NeedsForceApply)
                {
                    // PhysicsSystem é singleton acessível globalmente
                    // ou via referência passada — aqui usamos o pattern que já
                    // existe no engine: PhysicsSystem::Get().AddForce(...)
                    // Se o seu engine usa outra forma, ajuste aqui.
                    // Por segurança deixamos um log e zeramos os flags:
                    AXE_CORE_INFO("ScriptWorld: AddForce entity {} → ({},{},{})",
                        (uint32_t)entity,
                        rb.PendingForce.x, rb.PendingForce.y, rb.PendingForce.z);
                    // TODO: PhysicsSystem::Get().AddForce(entity, scene, rb.PendingForce);
                    rb.PendingForce = {};
                    rb.NeedsForceApply = false;
                }

                if (rb.NeedsVelocitySet)
                {
                    AXE_CORE_INFO("ScriptWorld: SetVelocity entity {} → ({},{},{})",
                        (uint32_t)entity,
                        rb.PendingVelocity.x, rb.PendingVelocity.y, rb.PendingVelocity.z);
                    // TODO: PhysicsSystem::Get().SetLinearVelocity(entity, scene, rb.PendingVelocity);
                    rb.PendingVelocity = {};
                    rb.NeedsVelocitySet = false;
                }
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
            if (sc->Instance)
                sc->Instance->OnEvent(eventName, value);
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

    void ScriptWorld::DispatchTriggerEnter(Scene& scene,
        entt::entity trigger, entt::entity other)
    {
        auto& registry = scene.GetRegistry();

        // Notifica o objeto que possui o trigger
        if (auto* sc = registry.try_get<ScriptComponent>(trigger))
            if (sc->Instance) sc->Instance->OnTriggerEnter(other);

        // Notifica também o outro objeto (ele "entrou" no trigger)
        if (auto* sc = registry.try_get<ScriptComponent>(other))
            if (sc->Instance) sc->Instance->OnTriggerEnter(trigger);
    }

    void ScriptWorld::DispatchTriggerExit(Scene& scene,
        entt::entity trigger, entt::entity other)
    {
        auto& registry = scene.GetRegistry();

        if (auto* sc = registry.try_get<ScriptComponent>(trigger))
            if (sc->Instance) sc->Instance->OnTriggerExit(other);

        if (auto* sc = registry.try_get<ScriptComponent>(other))
            if (sc->Instance) sc->Instance->OnTriggerExit(trigger);
    }

} // namespace axe