#include "axe/graphics/editor_camera.hpp"

#include <cmath>
#include "axe/log/log.hpp"


namespace axe
{
	EditorCamera::EditorCamera(float fovDegrees, float aspectRatio, float nearClip, float farClip)
		: Camera(fovDegrees, aspectRatio, nearClip, farClip)
	{
		UpdateView();
	}

	void EditorCamera::SetViewportSize(float width, float height)
	{
		m_ViewportWidth = width;
		m_ViewportHeight = height;

		if (height > 0.0f)
			SetAspectRatio(width / height);
	}

	void EditorCamera::OnUpdate(float deltaTime)
	{
		(void)deltaTime;
		UpdateView();
	}

	void EditorCamera::OnMouseScroll(float delta)
	{
		MouseZoom(delta * 0.1f);
		UpdateView();
	}
	glm::mat4 EditorCamera::GetViewMatrix() const
	{
		return m_ViewMatrix;

	}


	glm::mat4 EditorCamera::GetViewProjectionMatrix() const
	{

		return GetProjectionMatrix() * GetViewMatrix();


	}


	void EditorCamera::SetOrbit(const glm::vec3& focalPoint, float distance,
		float yaw, float pitch)
	{
		m_FocalPoint = focalPoint;
		m_Distance = distance;
		m_Yaw = yaw;
		m_Pitch = pitch;
		UpdateView();
	}

	void EditorCamera::PointAt(const glm::vec3& position, const glm::vec3& forward)
	{
		const glm::vec3 f = glm::normalize(forward);

		// ── A INVERSA DE GetForwardDirection ─────────────────────────────────
		//
		// Aqui a orientacao e `quat(vec3(-pitch, -yaw, 0))` e a frente e o
		// (0,0,-1) girado por ela, o que da:
		//
		//     F = ( cosP*sinY , -sinP , -cosP*cosY )
		//
		// Invertendo: sinP = -F.y, e o par (sinY, cosY) sai de (F.x, -F.z).
		// O atan2 cuida dos quadrantes; o clamp protege o asin de um
		// arredondamento que passe de 1 e devolva NaN — que apareceria como a
		// camera sumindo do mundo, sem nenhuma pista da causa.
		m_Pitch = std::asin(glm::clamp(-f.y, -1.0f, 1.0f));
		m_Yaw = std::atan2(f.x, -f.z);

		// A posicao e DERIVADA (foco - frente * distancia). Entao para a camera
		// cair no ponto pedido, o foco tem de ir para adiante dele, na mesma
		// distancia. A distancia atual e mantida: ela e a escala do pan e do
		// zoom, e zerar faria a navegacao ficar travada ao sair do modo.
		m_FocalPoint = position + f * m_Distance;

		UpdateView();
	}

	void EditorCamera::UpdateView()
	{
		m_Position = CalculatePosition();
		m_ViewMatrix = glm::lookAt(m_Position, m_FocalPoint, GetUpDirection());

	}

	glm::vec3 EditorCamera::CalculatePosition() const
	{
		return m_FocalPoint - GetForwardDirection() * m_Distance;
	}

	glm::vec3 EditorCamera::GetUpDirection() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::vec3 EditorCamera::GetRightDirection() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(1.0f, 0.0f, 0.0f));
	}

	glm::vec3 EditorCamera::GetForwardDirection() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, -1.0f));
	}

	glm::quat EditorCamera::GetOrientation() const
	{
		return glm::quat(glm::vec3(-m_Pitch, -m_Yaw, 0.0f));
	}

	void EditorCamera::MousePan(const glm::vec2& delta)
	{
		auto [xSpeed, ySpeed] = PanSpeed();
		m_FocalPoint += -GetRightDirection() * delta.x * xSpeed * m_Distance;
		m_FocalPoint += GetUpDirection() * delta.y * ySpeed * m_Distance;
	}

	void EditorCamera::MouseRotate(const glm::vec2& delta)
	{
		const float yawSign = GetUpDirection().y < 0.0f ? -1.0f : 1.0f;
		m_Yaw += yawSign * delta.x * RotationSpeed();
		m_Pitch += delta.y * RotationSpeed();
	}

	void EditorCamera::MouseZoom(float delta)
	{
		m_Distance -= delta * ZoomSpeed();
		if (m_Distance < 1.0f)
			m_Distance = 1.0f;
	}

	std::pair<float, float> EditorCamera::PanSpeed() const
	{
		float x = std::min(m_ViewportWidth / 1000.0f, 2.4f);
		float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

		float y = std::min(m_ViewportHeight / 1000.0f, 2.4f);
		float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

		return { xFactor, yFactor };
	}

	float EditorCamera::RotationSpeed() const
	{
		return 0.8f;
	}

	float EditorCamera::ZoomSpeed() const
	{
		float distance = m_Distance * 0.2f;
		distance = std::max(distance, 0.0f);
		float speed = distance * distance;
		return std::min(speed, 100.0f);
	}

	void EditorCamera::Rotate(const glm::vec2& delta)
	{
		MouseRotate(delta);
		UpdateView();
	}

	void EditorCamera::Pan(const glm::vec2& delta)
	{
		MousePan(delta);
		UpdateView();
	}

	void EditorCamera::Zoom(float delta)
	{
		MouseZoom(delta);
		UpdateView();
	}
}