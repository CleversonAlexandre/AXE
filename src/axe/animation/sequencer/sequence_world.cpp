// src/axe/animation/sequencer/sequence_world.cpp
//
// Ver a nota de topo do .hpp. Aqui esta a ORQUESTRACAO de runtime; a
// matematica de aplicar cada sample mora em sequence_apply.

#include "axe/animation/sequencer/sequence_world.hpp"

#include "axe/animation/animation_sampler.hpp"
#include "axe/animation/rig/control_rig_asset.hpp"
#include "axe/animation/rig/rig_nodes.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/log/log.hpp"
#include "axe/physics/physics_world.hpp"
#include "axe/scene/components.hpp"
#include "axe/scene/scene.hpp"
#include "axe/script/script_world.hpp"

#include <algorithm>

namespace axe
{
    // ═══════════════════════════════════════════════════════════════════════
    //  Ciclo de vida
    // ═══════════════════════════════════════════════════════════════════════

    void SequenceWorld::OnSceneStart(Scene& scene, PhysicsWorld* physics)
    {
        m_Instances.clear();
        m_EntityRestore.clear();
        m_CameraEntity = entt::null;
        m_Physics = physics;

        auto& reg = scene.GetRegistry();
        auto view = reg.view<SequencePlayerComponent>();

        for (auto e : view)
        {
            auto& spc = view.get<SequencePlayerComponent>(e);

            spc._Playing = false;
            spc._Started = false;

            if (spc.SequenceUUID.empty()) continue;

            Instance* inst = EnsureInstance(scene, e);
            if (!inst) continue;

            if (spc.PlayOnStart)
            {
                inst->Player.Scrub(
                    static_cast<float>(inst->Asset->GetFrameRange().Start));
                inst->Player.SetMode(SequencerPlaybackMode::Playing);
                spc._Playing = true;
                spc._Started = true;
            }
        }
    }

    void SequenceWorld::OnUpdate(Scene& scene, float deltaTime, ScriptWorld* scripts)
    {
        auto& reg = scene.GetRegistry();

        // Recalculado do zero todo frame. Ver GetActiveCameraEntity.
        m_CameraEntity = entt::null;

        auto view = reg.view<SequencePlayerComponent>();

        for (auto e : view)
        {
            auto& spc = view.get<SequencePlayerComponent>(e);
            if (!spc._Playing) continue;

            Instance* inst = EnsureInstance(scene, e);
            if (!inst) { spc._Playing = false; continue; }

            inst->Player.SetLoop(spc.Loop);
            inst->Player.OnUpdate(deltaTime);

            ApplyInstance(scene, e, *inst, deltaTime);

            // ── EVENTOS ──────────────────────────────────────────────────────
            //
            // Entregues a entidade do BINDING, e nao a que carrega o
            // componente. Um evento de cutscene quase sempre e sobre alguem —
            // "este personagem atirou" — e o script que responde e o dele.
            if (scripts)
            {
                for (const auto& ev : inst->Player.GetFiredEvents())
                {
                    const SequencerBinding* b =
                        inst->Asset ? inst->Asset->GetBinding(ev.BindingIndex) : nullptr;

                    entt::entity target = e;
                    if (b && !b->EntityName.empty())
                    {
                        const entt::entity be = scene.FindByName(b->EntityName);
                        if (be != entt::null && reg.valid(be)) target = be;
                    }

                    scripts->DispatchEvent(scene, target, ev.EventName, ev.Value);
                }
            }

            // ── A CUTSCENE ACABOU? ───────────────────────────────────────────
            //
            // O player passa sozinho para Paused ao cruzar o fim sem loop.
            // Devolver os transforms AQUI, e nao no proximo Play, e o que
            // impede a porta de ficar aberta para sempre.
            if (inst->Player.GetMode() != SequencerPlaybackMode::Playing)
            {
                spc._Playing = false;
                RestoreEntity(scene, e);
                continue;
            }

            // ── QUEM E A CAMERA DESTA CUTSCENE ───────────────────────────────
            //
            // A propria sequence responde. Em duas etapas, e a ordem entre elas
            // e a feature inteira:
            //
            //   1. A track de CORTE, se houver. E o plano em vigor neste frame.
            //   2. Sem track de corte: a primeira entidade animada que tenha
            //      CameraComponent. E o comportamento de quando so existia uma
            //      camera, e continua valendo para toda sequence ja gravada.
            if (spc.CameraCut && m_CameraEntity == entt::null && inst->Asset)
            {
                const std::string& cut = inst->Player.GetActiveCameraName();

                if (!cut.empty())
                {
                    const entt::entity be = scene.FindByName(cut);

                    if (be != entt::null && reg.valid(be) &&
                        reg.try_get<CameraComponent>(be))
                    {
                        m_CameraEntity = be;
                    }
                    else
                    {
                        // Corte apontando para uma camera que nao existe mais
                        // (renomeada, apagada). Cair na busca abaixo seria pior
                        // que avisar: o plano trocaria sozinho para outra camera
                        // e o animador procuraria o erro na timeline.
                        //
                        // Uma vez por nome, e nao por frame: um aviso que repete
                        // sessenta vezes por segundo empurra todo o resto do
                        // console para fora da tela, e ai ninguem le nenhum.
                        if (m_WarnedCut != cut)
                        {
                            m_WarnedCut = cut;
                            AXE_CORE_WARN("SequenceWorld: o corte aponta para '{}', "
                                "que nao e uma entidade de camera desta cena.", cut);
                        }
                    }
                }

                if (m_CameraEntity == entt::null && cut.empty())
                {
                    for (const auto& b : inst->Asset->GetBindings())
                    {
                        if (b.EntityName.empty()) continue;

                        const entt::entity be = scene.FindByName(b.EntityName);
                        if (be == entt::null || !reg.valid(be)) continue;
                        if (!reg.try_get<CameraComponent>(be)) continue;

                        m_CameraEntity = be;
                        break;
                    }
                }
            }
        }
    }

