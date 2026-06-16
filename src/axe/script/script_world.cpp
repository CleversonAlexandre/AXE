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

                if (sc.Instance) { sc.Instance->OnEnd(); sc.Instance.reset(); }
                if (sc.DllHandle) { DllLoader::Unload(sc.DllHandle); sc.DllHandle = nullptr; }
                sc.IsLoaded = false;

                if (!sc.IsLoaded)
                {
                    if (sc.DllPath.empty())
                    {
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

        int updated = 0;
        registry.view<ScriptComponent>().each([&](entt::entity entity, ScriptComponent& sc)
            {
                static int s_Frame = 0;
                s_Frame++;
                if (s_Frame <= 3)
                    AXE_CORE_INFO("ScriptWorld::OnSceneUpdate frame={} entity={} Instance={}",
                        s_Frame, (uint32_t)entity, (void*)sc.Instance.get());

                if (!sc.Instance)
                {
                    AXE_CORE_WARN("ScriptWorld::OnSceneUpdate: entity {} sem Instance!", (uint32_t)entity);
                    return;
                }
                updated++;

                const bool* cur = axe::Input::GetCurrentKeys();
                const bool* prev = axe::Input::GetPreviousKeys();

                static int s_pre = 0;
                if (s_pre++ < 3)
                    AXE_CORE_INFO("PreUpdate cur={} W={}", (void*)cur, cur ? (bool)cur[87] : false);

                sc.Instance->PreUpdate(cur, prev);

                // Log quando WASD pressionado — confirma que input chegou ao PreUpdate
                if (cur && (cur[87] || cur[65] || cur[83] || cur[68]))
                    AXE_CORE_INFO("ScriptWorld: WASD antes OnUpdate — W={} A={} S={} D={}",
                        (bool)cur[87], (bool)cur[65], (bool)cur[83], (bool)cur[68]);

                sc.Instance->OnUpdate(deltaTime);

                // Log Velocity do CC após OnUpdate — confirma se Move() foi chamado
                auto* cc = scene.GetRegistry().try_get<CharacterControllerComponent>(entity);
                if (cc && (std::abs(cc->Velocity.x) > 0.001f || std::abs(cc->Velocity.z) > 0.001f))
                    AXE_CORE_INFO("ScriptWorld: CC Velocity após OnUpdate = ({:.2f},{:.2f})",
                        cc->Velocity.x, cc->Velocity.z);
            });

        if (updated == 0)
            AXE_CORE_WARN("ScriptWorld::OnSceneUpdate — 0 scripts rodando!");

        registry.view<RigidbodyComponent>().each([&](entt::entity entity, RigidbodyComponent& rb)
            {
                if (!rb.IsCreated) return;

                if (rb.NeedsForceApply)
                {
                    AXE_CORE_INFO("ScriptWorld: AddForce entity {} → ({},{},{})",
                        (uint32_t)entity,
                        rb.PendingForce.x, rb.PendingForce.y, rb.PendingForce.z);
                    rb.PendingForce = {};
                    rb.NeedsForceApply = false;
                }

                if (rb.NeedsVelocitySet)
                {
                    AXE_CORE_INFO("ScriptWorld: SetVelocity entity {} → ({},{},{})",
                        (uint32_t)entity,
                        rb.PendingVelocity.x, rb.PendingVelocity.y, rb.PendingVelocity.z);
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

    void ScriptWorld::DispatchCollision(Scene& scene, entt::entity a, entt::entity b)
    {
        auto& registry = scene.GetRegistry();
        if (auto* sc = registry.try_get<ScriptComponent>(a))
            if (sc->Instance) sc->Instance->OnCollision(b);
        if (auto* sc = registry.try_get<ScriptComponent>(b))
            if (sc->Instance) sc->Instance->OnCollision(a);
    }

    void ScriptWorld::DispatchTriggerEnter(Scene& scene, entt::entity trigger, entt::entity other)
    {
        auto& registry = scene.GetRegistry();
        if (auto* sc = registry.try_get<ScriptComponent>(trigger))
            if (sc->Instance) sc->Instance->OnTriggerEnter(other);
        if (auto* sc = registry.try_get<ScriptComponent>(other))
            if (sc->Instance) sc->Instance->OnTriggerEnter(trigger);
    }

    void ScriptWorld::DispatchTriggerExit(Scene& scene, entt::entity trigger, entt::entity other)
    {
        auto& registry = scene.GetRegistry();
        if (auto* sc = registry.try_get<ScriptComponent>(trigger))
            if (sc->Instance) sc->Instance->OnTriggerExit(other);
        if (auto* sc = registry.try_get<ScriptComponent>(other))
            if (sc->Instance) sc->Instance->OnTriggerExit(trigger);
    }

} // namespace axe