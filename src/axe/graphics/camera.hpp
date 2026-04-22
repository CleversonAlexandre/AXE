#pragma once
#include "axe/core/types.hpp"
#include <glm/glm.hpp>

namespace axe
{
	class AXE_API Camera
	{
	public:
		Camera() = default;
		Camera(float fovDegrees, float aspectRatio, float nearClip, float farClip);

		void SetPerspective(float fovDegrees, float aspectRatio, float nearClip, float farClip);

		void SetPosition(const glm::vec3& position);
		void SetTarget(const glm::vec3& target);
		void SetUp(const glm::vec3& up);

		void SetAspectRatio(float aspectRatio);

		const glm::vec3& GetPosition() const { return m_Position; }
		const glm::vec3& GetTarget() const { return m_Target; }
		const glm::vec3& GetUp() const { return m_Up; }

		float GetAspectRatio() const { return m_AspectRatio; }

		glm::mat4 GetViewMatrix() const;
		glm::mat4 GetProjectionMatrix() const;
		glm::mat4 GetViewProjectionMatrix() const;

		

	private:
		glm::vec3 m_Position = glm::vec3(0.0f, 0.0f, 3.0f);
		glm::vec3 m_Target = glm::vec3(0.0f, 0.0f, 0.0f);
		glm::vec3 m_Up = glm::vec3(0.0f, 1.0f, 0.0f);

		float m_FovDegrees = 45.0f;
		float m_AspectRatio = 1.0f;
		float m_NearClip = 0.1f;
		float m_FarClip = 100.0f;
	};
}