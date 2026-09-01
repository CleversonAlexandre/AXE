#pragma once
#include "axe/utils/glm_config.hpp"
#include <cstdint>
#include <functional>
#include <unordered_set>

namespace axe
{
    // ==================== Rigidbody ====================

    enum class BodyType
    {
        Static,      // não se move, colide com tudo
        Dynamic,     // afetado pela física
        Kinematic    // movido por código, colide com dynamic
    };

    struct RigidbodyComponent
    {
        BodyType  Type = BodyType::Dynamic;
        float     Mass = 1.0f;
        float     Friction = 0.5f;
        float     Restitution = 0.0f;  // 0 = sem bounce, 1 = bounce total
        float     LinearDamping = 0.05f;
        float     AngularDamping = 0.05f;
        bool      UseGravity = true;
        bool      LockRotX = false;
        bool      LockRotY = false;
        bool      LockRotZ = false;

        // Runtime — ID do body no Jolt (cInvalidBodyID = não criado)
        uint32_t  BodyID = 0xffffffff; // JPH::BodyID::cInvalidBodyID
        bool      IsCreated = false;

        // ── Pending commands do ScriptRigidbodyProxy ──────────────────────────
        // Consumidos pelo PhysicsWorld::OnUpdate no mesmo frame que são escritos.
        glm::vec3 PendingForce = {};
        bool      NeedsForceApply = false;

        glm::vec3 PendingVelocity = {};
        bool      NeedsVelocitySet = false;

        // Velocidade atual, sincronizada do Jolt todo frame por PhysicsWorld::
        // OnUpdate — mesmo padrão de cache usado em CharacterControllerComponent
        // ::Velocity. Existe pra ScriptRigidbodyProxy::GetVelocity() poder ler
        // sem precisar de acesso direto ao Jolt (que o proxy não tem).
        glm::vec3 CurrentVelocity = {};

        // ── QUEM MANDA NO TRANSFORM: O CORPO OU QUEM ESCREVE NELE ────────────
        //
        // Falso (o normal): o Jolt simula e o resultado E o TransformComponent.
        //
        // Verdadeiro: inverte a seta. O corpo vira Kinematic e SEGUE o
        // transform que alguem de fora escreveu — hoje, uma cutscene.
        //
        // ── POR QUE ISTO PRECISA EXISTIR ─────────────────────────────────────
        //
        // Sem a inversao, os dois escrevem no mesmo transform todo frame: a
        // sequence escreve depois e vence NAQUELE frame, e o Step seguinte
        // desfaz — a porta da cutscene treme no lugar e nunca chega onde a
        // timeline diz. E o pior tipo de bug de ordem, porque cada metade,
        // olhada sozinha, esta certa.
        //
        // Kinematic e nao "desligado" de proposito: uma porta de cutscene
        // ainda tem de empurrar quem estiver na frente dela.
        //
        // NAO serializa: e estado de uma cutscene em curso, e uma cena salva
        // com isto ligado carregaria com o corpo inerte sem nada explicando.
        bool      _TransformDriven = false;
    };

    // ==================== Collider ====================

    enum class ColliderShape
    {
        Box,
        Sphere,
        Capsule,
        Mesh,       // Mesh exato — Static only
        ConvexHull, // Convex hull — Dynamic/Kinematic
    };

    struct ColliderComponent
    {
        ColliderShape Shape = ColliderShape::Box;
        glm::vec3     Offset = { 0, 0, 0 };

        // Box
        glm::vec3     HalfExtent = { 0.5f, 0.5f, 0.5f };

        // Sphere
        float         Radius = 1.0f;

        // Capsule
        float         Height = 1.8f;   // altura total
        float         CapsuleRadius = 0.3f;

        bool          IsTrigger = false;       // trigger não colide, só detecta
        bool          ShowDebug = false;        // mostra wireframe no viewport

        // Runtime — body estático implícito (criado pelo PhysicsWorld para colliders sem Rigidbody)
        uint32_t      StaticBodyID = 0xffffffff;
        bool          IsStaticCreated = false;
    };

    // ==================== Character Controller ====================

    struct CharacterControllerComponent
    {
        float Height = 1.8f;
        float Radius = 0.3f;
        float MaxSlopeAngle = 45.0f;
        float StepHeight = 0.3f;
        float MaxSpeed = 5.0f;
        float JumpForce = 5.0f;

        // ── Orientar pela direcao do movimento ────────────────────────────
        //
        // O "Orient Rotation to Movement" da Unreal: o personagem GIRA para
        // onde anda, e a animacao pode ser sempre a de caminhar PRA FRENTE.
        // Sem isto, andar pra esquerda toca a animacao de frente enquanto o
        // corpo desliza de lado.
        //
        // Mora no controller (e nao no grafo de script) porque e regra de
        // locomocao: todo personagem e NPC quer a mesma coisa, e ninguem
        // deveria reescrever atan2 em cada Blueprint.
        bool  OrientRotationToMovement = false;
        float RotationRate = 720.0f;   // graus por segundo (0 = instantaneo)

        // Wireframe da capsula na viewport — sem isto, ajustar Height/Radius
        // e adivinhacao: nao da pra ver onde a capsula esta em relacao ao
        // personagem.
        bool ShowDebug = true;

        // Deslocamento da CAPSULA em relacao a origem do personagem (pes).
        // Y ja recebe meia altura automaticamente; isto e o ajuste FINO por
        // cima disso — util quando o pivo do modelo nao esta exatamente
        // entre os pes, ou pra encaixar a capsula num personagem agachado.
        // Em METROS, como Height/Radius (a escala do transform nao entra).
        glm::vec3 CapsuleOffset{ 0.0f, 0.0f, 0.0f };

        // Runtime
        glm::vec3 Velocity = {};

        // Yaw alvo em RADIANOS, escrito pelo Move() quando ha direcao.
        // Valido so quando HasDesiredYaw — parado, o personagem mantem a
        // ultima orientacao em vez de voltar pra frente.
        float DesiredYaw = 0.0f;
        bool  HasDesiredYaw = false;
        bool      IsGrounded = false;
        bool      WantsJump = false;
        uint32_t  CharacterID = 0;
        bool      IsCreated = false;

        // Rastreamento de contatos — evita disparar Enter/Exit todo frame
        std::unordered_set<uint32_t> ActiveTriggers;
        std::unordered_set<uint32_t> ActiveCollisions;

        // Mesmo significado do campo homonimo em RigidbodyComponent: enquanto
        // ligado, o personagem NAO anda por conta propria — ele e teleportado
        // para onde o transform diz.
        //
        // Este e o caso mais comum dos dois, e o menos obvio: uma cutscene que
        // move o JOGADOR. Sem isto, o CharacterVirtual aplica gravidade e o
        // ExtendedUpdate sobrescreve a posicao no frame seguinte — o
        // personagem "escorrega" de volta durante o plano inteiro.
        bool      _TransformDriven = false;
    };

    // ==================== Trigger Volume ====================

    struct TriggerComponent
    {
        ColliderShape Shape = ColliderShape::Box;
        glm::vec3     HalfExtent = { 1, 1, 1 };
        float         Radius = 1.0f;

        // Callbacks — conectar via script ou editor
        std::function<void(uint32_t otherBodyID)> OnEnter;
        std::function<void(uint32_t otherBodyID)> OnExit;

        // Runtime
        uint32_t BodyID = 0;
        bool     IsCreated = false;
    };

} // namespace axe