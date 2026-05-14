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

		glm::vec3 forward = CalcForward(m_Yaw, m_Pitch);
		glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
		glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

		float speed = MoveSpeed * deltaTime;
		if (window->IsKeyDown((int)Key::LeftShift)) speed *= 3.0f;

		if (window->IsKeyDown((int)Key::W)) m_Position = m_Position + forward * speed;
		if (window->IsKeyDown((int)Key::S)) m_Position = m_Position - forward * speed;
		if (window->IsKeyDown((int)Key::A)) m_Position = m_Position - right * speed;
		if (window->IsKeyDown((int)Key::D)) m_Position = m_Position + right * speed;
		if (window->IsKeyDown((int)Key::E)) m_Position = m_Position + up * speed;
		if (window->IsKeyDown((int)Key::Q)) m_Position = m_Position - up * speed;

		glm::vec2 mousePos = window->GetCursorPosition();

		if (m_FirstMouse)
		{
			m_LastMousePos.x = mousePos.x;
			m_LastMousePos.y = mousePos.y;
			m_FirstMouse = false;
		}

		float deltaX = mousePos.x - m_LastMousePos.x;
		float deltaY = mousePos.y - m_LastMousePos.y;
		m_LastMousePos.x = mousePos.x;
		m_LastMousePos.y = mousePos.y;

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