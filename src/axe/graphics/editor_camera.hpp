#pragma once


#include "axe/utils/glm_config.hpp"

#include "axe/graphics/editor_camera.hpp"
#include "axe/core/types.hpp"
#include "axe/graphics/camera.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>


namespace axe
{
	class AXE_API EditorCamera : public Camera
	{
	public:
		EditorCamera() = default;
		EditorCamera(float fovDegrees, float aspectRatio, float nearClip, float farClip);

		void OnUpdate(float deltaTime);
		void OnMouseScroll(float delta);

		void SetViewportSize(float width, float height);

		const glm::mat4& GetViewMatrixCached() const { return m_ViewMatrix; }
		glm::mat4 GetViewProjectionMatrix() const override;
		glm::mat4 GetViewMatrix() const override;
		const glm::vec3& GetPosition() const { return m_Position; }
		float GetPitch() const { return m_Pitch; }
		float GetYaw() const { return m_Yaw; }
		float GetDistance() const { return m_Distance; }
		const glm::vec3& GetFocalPoint() const { return m_FocalPoint; }

		void Rotate(const glm::vec2& delta);
		void Pan(const glm::vec2& delta);
		void Zoom(float delta);
		
		void UpdateView();

	
		glm::mat4 m_ViewMatrix{ 1.0f };
		float GetViewportWidth()  const { return m_ViewportWidth; }
		float GetViewportHeight() const { return m_ViewportHeight; }

		glm::vec3 GetForwardDirection() const;

	private:
		

		glm::vec3 CalculatePosition() const;

		glm::vec3 GetUpDirection() const;
		glm::vec3 GetRightDirection() const;
		

		glm::quat GetOrientation() const;

		void MousePan(const glm::vec2& delta);
		void MouseRotate(const glm::vec2& delta);
		void MouseZoom(float delta);

		std::pair<float, float> PanSpeed() const;
		float RotationSpeed() const;
		float ZoomSpeed() const;

		
		

	private:
		

		glm::vec3 m_Position{ 0.0f, 0.0f, 3.0f };
		glm::vec3 m_FocalPoint{ 0.0f, 0.0f, 0.0f };

		glm::vec2 m_InitialMousePosition{ 0.0f, 0.0f };

		float m_Distance = 1.5f;
		float m_Pitch = 0.0f;
		float m_Yaw = 0.0f;

		float m_ViewportWidth = 1280.0f;
		float m_ViewportHeight = 720.0f;
	};
}