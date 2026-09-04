#include "game_camera.hpp"
#include "axe/log/log.hpp"
#include "axe/axe_window/window.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/components.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include "axe/input/key_codes.hpp"

namespace axe
{
    glm::vec3 GameCamera::ForwardFromYawPitch(float yaw, float pitch)
    {
        glm::vec3 forward;
        forward.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        forward.y = sin(glm::radians(pitch));
        forward.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        return glm::normalize(forward);
    }

    // Nome curto para o uso interno, que e o mais frequente neste arquivo.
    static glm::vec3 CalcForward(float yaw, float pitch)
    {
        return GameCamera::ForwardFromYawPitch(yaw, pitch);
    }

    void GameCamera::StartShake(float intensity, float duration)
    {
        m_ShakeIntensity = intensity;
        m_ShakeDuration = glm::max(0.01f, duration);
        m_ShakeTime = m_ShakeDuration;
    }

    // ── BP_CAMERA_V1 / ARM_SOLVER_V1 ─────────────────────────────────────────
    glm::vec3 GameCamera::ArmOffsetFor(const glm::vec3& viewDir,
        const glm::vec3& socketOffset)
    {
        if (socketOffset.x == 0.0f && socketOffset.y == 0.0f && socketOffset.z == 0.0f)
            return glm::vec3(0.0f);

        // viewDir aponta da CAMERA para o ALVO — e a mesma convencao do
        // UpdateThirdPerson, onde a posicao e `target - viewDir * distancia`.
        const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

        glm::vec3 right = glm::cross(viewDir, worldUp);
        const float len2 = glm::dot(right, right);

        // Olhando reto para cima ou para baixo, viewDir e paralelo ao up e o
        // produto vetorial zera: normalizar ali devolve NaN e a camera some da
        // cena sem erro nenhum no log. Nesse caso o "lado" do braco e ambiguo
        // de verdade — +X e uma escolha, e o importante e que seja ESTAVEL.
        right = (len2 > 1e-6f) ? right / std::sqrt(len2) : glm::vec3(1.0f, 0.0f, 0.0f);

        // O sinal do Z e negativo porque viewDir aponta PARA o alvo: Z
        // positivo tem de afastar a camera, no mesmo sentido da distancia.
        return right * socketOffset.x
            + worldUp * socketOffset.y
            - viewDir * socketOffset.z;
    }

    // A CONTA DO BRACO — a unica. Ver a nota no header.
    GameCamera::ArmPose GameCamera::SolveSpringArm(const glm::vec3& targetPosition,
        float yawDegrees, float pitchDegrees,
        float distance, float heightOffset,
        const glm::vec3& socketOffset)
    {
        const glm::vec3 viewDir = ForwardFromYawPitch(yawDegrees, pitchDegrees);
        const glm::vec3 offset = ArmOffsetFor(viewDir, socketOffset);

        ArmPose pose;

        // O alvo do BRACO esta a `heightOffset` acima do pivo do pawn...
        pose.Position = targetPosition + glm::vec3(0.0f, heightOffset, 0.0f)
            - viewDir * distance + offset;

        // ...mas a camera olha um pouco ABAIXO dele (metade da altura). Essa
        // meia altura e antiga e deliberada: ela poe o personagem um pouco
        // acima do centro da tela em vez de exatamente no meio.
        pose.LookAt = targetPosition + glm::vec3(0.0f, heightOffset * 0.5f, 0.0f)
            + offset;

        return pose;
    }

    GameCamera::ArmPose GameCamera::SolveSpringArm(const SpringArmComponent& arm,
        const glm::vec3& targetPosition)
    {
        const bool fixed = (arm.Mode == CameraRigMode::Fixed);

        // Os -90 / -10 sao os mesmos que o SceneRuntime usa no Reset quando o
        // braco NAO esta travado. Se aquele default mudar, muda aqui tambem —
        // e so aqui.
        const float yaw = fixed ? arm.FixedYaw : -90.0f;
        const float pitch = fixed ? arm.FixedPitch : -10.0f;

        return SolveSpringArm(targetPosition, yaw, pitch,
            arm.Length, arm.HeightOffset, arm.SocketOffset);
    }

    glm::vec3 GameCamera::ArmOffsetWorld() const
    {
        return ArmOffsetFor(CalcForward(m_Yaw, m_Pitch), TPSocketOffset);
    }

