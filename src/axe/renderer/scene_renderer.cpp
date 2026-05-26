#include "scene_renderer.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/scene/components.hpp"
#include "axe/log/log.hpp"

#include "axe/graphics/renderer/post_process_pass.hpp"
#include "axe/graphics/render_command.hpp"
namespace axe
{

	SceneRenderer::SceneRenderer() {}

	void SceneRenderer::RenderScene(const Scene& scene,
		const EditorCamera& camera,
		entt::entity selectedEntity)
	{
		const glm::mat4 viewProjection = camera.GetViewProjectionMatrix();
		const glm::mat4 projection = camera.GetProjectionMatrix();
		const glm::vec3 cameraPosition = camera.GetPosition();
		uint32_t width = (uint32_t)camera.GetViewportWidth();
		uint32_t height = (uint32_t)camera.GetViewportHeight();
		auto& registry = const_cast<Scene&>(scene).GetRegistry();

		const DirectionalLight* light = nullptr;
		for (auto entity : registry.view<LightComponent>())
		{
			auto& lc = registry.get<LightComponent>(entity);
			if (lc.Data) { light = lc.Data.get(); break; }
		}

		RenderShadowPass(scene, light);

		if (m_DeferredEnabled && m_TargetFBO != 0)
		{
			RenderDeferredScene(scene, viewProjection, projection,
				cameraPosition, selectedEntity, light, width, height);
		}
		else
		{
			// ✅ Forward — exatamente como estava antes
			m_MeshRenderer.SetEnvironment(m_Environment);
			if (m_ShadowPass)
				m_MeshRenderer.SetShadowMap(
					m_ShadowPass->GetDepthMapID(),
					m_ShadowPass->GetLightSpaceMatrix());

			m_CubeRenderer.Begin(viewProjection);
			m_LineRenderer.Begin(viewProjection);
			m_MeshRenderer.Begin(viewProjection, cameraPosition);

			auto roots = const_cast<Scene&>(scene).GetRootEntities();
			for (auto entity : roots)
				RenderEntity(scene, entity, glm::mat4(1.0f), selectedEntity, light);

			m_MeshRenderer.End();
			m_LineRenderer.End();
			m_CubeRenderer.End();
		}
	}

	
	void SceneRenderer::RenderScene(const Scene& scene,
		const glm::mat4& view,
		const glm::mat4& projection,
		const glm::vec3& cameraPosition,
		entt::entity selectedEntity)
	{
		glm::mat4 viewProjection = projection * view;
		auto& registry = const_cast<Scene&>(scene).GetRegistry();

		const DirectionalLight* light = nullptr;
		for (auto entity : registry.view<LightComponent>())
		{
			auto& lc = registry.get<LightComponent>(entity);
			if (lc.Data) { light = lc.Data.get(); break; }
		}

		RenderShadowPass(scene, light);

		m_MeshRenderer.SetEnvironment(m_Environment);
		if (m_ShadowPass)
			m_MeshRenderer.SetShadowMap(
				m_ShadowPass->GetDepthMapID(),
				m_ShadowPass->GetLightSpaceMatrix());

		m_CubeRenderer.Begin(viewProjection);
		m_LineRenderer.Begin(viewProjection);
		m_MeshRenderer.Begin(viewProjection, cameraPosition);

		auto roots = const_cast<Scene&>(scene).GetRootEntities();
		for (auto entity : roots)
			RenderEntity(scene, entity, glm::mat4(1.0f), selectedEntity, light);

		m_MeshRenderer.End();
		m_LineRenderer.End();
		m_CubeRenderer.End();
	}
	
	void SceneRenderer::RenderEntity(const Scene& scene, entt::entity entity,
		const glm::mat4& parentTransform, entt::entity selectedEntity,
		const DirectionalLight* light)
	{
		auto& registry = const_cast<Scene&>(scene).GetRegistry();
		if (!registry.valid(entity)) return;
		if (registry.any_of<PostProcessComponent>(entity)) return;
		if (registry.any_of<FolderComponent>(entity))
		{
			auto* rel = registry.try_get<RelationshipComponent>(entity);
			if (rel)
				for (auto child : rel->Children)
					RenderEntity(scene, child, glm::mat4(1.0f), selectedEntity, light);
			return;
		}

		if (registry.any_of<LightComponent>(entity)) return;

		auto* tc = registry.try_get<TransformComponent>(entity);
		// Usa transform próprio — NÃO acumula com pai (hierarquia é só organização)
		glm::mat4 worldTransform = tc ? tc->Data.GetMatrix() : glm::mat4(1.0f);

		bool selected = (entity == selectedEntity);
		auto* mc = registry.try_get<MeshComponent>(entity);
		auto* mat = registry.try_get<MaterialComponent>(entity);

		if (mc && mc->Data)
		{
			//AXE_CORE_INFO("SceneRenderer {:p} drawing entity {}", (void*)this, (uint32_t)entity);
			m_MeshRenderer.DrawMesh(*mc->Data, worldTransform,
				mat ? mat->Data.get() : nullptr, light);
		}
		else
			m_CubeRenderer.DrawCube(worldTransform, selected);

		if (selected)
			m_LineRenderer.DrawBoundingBox(worldTransform, { 1.0f, 0.0f, 0.0f, 1.0f });

		auto* rel = registry.try_get<RelationshipComponent>(entity);
		if (rel)
			for (auto child : rel->Children)
				RenderEntity(scene, child, glm::mat4(1.0f), selectedEntity, light);
	}
	void SceneRenderer::RenderShadowPass(const Scene& scene, const DirectionalLight* light)
	{
		//AXE_CORE_INFO("RenderShadowPass chamado - light: {}", light != nullptr);

		if (!light || !light->CastShadows) return;
		if (!m_ShadowPass)
			m_ShadowPass = ShadowMapPass::Create();

		if (!m_ShadowPass->IsInitialized())
			m_ShadowPass->Initialize(4096);
			//m_ShadowPass->Initialize(2048);

		auto lsm = ShadowMapPass::CalcLightSpaceMatrix(
			light->Direction, light->ShadowDistance);

		m_ShadowPass->Begin(lsm);

		auto& registry = const_cast<Scene&>(scene).GetRegistry();
		auto roots = const_cast<Scene&>(scene).GetRootEntities();

		int meshCount = 0;
		std::function<void(entt::entity)> renderDepth = [&](entt::entity entity)
			{
				//AXE_CORE_INFO("renderDepth entity: {} valid: {}", (uint32_t)entity, registry.valid(entity));
				if (!registry.valid(entity)) return;
				if (registry.any_of<LightComponent>(entity)) return;
				
				if (registry.any_of<PostProcessComponent>(entity)) return;
				if (registry.any_of<FolderComponent>(entity))
				{
					//AXE_CORE_INFO("skip folder");
					auto* rel = registry.try_get<RelationshipComponent>(entity);
					if (rel) for (auto child : rel->Children) renderDepth(child);
					return;
				}

				auto* tc = registry.try_get<TransformComponent>(entity);
				auto* mc = registry.try_get<MeshComponent>(entity);				
				if (mc && mc->Data && tc)
				{					
					m_ShadowPass->DrawMesh(*mc->Data, tc->Data.GetMatrix());				
					meshCount++;
				}

				auto* rel = registry.try_get<RelationshipComponent>(entity);
				if (rel) for (auto child : rel->Children) renderDepth(child);
			};
		//AXE_CORE_INFO("roots count: {}", roots.size());

		for (auto entity : roots)
			renderDepth(entity);

		//AXE_CORE_INFO("ShadowPass: {} meshes renderizados", meshCount);
		m_ShadowPass->End();
	}

