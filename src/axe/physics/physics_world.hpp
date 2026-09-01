#pragma once
#include "axe/core/types.hpp"
#include "axe/physics/physics_system.hpp"
#include "axe/physics/physics_components.hpp"
#include "axe/scene/scene.hpp"
#include "axe/utils/glm_config.hpp"
#include <entt/entt.hpp>

namespace axe
{
    // Sincroniza a cena entt com o mundo Jolt
    class AXE_API PhysicsWorld
    {
    public:
        void OnSceneStart(Scene& scene);
        void OnSceneStop(Scene& scene);
        void OnUpdate(Scene& scene, float deltaTime);

        void CreateBody(entt::entity entity, Scene& scene);
        void DestroyBody(entt::entity entity, Scene& scene);

        void AddForce(entt::entity entity, Scene& scene, const glm::vec3& force);
        void AddImpulse(entt::entity entity, Scene& scene, const glm::vec3& impulse);

        // ── QUEM MANDA NO TRANSFORM DESTA ENTIDADE ───────────────────────────
        //
        // `driven = true`  — o corpo para de simular e passa a SEGUIR o
        //                    TransformComponent. Rigidbody vira Kinematic (nao
        //                    desligado: uma porta de cutscene ainda empurra
        //                    quem esta na frente); CharacterController deixa de
        //                    andar e passa a ser teleportado.
        // `driven = false` — devolve o corpo ao tipo autorado, com velocidade
        //                    zerada.
        //
        // Existe porque animar por fora um objeto que tem corpo fisico e uma
        // disputa que a fisica sempre ganha no frame seguinte. Ver a nota em
        // RigidbodyComponent::_TransformDriven.
        //
        // Idempotente: chamar duas vezes com o mesmo valor nao faz nada. Quem
        // dirige nao precisa lembrar do que ja pediu.
        //
        // NAO e exclusivo do Sequencer de proposito. Qualquer sistema que
        // autore transform de fora — um elevador scriptado, uma ferramenta de
        // editor — tem o mesmo problema e esta e a mesma resposta.
        void SetTransformDriven(Scene& scene, entt::entity entity, bool driven);

        // CharacterController
        void CreateCharacter(entt::entity entity, Scene& scene);
        void DestroyCharacter(entt::entity entity, Scene& scene);

        RaycastHit Raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist = 1000.0f);
    };

} // namespace axe