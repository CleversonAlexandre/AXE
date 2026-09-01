#pragma once

// src/axe/animation/sequencer/sequence_world.hpp
//
// ═══════════════════════════════════════════════════════════════════════════
//  O SEXTO MUNDO: A CUTSCENE TOCANDO NO JOGO
// ═══════════════════════════════════════════════════════════════════════════
//
// Mesmo contrato do PhysicsWorld, do ScriptWorld, do ParticleWorld, do
// AudioWorld e do AnimationWorld: o SceneRuntime e o dono, chama OnSceneStart,
// OnUpdate e OnSceneStop na ordem certa, e o `game.exe` roda exatamente o
// mesmo objeto que o Play do editor roda.
//
// ── POR QUE UM MUNDO PROPRIO, E NAO UM PEDACO DO AnimationWorld ─────────────
//
// Os dois mexem em pose, entao a tentacao e obvia. Mas eles respondem a
// perguntas diferentes:
//
//   AnimationWorld — "que pose este personagem tem AGORA?", a partir do
//                    AnimGraph dele. Uma pergunta por personagem.
//
//   SequenceWorld  — "que estado o MUNDO tem no frame 47 desta cutscene?".
//                    Uma sequence dirige varios personagens, mais a camera,
//                    mais a porta, mais a luz — e dirige o TRANSFORM deles,
//                    nao so a pose.
//
// Enfiar o segundo dentro do primeiro faria o AnimationWorld escrever em
// TransformComponent de entidades sem esqueleto nenhum, e a ordem entre "a
// sequence move a camera" e "o AnimGraph anima o personagem" ficaria implicita
// num laco que nao foi feito para decidir isso.
//
// ── A ORDEM QUE IMPORTA ─────────────────────────────────────────────────────
//
// Este mundo roda DEPOIS do AnimationWorld, e o motivo e o mesmo `PoseOverride`
// que o Sequencer ja usava no editor: quem escreve por ultimo vence. Uma
// cutscene existe justamente para sobrepor o comportamento normal — se o
// AnimGraph rodasse depois, o personagem voltaria a andar no meio da cena.

#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

#include "axe/animation/pose.hpp"
#include "axe/animation/rig/rig_graph.hpp"
#include "axe/animation/rig/rig_hierarchy.hpp"
#include "axe/animation/sequencer/sequence_apply.hpp"
#include "axe/animation/sequencer/sequencer_asset.hpp"
#include "axe/animation/sequencer/sequencer_player.hpp"
#include "axe/scene/transform.hpp"

#include <entt/entt.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace axe
{
    class Scene;
    class ControlRigAsset;
    class ScriptWorld;
    class PhysicsWorld;

    class AXE_API SequenceWorld
    {
    public:
        // Prepara um player para cada SequencePlayerComponent e dispara os que
        // tem PlayOnStart.
        //
        // `physics` pode ser nulo (preview sem mundo fisico). Guardado ate o
        // OnSceneStop: os dois pertencem ao mesmo SceneRuntime e tem a mesma
        // vida, e a alternativa — passa-lo em cada uma das quatro entradas —
        // deixaria "soltar o corpo no fim" a cargo de quem chama, que e onde
        // esse tipo de coisa e esquecida.
        void OnSceneStart(Scene& scene, PhysicsWorld* physics);

        // `scripts` pode ser nulo: sem ScriptWorld, as tracks de Event
        // simplesmente nao entregam nada. Nao e erro — e o preview do editor.
        void OnUpdate(Scene& scene, float deltaTime, ScriptWorld* scripts);

        // Devolve toda entidade dirigida ao transform que ela tinha antes, e
        // solta o PoseOverride dos personagens. Ver a nota em m_EntityRestore.
        void OnSceneStop(Scene& scene);

        // ── CONTROLE POR CODIGO ──────────────────────────────────────────────
        //
        // A entidade e a que carrega o SequencePlayerComponent.
        void Play(Scene& scene, entt::entity e);
        void Stop(Scene& scene, entt::entity e);
        bool IsPlaying(Scene& scene, entt::entity e) const;

        // ── A CAMERA DA CUTSCENE ─────────────────────────────────────────────
        //
        // Entidade com CameraComponent que uma sequence TOCANDO esta animando,
        // ou null. O SceneRuntime le isto todo frame para decidir de onde a
        // GameCamera enxerga.
        //
        // Recalculado por frame, e nao guardado num "modo cutscene": um estado
        // que so se desliga por um caminho e um estado que um dia fica ligado
        // para sempre — e o sintoma seria o jogador preso olhando para uma
        // camera parada, sem nenhum jeito de sair.
        entt::entity GetActiveCameraEntity() const { return m_CameraEntity; }

    private:
        // Copia viva de um `.axerig`, por entidade dirigida.
        //
        // O molde e compartilhado com o resto do jogo (o mesmo arquivo devolve
        // o mesmo objeto), entao escrever `Value` no asset faria a pose da
        // cutscene virar a pose padrao de TODO personagem que use aquele rig.
        // E o mesmo cuidado do AnimNode_ControlRig::EnsureWorkingCopy.
        struct RigInstance
        {
            std::shared_ptr<ControlRigAsset> Asset;
            RigHierarchy Hierarchy;
            RigGraph     Graph;
            std::vector<std::pair<std::string, RigGraph>> Functions;

            std::string  SourceUUID;
            uint32_t     ClonedVersion = 0;
            bool         Ready = false;
        };

        struct Instance
        {
            SequencerPlayer            Player;
            std::shared_ptr<SequencerAsset> Asset;
            std::string                LoadedUUID;

            // Chaveado pelo indice do binding, revalidado contra o
            // RigAssetUUID a cada uso.
            std::unordered_map<int, RigInstance> Rigs;

            std::vector<Pose> WorkPoses;
        };

        std::unordered_map<entt::entity, Instance> m_Instances;

        // ── O TRANSFORM ORIGINAL DE TODA ENTIDADE QUE A CUTSCENE MOVEU ───────
        //
        // Osso volta sozinho: a pose e recomposta da bind pose todo frame. O
        // TransformComponent nao — o que a sequence escreve nele FICA.
        //
        // Sem guardar, uma cutscene que termina deixa a porta (ou o elevador,
        // ou o proprio jogador) parada no ultimo frame dela, para sempre. No
        // editor isso ainda seria pior: um Ctrl+S depois do Play gravaria essa
        // pose como se fosse a posicao autorada da cena.
        std::unordered_map<entt::entity, Transform> m_EntityRestore;

        entt::entity m_CameraEntity{ entt::null };

        // Ultimo corte quebrado ja reclamado. Ver a nota no .cpp: o aviso e por
        // NOME, nao por frame.
        std::string  m_WarnedCut;

        // Nao-dono. Ver OnSceneStart.
        PhysicsWorld* m_Physics = nullptr;

        // Reaproveitados entre entidades e entre frames.
        std::vector<sequence::BoneEulerEdit> m_RotScratch;

        Instance* EnsureInstance(Scene& scene, entt::entity e);
        RigInstance* EnsureRig(Instance& inst, const SequencerBinding& b, int bindingIndex);

        void ApplyInstance(Scene& scene, entt::entity owner, Instance& inst,
            float deltaTime);
        void SolveRig(RigInstance& rig, const SequencerBinding& b, int bindingIndex,
            const std::vector<SequencerSample>& samples,
            const Skeleton& skel, Pose& pose,
            const glm::mat4& world, float deltaTime);

        void RestoreEntity(Scene& scene, entt::entity e);
    };

} // namespace axe