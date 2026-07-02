#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/axe_window/window.hpp"
#include <entt/entt.hpp>

namespace axe
{
    class Scene;
    class AXE_API GameCamera
    {
    public:
        GameCamera() = default;

        // ── Modo de câmera ────────────────────────────────────────────────────
        enum class Mode { FreeFly, ThirdPerson };

        void OnUpdate(float deltaTime, Window* window);

        glm::mat4 GetViewMatrix() const;
        glm::mat4 GetProjectionMatrix(float aspectRatio) const;
        glm::mat4 GetViewProjectionMatrix(float aspectRatio) const;

        const glm::vec3& GetPosition() const { return m_Position; }

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

        Mode  CameraMode = Mode::FreeFly;
        bool  MouseCaptured = false;
        bool  m_FirstMouse = true;

    private:
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