    void SequenceWorld::OnSceneStop(Scene& scene)
    {
        auto& reg = scene.GetRegistry();

        for (const auto& [e, saved] : m_EntityRestore)
        {
            if (!reg.valid(e)) continue;

            if (auto* tc = reg.try_get<TransformComponent>(e))
                tc->Data = saved;

            // Devolve o corpo fisico ANTES de sair. Um corpo esquecido em
            // Kinematic e um objeto que nunca mais cai, e nada no Inspector
            // explicaria por que — o Type autorado continuaria dizendo Dynamic.
            if (m_Physics)
                m_Physics->SetTransformDriven(scene, e, false);
        }
        m_EntityRestore.clear();

        // Solta a pose dos personagens que a cutscene dirigia: sem isto o
        // AnimGraph deles continua calado depois do Stop.
        for (auto& [owner, inst] : m_Instances)
        {
            if (!inst.Asset) continue;

            for (const auto& b : inst.Asset->GetBindings())
            {
                if (b.EntityName.empty()) continue;

                const entt::entity be = scene.FindByName(b.EntityName);
                if (be == entt::null || !reg.valid(be)) continue;

                if (auto* smc = reg.try_get<SkeletalMeshComponent>(be))
                    smc->PoseOverride = false;
            }
        }

        m_Instances.clear();
        m_CameraEntity = entt::null;

        auto view = reg.view<SequencePlayerComponent>();
        for (auto e : view)
        {
            auto& spc = view.get<SequencePlayerComponent>(e);
            spc._Playing = false;
            spc._Started = false;
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  Controle por codigo
    // ═══════════════════════════════════════════════════════════════════════

    void SequenceWorld::Play(Scene& scene, entt::entity e)
    {
        auto& reg = scene.GetRegistry();
        if (!reg.valid(e)) return;

        auto* spc = reg.try_get<SequencePlayerComponent>(e);
        if (!spc) return;

        Instance* inst = EnsureInstance(scene, e);
        if (!inst || !inst->Asset) return;

        inst->Player.Scrub(static_cast<float>(inst->Asset->GetFrameRange().Start));
        inst->Player.SetMode(SequencerPlaybackMode::Playing);

        spc->_Playing = true;
        spc->_Started = true;
    }

    void SequenceWorld::Stop(Scene& scene, entt::entity e)
    {
        auto& reg = scene.GetRegistry();
        if (!reg.valid(e)) return;

        if (auto* spc = reg.try_get<SequencePlayerComponent>(e))
            spc->_Playing = false;

        auto it = m_Instances.find(e);
        if (it != m_Instances.end())
            it->second.Player.SetMode(SequencerPlaybackMode::Paused);

        RestoreEntity(scene, e);
    }

    bool SequenceWorld::IsPlaying(Scene& scene, entt::entity e) const
    {
        const auto& reg = scene.GetRegistry();
        if (!reg.valid(e)) return false;

        const auto* spc = reg.try_get<SequencePlayerComponent>(e);
        return spc && spc->_Playing;
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  Carga
    // ═══════════════════════════════════════════════════════════════════════

    SequenceWorld::Instance* SequenceWorld::EnsureInstance(Scene& scene, entt::entity e)
    {
        auto& reg = scene.GetRegistry();
        auto* spc = reg.try_get<SequencePlayerComponent>(e);
        if (!spc || spc->SequenceUUID.empty()) return nullptr;

        Instance& inst = m_Instances[e];

        if (inst.LoadedUUID != spc->SequenceUUID || !inst.Asset)
        {
            inst = Instance{};
            inst.LoadedUUID = spc->SequenceUUID;

            const AssetRecord* rec = AssetDatabase::Get().GetByUUID(spc->SequenceUUID);
            if (!rec)
            {
                AXE_CORE_ERROR("SequenceWorld: o .axeseq desta entidade nao esta no "
                    "AssetDatabase. A cutscene nao toca.");
                return nullptr;
            }

            auto asset = std::make_shared<SequencerAsset>();
            if (!asset->LoadFromFile(rec->FilePath.string()))
            {
                AXE_CORE_ERROR("SequenceWorld: falha ao ler a sequence '{}'.", rec->Name);
                return nullptr;
            }

            inst.Asset = asset;

            // OnStart faz a copia profunda. Ver R1 no SEQUENCER_DESIGN §12: duas
            // entidades tocando a MESMA sequence nao podem compartilhar tempo
            // nem curvas.
            inst.Player.OnStart(*inst.Asset);
        }

        return inst.Asset ? &inst : nullptr;
    }

    SequenceWorld::RigInstance* SequenceWorld::EnsureRig(Instance& inst,
        const SequencerBinding& b, int bindingIndex)
    {
        if (b.RigAssetUUID.empty())
        {
            inst.Rigs.erase(bindingIndex);
            return nullptr;
        }

        RigInstance& rt = inst.Rigs[bindingIndex];

        if (rt.SourceUUID != b.RigAssetUUID)
        {
            rt = RigInstance{};
            rt.SourceUUID = b.RigAssetUUID;

            const AssetRecord* rec = AssetDatabase::Get().GetByUUID(b.RigAssetUUID);
            if (!rec) return nullptr;

            rt.Asset = ControlRigAsset::LoadFromFile(rec->FilePath);
            if (!rt.Asset) return nullptr;
        }

        if (!rt.Asset) return nullptr;

        if (!rt.Ready || rt.ClonedVersion != rt.Asset->GetVersion())
        {
            rt.Hierarchy = rt.Asset->GetHierarchy();
            rt.Graph = rt.Asset->GetGraph();

            rt.Functions.clear();
            for (const auto& f : rt.Asset->GetFunctions())
                rt.Functions.emplace_back(f.Name, f.Graph);

            rt.ClonedVersion = rt.Asset->GetVersion();
            rt.Ready = true;
        }

        return &rt;
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  Aplicacao
    // ═══════════════════════════════════════════════════════════════════════

    void SequenceWorld::ApplyInstance(Scene& scene, entt::entity owner,
        Instance& inst, float deltaTime)
    {
        if (!inst.Asset) return;

        auto& reg = scene.GetRegistry();

        const auto& samples = inst.Player.GetLastSamples();
        const auto& clipSamples = inst.Player.GetLastClipSamples();
        const auto& bindings = inst.Asset->GetBindings();

        inst.WorkPoses.resize(bindings.size());

        for (int bi = 0; bi < static_cast<int>(bindings.size()); ++bi)
        {
            const SequencerBinding& b = bindings[bi];
            if (b.EntityName.empty()) continue;

            const entt::entity target = scene.FindByName(b.EntityName);
            if (target == entt::null || !reg.valid(target)) continue;

            // ── 1. O TRANSFORM DA PROPRIA ENTIDADE ───────────────────────────
            //
            // Independe de esqueleto, entao vem antes e fora do caminho de
            // pose: uma camera nao tem osso nenhum, e desistir do binding por
            // falta de esqueleto e exatamente o bug que a versao do editor ja
            // teve.
            bool hasEntityTrack = false;
            for (const auto& tr : b.Tracks)
            {
                if (tr.TargetType != SequencerTargetType::Entity) continue;
                if (tr.Muted) continue;
                hasEntityTrack = true;
                break;
            }

            if (hasEntityTrack)
            {
                if (auto* tc = reg.try_get<TransformComponent>(target))
                {
                    // Primeira vez que dirigimos esta entidade: guarda o
                    // original. Ver m_EntityRestore.
                    if (m_EntityRestore.find(target) == m_EntityRestore.end())
                    {
                        m_EntityRestore[target] = tc->Data;

                        // E toma o corpo fisico, se houver. Sem isto os dois
                        // escrevem no mesmo transform todo frame e a fisica
                        // desfaz no Step seguinte — ver
                        // PhysicsWorld::SetTransformDriven.
                        if (m_Physics)
                            m_Physics->SetTransformDriven(scene, target, true);
                    }

                    tc->Data = sequence::ApplyEntitySamples(
                        m_EntityRestore[target], samples, bi);
                }
            }

            // ── 2. POSE ──────────────────────────────────────────────────────
            auto* smc = reg.try_get<SkeletalMeshComponent>(target);
            if (!smc || !smc->Data) continue;

            const Skeleton* skel = smc->GetSkeleton();
            if (!skel) continue;

            Pose& pose = inst.WorkPoses[bi];

            // 2a. CAMADA BASE: o clipe da section ativa, ou a pose de repouso.
            //
            //     Sem clipe, todo osso que a sequence NAO anima fica onde o
            //     esqueleto o coloca — e por isso animar so o Hips nao desmonta
            //     o resto do personagem.
            const AnimationClip* baseClip = nullptr;
            float baseTime = 0.0f;

            for (const auto& cs : clipSamples)
            {
                if (cs.BindingIndex != bi) continue;

                for (const auto& c : smc->Clips)
                {
                    if (c && c->GetName() == cs.ClipName)
                    {
                        baseClip = c.get();
                        baseTime = cs.TimeSeconds;
                        break;
                    }
                }
                if (baseClip) break;
            }

            if (baseClip)
                AnimationSampler::SamplePose(*skel, *baseClip, baseTime, pose);
            else
                Pose::FromBindPose(*skel, pose);

            // 2b. Os canais autorados, por cima, em espaco LOCAL.
            sequence::ApplyBoneSamples(pose, *skel, samples, bi, m_RotScratch);

            // 2c. CONTROL RIG, por ultimo entre as camadas de pose.
            //
            //     Mesma ordem do editor e do AnimNode_ControlRig: o rig corrige
            //     por cima de clipe + tracks de osso. Invertido, o FK bruto
            //     sobrescreveria o solve e o Foot IK nao teria efeito nenhum em
            //     osso que o animador tivesse tocado.
            if (!b.RigAssetUUID.empty())
            {
                if (RigInstance* rig = EnsureRig(inst, b, bi))
                {
                    if (rig->Ready)
                    {
                        SolveRig(*rig, b, bi, samples, *skel, pose,
                            scene.GetWorldTransform(target), deltaTime);
                    }
                }
            }

            // 2d. A cutscene e a dona da pose enquanto toca. Sem isto o
            //     AnimationWorld reescreve o BonePalette no mesmo frame e nada
            //     do que fizemos aparece — e o modo de falha e cruel, porque
            //     tudo aqui reporta sucesso.
            smc->PoseOverride = true;

            bool wantsGlobals = smc->ShowSkeleton || smc->_WantsBoneGlobals;
            if (!wantsGlobals)
            {
                for (const auto& tr : b.Tracks)
                {
                    if (tr.Type == SequencerTrackType::TransformSocket)
                    {
                        wantsGlobals = true;
                        break;
                    }
                }
            }

            AnimationSampler::BuildSkinningMatrices(
                *skel, pose, smc->BonePalette,
                wantsGlobals ? &smc->BoneGlobals : nullptr);
        }

        (void)owner;
    }

    void SequenceWorld::SolveRig(RigInstance& rig, const SequencerBinding& b,
        int bindingIndex, const std::vector<SequencerSample>& samples,
        const Skeleton& skel, Pose& pose,
        const glm::mat4& world, float deltaTime)
    {
        // 1. A pose do animador ANTES do ResetToInitial — e ele que resolve
        //    `Current = Initial * Value`. Ver sequence_apply.
        sequence::ApplyControlSamples(rig.Hierarchy, b, samples, bindingIndex);

        rig.Hierarchy.ResetToInitial();
        rig.Hierarchy.ApplyPose(skel, pose);

        // 2. Os controles vao para cima da animacao.
        //
        //    Este e o passo cujo sumico produziu o BP_Player torto ao mirar:
        //    sem ele o Two Bone IK mira em controles de pe parados na posicao
        //    de bind, enquanto a animacao ja saiu de la.
        if (b.RigControlsFollowAnimation)
            rig.Hierarchy.SnapControlsToCurrentBones();

        RigExecContext rc;
        rc.Hierarchy = &rig.Hierarchy;
        rc.Skel = &skel;
        rc.WorldTransform = world;

        // Em Play a fisica existe, ao contrario do preview do editor: um Ground
        // Trace aqui devolve o chao de verdade.
        rc.AllowWorldQueries = true;
        rc.UseEditorGround = false;

        rc.DeltaTime = deltaTime;
        rc.Graph = &rig.Graph;
        rc.Blackboard = nullptr;   // sem AnimGraph, sem parametros de gameplay

        rc.ResolveFunction = [&rig](const std::string& name) -> RigGraph* {
            for (auto& f : rig.Functions)
                if (f.first == name) return &f.second;
            return nullptr;
            };

        rig.Graph.Execute(rc, "ForwardsSolve");

        // 3. De volta para a pose. WritePose so escreve os ossos que existem na
        //    hierarquia do rig — dedos e twist bones continuam com o que o
        //    clipe e as tracks deram.
        rig.Hierarchy.WritePose(skel, pose);
    }

    void SequenceWorld::RestoreEntity(Scene& scene, entt::entity owner)
    {
        auto it = m_Instances.find(owner);
        if (it == m_Instances.end() || !it->second.Asset) return;

        auto& reg = scene.GetRegistry();

        for (const auto& b : it->second.Asset->GetBindings())
        {
            if (b.EntityName.empty()) continue;

            const entt::entity e = scene.FindByName(b.EntityName);
            if (e == entt::null || !reg.valid(e)) continue;

            auto rit = m_EntityRestore.find(e);
            if (rit != m_EntityRestore.end())
            {
                if (auto* tc = reg.try_get<TransformComponent>(e))
                    tc->Data = rit->second;

                m_EntityRestore.erase(rit);

                // O corpo volta a simular a partir de onde a cutscene o
                // deixou — e a posicao restaurada acima, nao a do ultimo
                // frame do plano. Com velocidade zerada, ver
                // SetTransformDriven.
                if (m_Physics)
                    m_Physics->SetTransformDriven(scene, e, false);
            }

            if (auto* smc = reg.try_get<SkeletalMeshComponent>(e))
                smc->PoseOverride = false;
        }
    }

} // namespace axe