    void GameCamera::OnUpdate(float deltaTime, Window* window)
    {
        if (!window) return;

        // Follow entity via script — atualiza o ponteiro de target a cada frame.
        //
        // BP_CAMERA_V1: este bloco subiu para ANTES do teste de captura. Ele e
        // quem resolve `SetFollowEntity` em `m_TargetPosition`; testar o modo
        // antes dele fazia o primeiro frame apos um `Camera Follow` decidir
        // pelo estado velho.
        if (m_FollowEntity != entt::null && m_FollowScene)
        {
            auto* tc = m_FollowScene->GetRegistry().try_get<TransformComponent>(m_FollowEntity);
            if (tc)
            {
                m_TargetPosition = &tc->Data.Position;
                CameraMode = Mode::ThirdPerson;
            }
            else
            {
                m_FollowEntity = entt::null;
                m_TargetPosition = nullptr;
            }
        }

        // ── BP_CAMERA_V1: a captura de mouse deixou de barrar o FOLLOW ───────
        //
        // Era `if (!MouseCaptured || !window) return;`, a PRIMEIRA linha da
        // funcao. Com isso, um jogo que nao captura o mouse — todo jogo de
        // plataforma — tinha a camera congelada onde o Play a deixou: ela
        // nunca seguia o personagem, e nao havia nada no log dizendo por que.
        //
        // A captura so faz sentido para LER O MOUSE. O FreeFly depende dela
        // (e WASD + mouse look: sem cursor preso nao existe) e continua
        // exigindo-a; o ThirdPerson passa a seguir sempre, e le o mouse
        // apenas quando ha captura (ver UpdateThirdPerson).
        if (CameraMode == Mode::ThirdPerson && m_TargetPosition)
            UpdateThirdPerson(deltaTime, window);
        else if (MouseCaptured)
            UpdateFreeFly(deltaTime, window);

        // Camera Shake — aplica offset randômico que decai com o tempo
        if (m_ShakeTime > 0.f)
        {
            m_ShakeTime -= deltaTime;
            float t = glm::max(0.f, m_ShakeTime / m_ShakeDuration);
            float mag = m_ShakeIntensity * t;

            // Ruído baseado em sin com frequências altas — sem deps externas
            float ox = std::sin(m_ShakeTime * 47.3f + 1.1f) * mag;
            float oy = std::sin(m_ShakeTime * 31.7f + 2.3f) * mag;
            float oz = std::sin(m_ShakeTime * 23.9f + 0.7f) * mag;
            m_Position += glm::vec3(ox, oy, oz);
        }
    }

    void GameCamera::UpdateFreeFly(float deltaTime, Window* window)
    {
        glm::vec3 forward = CalcForward(m_Yaw, m_Pitch);
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
        glm::vec3 up = glm::vec3(0, 1, 0);

        float speed = MoveSpeed * deltaTime;
        if (window->IsKeyDown((int)Key::LeftShift)) speed *= 3.0f;

        if (window->IsKeyDown((int)Key::W)) m_Position += forward * speed;
        if (window->IsKeyDown((int)Key::S)) m_Position -= forward * speed;
        if (window->IsKeyDown((int)Key::A)) m_Position -= right * speed;
        if (window->IsKeyDown((int)Key::D)) m_Position += right * speed;
        if (window->IsKeyDown((int)Key::E)) m_Position += up * speed;
        if (window->IsKeyDown((int)Key::Q)) m_Position -= up * speed;

        glm::vec2 mousePos = window->GetCursorPosition();
        if (m_FirstMouse)
        {
            m_LastMousePos.x = mousePos.x;
            m_LastMousePos.y = mousePos.y;
            m_FirstMouse = false;
        }

        float dx = mousePos.x - m_LastMousePos.x;
        float dy = mousePos.y - m_LastMousePos.y;
        m_LastMousePos.x = mousePos.x;
        m_LastMousePos.y = mousePos.y;

        m_Yaw += dx * Sensitivity;
        m_Pitch -= dy * Sensitivity;
        m_Pitch = std::clamp(m_Pitch, -89.0f, 89.0f);
    }

