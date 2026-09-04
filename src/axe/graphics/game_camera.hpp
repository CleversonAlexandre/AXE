#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/axe_window/window.hpp"
#include <entt/entt.hpp>

namespace axe
{
    class Scene;

    // Forward-declare em vez de incluir components.hpp: aquele header e o hub
    // que ja obrigou o projeto a ligar /bigobj, e a GameCamera so precisa do
    // NOME do tipo para receber uma referencia (ver SolveSpringArm).
    struct SpringArmComponent;

    class AXE_API GameCamera
    {
    public:
        GameCamera() = default;

        // ── Modo de câmera ────────────────────────────────────────────────────
        enum class Mode { FreeFly, ThirdPerson };

        void OnUpdate(float deltaTime, Window* window);

        // ── A CONVENCAO DE "FRENTE", NUM LUGAR SO ────────────────────────────
        //
        // Era um `static CalcForward` local do .cpp. Virou publica porque tres
        // lugares precisam da MESMA resposta e nenhum deles e a GameCamera:
        //
        //   - o SceneRuntime, que posiciona a camera do Play a partir do
        //     transform de uma entidade-camera;
        //   - o desenho do frustum no viewport, que tem de apontar para onde a
        //     camera vai apontar de verdade;
        //   - o "ver pela camera", que pilota a EditorCamera.
        //
        // Copiada nos tres, ela divergiria no primeiro dia em que alguem
        // decidisse que camera zerada olha para -Z em vez de +X. Aqui, mudar a
        // convencao e mudar uma funcao.
        //
        // Angulos em GRAUS (yaw em torno de Y, pitch em torno do eixo lateral).
        // Com os dois em zero, a frente e +X.
        static glm::vec3 ForwardFromYawPitch(float yawDegrees, float pitchDegrees);

        // ── ONDE O BRACO POE A CAMERA, NUM LUGAR SO (ARM_SOLVER_V1) ──────────
        //
        // Pela mesma razao que o ForwardFromYawPitch acima ficou publico: havia
        // TRES contas diferentes para "onde fica a camera deste Spring Arm", e
        // elas discordavam.
        //
        //   1. o Play  — esta classe, a unica correta;
        //   2. o preview do Script Editor — punha a camera em
        //      `pawn + (socket.x, height + socket.y, length + socket.z)`,
        //      um eixo Z cru que ignora o pitch e ignora o modo travado, e
        //      apontava sempre PARA o personagem;
        //   3. o frustum do viewport — desenhava no TRANSFORM DA ENTIDADE,
        //      ignorando o braco inteiro. Como o CameraComponent de um pawn
        //      mora na mesma entidade do personagem, o frustum nascia nos PES
        //      dele, apontando para onde o personagem olha.
        //
        // O sintoma era o autor posicionar a camera numa tela e ela aparecer em
        // outro lugar nas outras duas — sem nada indicando qual estava certa.
        //
        // Agora as tres chamam ISTO. Angulos em GRAUS; distancia, altura e
        // offset em METROS.
        struct ArmPose
        {
            glm::vec3 Position;   // onde a camera fica
            glm::vec3 LookAt;     // para onde ela olha
        };

        static ArmPose SolveSpringArm(const glm::vec3& targetPosition,
            float yawDegrees, float pitchDegrees,
            float distance, float heightOffset,
            const glm::vec3& socketOffset);

        // A mesma coisa, a partir do componente — inclusive a regra de QUAL
        // angulo usar: travado usa FixedYaw/FixedPitch; em orbita usa os
        // angulos com que o Play COMECA (-90 / -10), que e o unico palpite
        // honesto para uma camera que o jogador vai girar com o mouse.
        //
        // Essa regra tambem precisa existir num lugar so: repetida no preview e
        // no viewport, ela divergiria no dia em que o default do Play mudasse.
        static ArmPose SolveSpringArm(const SpringArmComponent& arm,
            const glm::vec3& targetPosition);

        // O socket offset convertido para o mundo a partir de uma direcao de
        // vista. Exposto junto do solver porque quem desenha o braco (a linha
        // roxa do preview) precisa da mesma conta sem querer a pose inteira.
        static glm::vec3 ArmOffsetFor(const glm::vec3& viewDir,
            const glm::vec3& socketOffset);

        glm::mat4 GetViewMatrix() const;
        glm::mat4 GetProjectionMatrix(float aspectRatio) const;
        glm::mat4 GetViewProjectionMatrix(float aspectRatio) const;

        const glm::vec3& GetPosition() const { return m_Position; }

        // Angulo da orbita third-person, em graus. Publico porque o script
        // precisa dele para mover o personagem em relacao a CAMERA (mouse
        // look): sem isto, "para frente" e sempre o mesmo eixo do mundo e
        // girar a camera nao corrige a direcao do movimento.
        float GetYaw()   const { return m_Yaw; }
        float GetPitch() const { return m_Pitch; }

