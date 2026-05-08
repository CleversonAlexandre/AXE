#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"

struct GLFWwindow; // forward declaration

namespace axe
{

	class AXE_API GameCamera
	{
	public:
		GameCamera() = default;

		void OnUpdate(float deltaTime, GLFWwindow* window);

		glm::mat4 GetViewMatrix() const;
		glm::mat4 GetProjectionMatrix(float aspectRatio) const;
		glm::mat4 GetViewProjectionMatrix(float aspectRatio) const;

		const glm::vec3& GetPosition() const { return m_Position; }

		void Reset(const glm::vec3& position, float yaw, float pitch);

		float MoveSpeed = 5.0f;
		float Sensitivity = 0.1f;
		float Fov = 60.0f;
		float NearClip = 0.1f;
		float FarClip = 1000.0f;

		bool MouseCaptured = false;
		bool      m_FirstMouse = true;
	private:
		glm::vec3 m_Position{ 0.0f, 1.0f, 5.0f };
		float     m_Yaw = -90.0f;
		float     m_Pitch = 0.0f;
		float     m_LastMousePos_x = 0.0f;
		float     m_LastMousePos_y = 0.0f;
		

		// Alias para compatibilidade
		struct { float x = 0.0f; float y = 0.0f; } m_LastMousePos;
	};

} // namespace axe