    void GameCamera::UpdateThirdPerson(float deltaTime, Window* window)
    {
        // ── BP_CAMERA_V1: braco TRAVADO ──────────────────────────────────────
        //
        // Travado vence TPMouseRotates: nao adianta "nao atualizar" o angulo,
        // porque isso deixa o braco onde o ultimo movimento de mouse o parou.
        // Aqui o angulo e IMPOSTO todo frame.
        //
        // O m_FirstMouse fica levantado enquanto trava: no instante em que o
        // jogo destravar (uma cutscene, um modo de mira), a primeira leitura
        // de mouse vira delta zero em vez de um salto do tamanho de tudo que o
        // cursor andou durante a trava.
        if (TPLockRotation)
        {
            m_Yaw = TPLockYaw;
            m_Pitch = TPLockPitch;
            m_FirstMouse = true;
        }
        // ── Rotação com mouse ─────────────────────────────────────────────────
        // MouseCaptured entrou na condicao junto com a mudanca do OnUpdate:
        // sem cursor preso o delta lido e o do mouse passeando pela UI.
        else if (TPMouseRotates && MouseCaptured)
        {
            glm::vec2 mousePos = window->GetCursorPosition();
            if (m_FirstMouse)
            {
                m_LastMousePos.x = mousePos.x;
                m_LastMousePos.y = mousePos.y;
                m_FirstMouse = false;
            }

            float dx = mousePos.x - m_LastMousePos.x;
            float dy = mousePos.y - m_LastMousePos.y;
            m_LastMousePos.x = mousePos.x;
            m_LastMousePos.y = mousePos.y;

            m_Yaw += dx * Sensitivity;
            m_Pitch -= dy * Sensitivity;
            m_Pitch = std::clamp(m_Pitch, -45.0f, 45.0f);
        }

        // ── Calcula posição desejada ──────────────────────────────────────────
        //
        // ARM_SOLVER_V1 — a conta saiu daqui e virou GameCamera::SolveSpringArm,
        // porque o preview do Script Editor e o frustum do viewport precisam
        // dela e tinham cada um a sua versao (ver a nota no header).
        m_DesiredPosition = SolveSpringArm(*m_TargetPosition, m_Yaw, m_Pitch,
            TPDistance, TPHeight, TPSocketOffset).Position;

        // ── Lerp suave para a posição desejada ────────────────────────────────
        //
        // BP_CAMERA_V1 — `EnableCameraLag` desligado agora significa GRUDAR no
        // alvo (alpha = 1). Antes a caixa do Inspector nao tinha efeito nenhum
        // no jogo: o valor nem chegava aqui.
        float alpha = TPEnableLag ? std::min(1.0f, TPLagSpeed * deltaTime) : 1.0f;
        m_Position = glm::mix(m_Position, m_DesiredPosition, alpha);
    }

    glm::mat4 GameCamera::GetViewMatrix() const
    {
        if (CameraMode == Mode::ThirdPerson && m_TargetPosition)
        {
            // Olha para o player — DESLOCADO pelo mesmo offset que deslocou a
            // camera (BP_CAMERA_V1). Sem somar o offset aqui, mover o socket
            // giraria a camera de volta para o personagem em vez de deslocar o
            // enquadramento: o personagem ficaria sempre no centro da tela e o
            // campo pareceria nao ter efeito nenhum.
            //
            // Com offset zero esta conta e IDENTICA a de antes, entao nenhuma
            // camera de terceira pessoa ja existente muda de comportamento.
            //
            // ARM_SOLVER_V1 — o mesmo solver da posicao, para que as duas
            // metades nao possam divergir.
            const glm::vec3 lookAt = SolveSpringArm(*m_TargetPosition, m_Yaw, m_Pitch,
                TPDistance, TPHeight, TPSocketOffset).LookAt;
            return glm::lookAt(m_Position, lookAt, glm::vec3(0, 1, 0));
        }
        glm::vec3 forward = CalcForward(m_Yaw, m_Pitch);
        return glm::lookAt(m_Position, m_Position + forward, glm::vec3(0, 1, 0));
    }

    glm::mat4 GameCamera::GetProjectionMatrix(float aspectRatio) const
    {
        return glm::perspective(glm::radians(Fov), aspectRatio, NearClip, FarClip);
    }

    glm::mat4 GameCamera::GetViewProjectionMatrix(float aspectRatio) const
    {
        return GetProjectionMatrix(aspectRatio) * GetViewMatrix();
    }

    void GameCamera::Reset(const glm::vec3& position, float yaw, float pitch)
    {
        m_Position = position;
        m_DesiredPosition = position;
        m_Yaw = yaw;
        m_Pitch = pitch;
        m_FirstMouse = true;
    }

} // namespace axe