        void Reset(const glm::vec3& position, float yaw, float pitch);

        // ── Camera Shake ──────────────────────────────────────────────────────
        void StartShake(float intensity, float duration);
        bool IsShaking() const { return m_ShakeTime > 0.f; }

        // ── Third Person ──────────────────────────────────────────────────────
        void SetTarget(const glm::vec3* targetPosition) { m_TargetPosition = targetPosition; }
        void ClearTarget() { m_TargetPosition = nullptr; }
        bool HasTarget() const { return m_TargetPosition != nullptr; }

        // Follow por entity (usado por script — atualizado em OnUpdate)
        void SetFollowEntity(entt::entity e, Scene* s) { m_FollowEntity = e; m_FollowScene = s; }
        void ClearFollowEntity() { m_FollowEntity = entt::null; m_FollowScene = nullptr; m_TargetPosition = nullptr; }

        // Configuraçõess
        float MoveSpeed = 5.0f;
        float Sensitivity = 0.1f;
        float Fov = 60.0f;
        float NearClip = 0.1f;
        float FarClip = 1000.0f;

        float TPDistance = 5.0f;   // distância atrás do player
        float TPHeight = 2.0f;   // altura acima do player
        float TPLagSpeed = 8.0f;   // suavização do follow (lerp)
        bool  TPMouseRotates = true;   // mouse rotaciona a câmera

        // ── BP_CAMERA_V1 — O SOCKET OFFSET DO BRACO ──────────────────────────
        //
        // O `SpringArmComponent::SocketOffset` existia, era serializado, era
        // editavel no Inspector E no Script Editor, e o preview do Script
        // Editor ate desenhava a camera nele. So que NADA aqui dentro o lia:
        // o `UpdateThirdPerson` calculava a posicao a partir de TPDistance e
        // TPHeight e mais nada. Ajustar o campo mexia no desenho do preview e
        // nao mexia no jogo — que e o pior tipo de defeito, porque o valor
        // "responde" na tela de autoria.
        //
        // Convencao (a MESMA que o preview do Script Editor ja desenhava, para
        // que os dois concordem):
        //   X = lateral       (direita do braco)
        //   Y = vertical      (cima do mundo)
        //   Z = profundidade  (para longe do alvo, no sentido do braco)
        //
        // Em METROS, como TPDistance e TPHeight.
        glm::vec3 TPSocketOffset{ 0.0f };

        // ── BRACO TRAVADO ────────────────────────────────────────────────────
        //
        // Com isto ligado o braco para de orbitar: ele fica em TPLockYaw /
        // TPLockPitch e so acompanha o alvo transladando. O mouse deixa de
        // girar a camera mesmo com TPMouseRotates ligado — travado e travado.
        //
        // E o que um jogo de plataforma precisa, e nao havia como consegui-lo:
        // TPMouseRotates=false apenas parava de ATUALIZAR o angulo, deixando o
        // braco parado em onde quer que o ultimo movimento de mouse o tivesse
        // deixado.
        bool  TPLockRotation = false;
        float TPLockYaw = -90.0f;
        float TPLockPitch = -10.0f;

        // `SpringArmComponent::EnableCameraLag` tambem nunca chegava aqui:
        // desmarcar a caixa no Inspector nao tirava a suavizacao do jogo.
        bool  TPEnableLag = true;

        Mode  CameraMode = Mode::FreeFly;
        bool  MouseCaptured = false;
        bool  m_FirstMouse = true;

    private:
        // BP_CAMERA_V1 — o TPSocketOffset convertido para o mundo, a partir do
        // yaw/pitch ATUAIS do braco. Usado em dois lugares que precisam
        // concordar: a posicao desejada (UpdateThirdPerson) e o ponto para
        // onde a camera olha (GetViewMatrix). Se divergirem, mover o offset
        // desenquadra em vez de deslocar o enquadramento.
        glm::vec3 ArmOffsetWorld() const;

        void UpdateFreeFly(float deltaTime, Window* window);
        void UpdateThirdPerson(float deltaTime, Window* window);

        glm::vec3 m_Position{ 0.0f, 1.0f, 5.0f };
        glm::vec3 m_DesiredPosition{ 0.0f, 1.0f, 5.0f };
        float     m_Yaw = -90.0f;
        float     m_Pitch = -10.0f;

        const glm::vec3* m_TargetPosition = nullptr;

        // Shake
        float     m_ShakeIntensity = 0.f;
        float     m_ShakeTime = 0.f;
        float     m_ShakeDuration = 0.f;

        // Follow entity via script
        entt::entity m_FollowEntity = entt::null;
        Scene* m_FollowScene = nullptr;

        struct { float x = 0.0f; float y = 0.0f; } m_LastMousePos;
    };

} // namespace axe