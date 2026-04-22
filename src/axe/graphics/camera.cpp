#include "axe/graphics/camera.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> 

namespace axe
{
	Camera::Camera(float fovDegrees, float aspectRatio, float nearClip, float farClip)
		: m_FovDegrees(fovDegrees), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip)
	{
	}

	void Camera::SetPerspective(float fovDegrees, float aspectRatio, float nearClip, float farClip)
	{
		m_FovDegrees = fovDegrees;
		m_AspectRatio = aspectRatio;
		m_NearClip = nearClip;
		m_FarClip = farClip;
	}

	void Camera::SetPosition(const glm::vec3& postion)
	{
		m_Position = postion;
	}

	void Camera::SetTarget(const glm::vec3& target)
	{
		m_Target = target;
	}

	void Camera::SetUp(const glm::vec3& up)
	{
		m_Up = up;
	}

	void Camera::SetAspectRatio(float aspectRatio)
	{
		m_AspectRatio = aspectRatio;
	}


	glm::mat4 Camera::GetViewMatrix() const
	{
		return glm::lookAt(m_Position, m_Target, m_Up);
	}
	glm::mat4 Camera::GetProjectionMatrix() const
	{
		return glm::perspective(
			glm::radians(m_FovDegrees),
			m_AspectRatio,
			m_NearClip,
			m_FarClip
		);
	}
	glm::mat4 Camera::GetViewProjectionMatrix() const
	{
		return GetProjectionMatrix() * GetViewMatrix();
	}

}