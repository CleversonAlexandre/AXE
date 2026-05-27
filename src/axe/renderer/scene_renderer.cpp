#include "scene_renderer.hpp"
#include "axe/utils/glm_config.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/scene/components.hpp"
#include "axe/log/log.hpp"

#include "axe/graphics/renderer/post_process_pass.hpp"
#include "axe/graphics/render_command.hpp"

#include <glad/glad.h>
#include "axe/graphics/renderer/skybox_renderer.hpp"

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
		const glm::mat4 view = camera.GetViewMatrix();
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

	

		if (m_DeferredEnabled && m_DeferredSupported && m_TargetFBO != 0)
		{
			RenderDeferredScene(scene, viewProjection, view, projection,
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
		const glm::mat4& view,
		const glm::mat4& projection,
		const glm::vec3& cameraPosition,
		entt::entity selectedEntity,
		const DirectionalLight* light,
		uint32_t width, uint32_t height)
	{		

		

		// --- Resize ---
		if (width != m_Width || height != m_Height)
		{
			m_Width = width;
			m_Height = height;
			m_GBuffer.Resize(width, height);
			if (m_SSAOPass) m_SSAOPass->Resize(width, height);
		}

		auto roots = const_cast<Scene&>(scene).GetRootEntities();

		// --- 1. Shadow Pass ---
		// (já chamado antes em RenderScene — não duplica)

		// --- 2. Geometry Pass → G-Buffer ---
		
		m_GeometryPass->Begin(m_GBuffer, viewProjection, cameraPosition);
		
		int meshCount = 0;
		for (auto entity : roots)
		{
			auto& registry = const_cast<Scene&>(scene).GetRegistry();
			if (registry.try_get<MeshComponent>(entity))
				meshCount++;
			GeometryPassEntity(scene, entity);
		}		
		m_GeometryPass->End();		

		// --- 3. SSAO Pass ---
		uint32_t ssaoID = 0;
		if (m_SSAOSettings.Enabled && m_SSAOPass && m_SSAOPass->IsInitialized())
		{
			m_SSAOPass->Execute(m_GBuffer, projection, view, m_SSAOSettings);
			ssaoID = m_SSAOPass->GetOcclusionTextureID();
		}

		// --- 4. Lighting Pass → HDR Framebuffer ---
		RenderCommand::BindFramebuffer(m_TargetFBO);
		RenderCommand::SetViewport(0, 0, width, height);

		// Copia depth do G-Buffer para o HDR
		// para que o forward pass respeite a profundidade
		//RenderCommand::BlitDepth(m_GBuffer.GetFramebufferID(), m_TargetFBO, width, height);

		uint32_t shadowID = m_ShadowPass ? m_ShadowPass->GetDepthMapID() : 0;
		glm::mat4 lsm = m_ShadowPass ? m_ShadowPass->GetLightSpaceMatrix() : glm::mat4(1.0f);

		m_LightingPass->Execute(m_GBuffer, ssaoID, shadowID, lsm,
			cameraPosition, light, m_Environment);

		// --- 5. Skybox --- 
		// Renderizado após o lighting com depth do G-Buffer já copiado
		// GL_LEQUAL garante que o skybox aparece atrás de tudo
		if (m_SkyboxRenderer)
			m_SkyboxRenderer->RenderDeferred(m_SkyboxView, m_SkyboxProjection);
		//	m_SkyboxRenderer->Render(m_SkyboxView, m_SkyboxProjection);

		// --- 6. Forward Pass ---
		// CubeRenderer (objetos sem mesh), LineRenderer (bounding boxes, seleção)
		RenderCommand::SetDepthTest(true);
		RenderCommand::SetDepthFunc(RendererAPI::DepthFunc::Less);

		//m_CubeRenderer.Begin(viewProjection);
		//m_LineRenderer.Begin(viewProjection);
		//for (auto entity : roots)
		//	RenderEntity(scene, entity, glm::mat4(1.0f), selectedEntity, light);
		//m_LineRenderer.End();
		//m_CubeRenderer.End();

		// --- Restaura estado ---
		RenderCommand::ResetState();
	}

	void SceneRenderer::InitializeDeferredPasses(uint32_t width, uint32_t height)
	{
		m_GeometryPass = GeometryPass::Create();
		m_GeometryPass->Initialize();

		m_LightingPass = LightingPass::Create();
		m_LightingPass->Initialize();

		m_SSAOPass = SSAOPass::Create();
		m_SSAOPass->Initialize(width, height);

		m_GBuffer.Initialize(width, height);

		m_Width = width;
		m_Height = height;
	}

} // namespace axe