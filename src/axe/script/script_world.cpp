#include "script_world.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include "script_component.hpp"
#include "dll_loader.hpp"
#include "script_paths.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/scene/scene.hpp"
#include "axe/physics/physics_components.hpp"
#include "axe/log/log.hpp"
#include "axe/input/input.hpp"

namespace axe
{
    ScriptWorld::ScriptWorld() = default;
    ScriptWorld::~ScriptWorld() = default;

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
                    // SC4 — o caminho da DLL passa a ser SEMPRE resolvido aqui,
                    // e nunca lido da cena. Ele é dado derivado: depende de onde
                    // o projeto está no disco desta máquina. Guardar o valor
                    // absoluto no .axescene é a mesma classe de erro que a regra
                    // "nunca serialize caminho absoluto" já proíbe para asset —
                    // funcionava só até alguém mover a pasta ou abrir o projeto
                    // em outro computador. Um sc.DllPath que veio de cena antiga
                    // é ignorado de propósito.
                    // SC29 — o UUID e uma AJUDA, nao um requisito.
                    //
                    // Num jogo empacotado o AssetDatabase pode nem estar
                    // carregado, e GetByPath devolve nada. Antes isso bastava
                    // para o script nao carregar; agora o ResolveDll tem um
                    // passo que acha a DLL pelo padrao do nome quando o UUID
                    // falta (ver ScriptPaths::ResolveDll, passo 3).
                    std::string uuid;
                    if (!sc.ScriptAssetPath.empty())
                        if (auto* rec = AssetDatabase::Get().GetByPath(sc.ScriptAssetPath))
                            uuid = rec->UUID;

                    auto resolved = ScriptPaths::ResolveDll(sc.ScriptName, uuid);
                    if (!resolved.empty())
                    {
                        sc.DllPath = resolved.string();
                    }
                    else
                    {
                        sc.DllPath.clear();
                        AXE_CORE_WARN("ScriptWorld: DLL não encontrada para '{}'. Compile o script antes do Play.", sc.ScriptName);
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
                                //AXE_CORE_INFO("ScriptWorld: DLL '{}' carregada.", sc.ScriptName);
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
                ctx.SequencePtr = m_SequenceWorld;
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
                //if (s_Frame <= 3)
                //    AXE_CORE_INFO("ScriptWorld::OnSceneUpdate frame={} entity={} Instance={}",
                //        s_Frame, (uint32_t)entity, (void*)sc.Instance.get());

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

                // Injeta câmera no contexto antes do OnUpdate
                if (m_ActiveCamera)
                    sc.Instance->UpdateCameraInContext(m_ActiveCamera);

                sc.Instance->OnUpdate(deltaTime);

                // Log Velocity do CC após OnUpdate — confirma se Move() foi chamado
                auto* cc = scene.GetRegistry().try_get<CharacterControllerComponent>(entity);
                //if (cc && (std::abs(cc->Velocity.x) > 0.001f || std::abs(cc->Velocity.z) > 0.001f))
                //    AXE_CORE_INFO("ScriptWorld: CC Velocity após OnUpdate = ({:.2f},{:.2f})",
                //        cc->Velocity.x, cc->Velocity.z);
            });

        //if (updated == 0)
        //    AXE_CORE_WARN("ScriptWorld::OnSceneUpdate — 0 scripts rodando!");

        registry.view<RigidbodyComponent>().each([&](entt::entity entity, RigidbodyComponent& rb)
            {
                if (!rb.IsCreated) return;

                if (rb.NeedsForceApply)
                {
                    //AXE_CORE_INFO("ScriptWorld: AddForce entity {} → ({},{},{})",
                    //    (uint32_t)entity,
                    //    rb.PendingForce.x, rb.PendingForce.y, rb.PendingForce.z);
                    rb.PendingForce = {};
                    rb.NeedsForceApply = false;
                }

                if (rb.NeedsVelocitySet)
                {
                    //AXE_CORE_INFO("ScriptWorld: SetVelocity entity {} → ({},{},{})",
                    //    (uint32_t)entity,
                    //    rb.PendingVelocity.x, rb.PendingVelocity.y, rb.PendingVelocity.z);
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