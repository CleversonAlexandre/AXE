#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/log/log.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/renderer/cube_renderer.hpp"
#include "axe/graphics/editor_camera.hpp"

#include "axe/renderer/scene_renderer.hpp"
#include "axe/scene/scene.hpp"
#include "axe/scene/scene_objects.hpp"

#include <glad/glad.h>
#include "axe/utils/glm_config.hpp"


namespace axe
{

	

	static bool DecomposeTransform(const glm::mat4& transform, glm::vec3& position, glm::vec3& rotation, glm::vec3& scale)
	{
		using namespace glm;

		vec3 skew;
		vec4 perspective;
		quat orientation;

		if (!decompose(transform, scale, orientation, position, skew, perspective))
			return false;

		rotation = eulerAngles(orientation);
		return true;
	}

	void ViewportRenderer::Initialize()
	{
		m_SceneRenderer = std::make_unique<SceneRenderer>();
		m_Scene = std::make_unique<Scene>();
		m_Camera = std::make_unique<EditorCamera>(45.0f, 1.0f, 0.1f, 100.0f);

		

		std::uint32_t firstID = 0;

		{
			auto& cubeA = m_Scene->CreateObject("Cube A");
			cubeA.TransformData.Position = { 0.0f, 0.0f, 0.0f };
			firstID = cubeA.ID;
		}

		{
			auto& cubeB = m_Scene->CreateObject("Cube B");
			cubeB.TransformData.Position = { 2.0f, 0.0f, 0.0f };
			cubeB.TransformData.Rotation.y = glm::radians(25.0f);
		}

		{
			auto& cubeC = m_Scene->CreateObject("Cube C");
			cubeC.TransformData.Position = { -2.0f, 0.0f, 0.0f };
			cubeC.TransformData.Scale = { 1.0f, 2.0f, 1.0f };
		}

		m_SelectedObjectID = firstID;
	}
	SceneObject* ViewportRenderer::GetSelectedObject()
	{
		if (!m_Scene || m_SelectedObjectID == 0)
			return nullptr;


		

		return m_Scene->FindObjectByID(m_SelectedObjectID);



	}

	const SceneObject* ViewportRenderer::GetSelectedObject() const
	{
		if (!m_Scene || m_SelectedObjectID == 0)
			return nullptr;

		return m_Scene->FindObjectByID(m_SelectedObjectID);
	}

	void ViewportRenderer::RenderToFramebuffer(Framebuffer& framebuffer, std::uint32_t width, std::uint32_t height, float timeSeconds)
	{
		framebuffer.Bind();

		glViewport(0, 0, width, height);
		glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		if (m_Camera && height > 0)
		{
			m_Camera->SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
			m_Camera->SetViewportSize((float)width, (float)height);
		}

		if (m_SceneRenderer && m_Scene && m_Camera)
		{
			m_SceneRenderer->RenderScene(*m_Scene, *m_Camera, m_SelectedObjectID);
		}

		framebuffer.Unbind();
		//DrawGuizmo();
	}

	void ViewportRenderer::OnMouseRotate(const glm::vec2& delta)
	{
		if (m_Camera)
			m_Camera->Rotate(delta);
	}

	void ViewportRenderer::OnMousePan(const glm::vec2& delta)
	{
		if (m_Camera)
			m_Camera->Pan(delta);
	}

	void ViewportRenderer::OnMouseZoom(float delta)
	{
		if (m_Camera)
			m_Camera->Zoom(delta);
	}

	
	
	void ViewportRenderer::DrawGuizmo(const glm::vec2& boundsMin, const glm::vec2& boundsMax)
	{
				
		SceneObject* selectedObject = GetSelectedObject();
		if (!selectedObject || !m_Camera)
			return;

		float width = boundsMax.x - boundsMin.x;
		float height = boundsMax.y - boundsMin.y;

		if (width <= 0.0f || height <= 0.0f)
			return;

		
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(boundsMin.x, boundsMin.y, width, height);

		glm::mat4 view = m_Camera->GetViewMatrix();
		glm::mat4 projection = m_Camera->GetProjectionMatrix();
		glm::mat4 model = selectedObject->TransformData.GetMatrix();

		ImGuizmo::Manipulate(
			glm::value_ptr(view),
			glm::value_ptr(projection),
			m_GuizmoOperation,
			ImGuizmo::LOCAL,
			glm::value_ptr(model)
		);

		if (ImGuizmo::IsUsing())
		{
			//glm::vec3 position;
			//glm::vec3 rotation;
			//glm::vec3 scale;

			selectedObject->TransformData.WorldMatrix = model;
			selectedObject->TransformData.UseWorldMatrix = true;

			glm::vec3 position, rotation, scale;
					
			if (DecomposeTransform(model, position, rotation, scale))
			{			
				selectedObject->TransformData.Position = position;
				selectedObject->TransformData.Rotation = rotation;			
				selectedObject->TransformData.Scale = scale;

				
			}
			
			

		}


	}
}


