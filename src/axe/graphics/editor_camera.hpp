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

		// VIEW_GIZMO_V1 — publica porque o gizmo de navegacao fala em
		// quaternion, e esta e a FONTE UNICA da convencao de orientacao
		// (`quat(vec3(-pitch, -yaw, 0))`). Recalcular a mesma formula do lado
		// de fora criaria dois lugares que precisam concordar — a armadilha
		// que ja mordeu tres vezes nesta engine com listas de tipo.
		glm::quat GetOrientation() const;

		void Rotate(const glm::vec2& delta);
		void Pan(const glm::vec2& delta);
		void Zoom(float delta);

		// Enquadra a camera de uma vez: ponto focal + distancia. Usado pelos
		// previews pra enquadrar o conteudo (ex.: o personagem do AnimGraph)
		// sem depender do usuario orbitar ate achar.
		void SetView(const glm::vec3& focalPoint, float distance)
		{
			m_FocalPoint = focalPoint;
			m_Distance = distance;
			UpdateView();
		}


		// ── PILOTAR A CAMERA A PARTIR DE UM PONTO E UMA DIRECAO ──────────────
		//
		// A EditorCamera e ORBITAL: ela guarda foco + distancia + yaw/pitch, e
		// a posicao e derivada disso (`m_FocalPoint - forward * m_Distance`).
		// Nao havia como dizer "fique AQUI olhando para ALI" — so `SetView`,
		// que move o foco mas nao gira.
		//
		// `PointAt` resolve o inverso: escolhe yaw/pitch a partir da direcao e
		// poe o foco adiante, de modo que a posicao calculada caia exatamente
		// no ponto pedido. E o que permite ver pela camera da cena sem trocar o
		// caminho de render.
		//
		// `forward` no espaco do MUNDO, normalizado por quem chama ou nao — a
		// funcao normaliza.
		void PointAt(const glm::vec3& position, const glm::vec3& forward);

		// Estado orbital cru. Existe para SALVAR e RESTAURAR a vista quando o
		// usuario entra e sai do modo "ver pela camera": sem isto, sair
		// devolveria a camera para onde a cutscene a deixou, e o enquadramento
		// que a pessoa tinha antes se perderia.
		void SetOrbit(const glm::vec3& focalPoint, float distance, float yaw, float pitch);

		void UpdateView();


		glm::mat4 m_ViewMatrix{ 1.0f };
		float GetViewportWidth()  const { return m_ViewportWidth; }
		float GetViewportHeight() const { return m_ViewportHeight; }

		glm::vec3 GetForwardDirection() const;

	private:


		glm::vec3 CalculatePosition() const;

		glm::vec3 GetUpDirection() const;
		glm::vec3 GetRightDirection() const;

		// VIEW_GIZMO_V1 — a declaracao subiu para o bloco publico, junto dos
		// outros getters de estado. Aqui ficaria duplicada.

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