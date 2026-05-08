#include "game_camera.hpp"
#include "axe/log/log.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <algorithm>

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

	void GameCamera::OnUpdate(float deltaTime, GLFWwindow* window)
	{
		if (!MouseCaptured || !window) return;
	

		glm::vec3 forward = CalcForward(m_Yaw, m_Pitch);
		glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
		glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

		float speed = MoveSpeed * deltaTime;
		if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
			speed *= 3.0f;

		if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) m_Position = m_Position + forward * speed;
		if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) m_Position = m_Position - forward * speed;
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) m_Position = m_Position - right * speed;
		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) m_Position = m_Position + right * speed;
		if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) m_Position = m_Position + up * speed;
		if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) m_Position = m_Position - up * speed;

		// Mouse look
		double mx, my;
		glfwGetCursorPos(window, &mx, &my);

		float mouseX = (float)mx;
		float mouseY = (float)my;

		if (m_FirstMouse)
		{
			m_LastMousePos.x = mouseX;
			m_LastMousePos.y = mouseY;
			m_FirstMouse = false;
		}

		float deltaX = mouseX - m_LastMousePos.x;
		float deltaY = mouseY - m_LastMousePos.y;

		m_LastMousePos.x = mouseX;
		m_LastMousePos.y = mouseY;

		m_Yaw = m_Yaw + deltaX * Sensitivity;
		m_Pitch = m_Pitch - deltaY * Sensitivity;

		if (m_Pitch > 89.0f) m_Pitch = 89.0f;
		if (m_Pitch < -89.0f) m_Pitch = -89.0f;
	}

	glm::mat4 GameCamera::GetViewMatrix() const
	{
		glm::vec3 forward = CalcForward(m_Yaw, m_Pitch);
		return glm::lookAt(m_Position, m_Position + forward, glm::vec3(0.0f, 1.0f, 0.0f));
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
		m_Yaw = yaw;
		m_Pitch = pitch;
		m_FirstMouse = true;
	}

} // namespace axe