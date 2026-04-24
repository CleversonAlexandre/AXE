#include "scene_renderer.hpp"
#include "axe/utils/glm_config.hpp"

namespace axe
{

	SceneRenderer::SceneRenderer()
	{}

	void SceneRenderer::RenderScene(const Scene& scene, const EditorCamera& camera, std::uint32_t selectedObjectID)
	{
		const glm::mat4 viewProjection = camera.GetViewProjectionMatrix();

		m_CubeRenderer.Begin(viewProjection);

		m_LineRenderer.Begin(viewProjection);

		for (const auto& object : scene.GetObjects())
		{
			glm::mat4 model = object.TransformData.GetMatrix();
			bool selected = object.ID == selectedObjectID;

			// 1. draw normal
			m_CubeRenderer.DrawCube(model, selected);

			// 2. overlay wireframe se selecionado
			if (selected)
			{
				//m_CubeRenderer.DrawCubeWireframe(model);
				m_LineRenderer.DrawBoundingBox(model, { 1.0f, 0.0f, 0.0f, 1.0f });

			}
		}
		m_LineRenderer.End();
		m_CubeRenderer.End();
	}
}

/*
* 
* Cor	Valor
Vermelho{1, 0, 0, 1}
Verde	{0, 1, 0, 1}
Azul	{0, 0, 1, 1}
Amarelo	{1, 1, 0, 1}
Branco	{1, 1, 1, 1}
Preto
*/