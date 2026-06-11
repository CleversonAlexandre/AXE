#include "game_camera.hpp"
#include "axe/log/log.hpp"
#include "axe/axe_window/window.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include "axe/input/key_codes.hpp"

namespace axe
{
    static glm::vec3 CalcForward(float yaw, float pitch)
    {
        glm::vec3 forward;
        forward.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        forward.y = sin(glm::radians(pitch));
        forward.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        return glm::normalize(forward);
    }

    void GameCamera::OnUpdate(float deltaTime, Window* window)
    {
        if (!MouseCaptured || !window) return;

        if (CameraMode == Mode::ThirdPerson && m_TargetPosition)
            UpdateThirdPerson(deltaTime, window);
        else
            UpdateFreeFly(deltaTime, window);
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
        // ── Rotação com mouse ─────────────────────────────────────────────────
        if (TPMouseRotates)
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
        // Orbita em volta do player: atrás e acima
        glm::vec3 target = *m_TargetPosition + glm::vec3(0, TPHeight, 0);

        // Direção da câmera baseada no yaw/pitch
        glm::vec3 camDir;
        camDir.x = cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
        camDir.y = sin(glm::radians(m_Pitch));
        camDir.z = sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
        camDir = glm::normalize(camDir);

        m_DesiredPosition = target - camDir * TPDistance;

        // ── Lerp suave para a posição desejada ────────────────────────────────
        float alpha = std::min(1.0f, TPLagSpeed * deltaTime);
        m_Position = glm::mix(m_Position, m_DesiredPosition, alpha);
    }

    glm::mat4 GameCamera::GetViewMatrix() const
    {
        if (CameraMode == Mode::ThirdPerson && m_TargetPosition)
        {
            // Olha sempre para o player
            glm::vec3 lookAt = *m_TargetPosition + glm::vec3(0, TPHeight * 0.5f, 0);
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