	void SceneRenderer::GeometryPassEntity(const Scene& scene, entt::entity entity)
	{
		auto& registry = const_cast<Scene&>(scene).GetRegistry();
		if (!registry.valid(entity)) return;
		if (registry.any_of<PostProcessComponent>(entity)) return;
		if (registry.any_of<LightComponent>(entity)) return;
		if (registry.any_of<FolderComponent>(entity))
		{
			auto* rel = registry.try_get<RelationshipComponent>(entity);
			if (rel) for (auto child : rel->Children)
				GeometryPassEntity(scene, child);
			return;
		}

		auto* tc = registry.try_get<TransformComponent>(entity);
		auto* mc = registry.try_get<MeshComponent>(entity);
		auto* mat = registry.try_get<MaterialComponent>(entity);

		if (mc && mc->Data && tc)
			m_GeometryPass->DrawMesh(*mc->Data, tc->Data.GetMatrix(),
				mat ? mat->Data.get() : nullptr);

		auto* rel = registry.try_get<RelationshipComponent>(entity);
		if (rel) for (auto child : rel->Children)
			GeometryPassEntity(scene, child);
	}


	void SceneRenderer::RenderDeferredScene(const Scene& scene,
		const glm::mat4& viewProjection,
		const glm::mat4& projection,
		const glm::vec3& cameraPosition,
		entt::entity selectedEntity,
		const DirectionalLight* light,
		uint32_t width, uint32_t height)
	{
		// Inicializa passes se necessário
		if (!m_GeometryPass)
		{
			m_GeometryPass = GeometryPass::Create();
			m_GeometryPass->Initialize();
		}
		if (!m_SSAOPass)
		{
			m_SSAOPass = SSAOPass::Create();
			m_SSAOPass->Initialize(width, height);
		}
		if (!m_LightingPass)
		{
			m_LightingPass = LightingPass::Create();
			m_LightingPass->Initialize();
		}
		if (!m_GBuffer.IsInitialized())
			m_GBuffer.Initialize(width, height);

		// Resize
		if (width != m_Width || height != m_Height)
		{
			m_Width = width;
			m_Height = height;
			m_GBuffer.Resize(width, height);
			if (m_SSAOPass) m_SSAOPass->Resize(width, height);
		}

		// 1. Geometry pass
		m_GeometryPass->Begin(m_GBuffer, viewProjection, cameraPosition);
		auto roots = const_cast<Scene&>(scene).GetRootEntities();
		for (auto entity : roots)
			GeometryPassEntity(scene, entity);
		m_GeometryPass->End();

		// 2. SSAO
		if (m_SSAOSettings.Enabled)
			m_SSAOPass->Execute(m_GBuffer, projection, m_SSAOSettings);

		// 3. Lighting — escreve no m_TargetFBO (HDR)
		RenderCommand::BindFramebuffer(m_TargetFBO);
		RenderCommand::SetViewport(0, 0, width, height);

		uint32_t ssaoID = m_SSAOSettings.Enabled ? m_SSAOPass->GetOcclusionTextureID() : 0;
		uint32_t shadowID = m_ShadowPass ? m_ShadowPass->GetDepthMapID() : 0;
		glm::mat4 lsm = m_ShadowPass ? m_ShadowPass->GetLightSpaceMatrix() : glm::mat4(1.0f);

		m_LightingPass->Execute(m_GBuffer, ssaoID, shadowID, lsm,
			cameraPosition, light, m_Environment);

		// 4. Forward por cima — cubo renderer e line renderer
		m_CubeRenderer.Begin(viewProjection);
		m_LineRenderer.Begin(viewProjection);
		for (auto entity : roots)
			RenderEntity(scene, entity, glm::mat4(1.0f), selectedEntity, light);
		m_LineRenderer.End();
		m_CubeRenderer.End();
	}

} // namespace axe