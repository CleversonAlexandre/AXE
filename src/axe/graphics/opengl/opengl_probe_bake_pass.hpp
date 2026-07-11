#pragma once
#include "axe/graphics/renderer/probe_bake_pass.hpp"
#include "axe/graphics/renderer/shadow_map_pass.hpp"
#include "axe/lighting/reflection_probe.hpp"

namespace axe
{
	class Shader;

	class OpenGLProbeBakePass final : public ProbeBakePass
	{
	public:
		void Initialize() override;
		bool IsInitialized() const override { return m_Initialized; }

		std::shared_ptr<ProbeGrid> Bake(const RenderQueue& queue,
			const SceneEnvironment* environment,
			const ProbeBakeRequest& request) override;

		std::shared_ptr<ReflectionCapture> CaptureReflection(
			const RenderQueue& queue,
			const SceneEnvironment* environment,
			const ReflectionBakeRequest& request,
			const ProbeVolumeData* giVolume = nullptr) override;

	private:
		// Dono do cubemap pré-filtrado de um Reflection Probe — deleta a
		// textura GL no destrutor (RAII através da fronteira abstrata).
		class OpenGLReflectionCapture final : public ReflectionCapture
		{
		public:
			explicit OpenGLReflectionCapture(uint32_t prefilteredID)
				: m_PrefilteredID(prefilteredID) {}
			~OpenGLReflectionCapture() override;
			uint32_t GetPrefilteredID() const override { return m_PrefilteredID; }
			bool IsValid() const override { return m_PrefilteredID != 0; }
		private:
			uint32_t m_PrefilteredID = 0;
		};

		// Fonte de bounce — grid SH da passada anterior do bake, usado
		// como termo ambiente das superfícies na passada seguinte
		// (multi-bounce). Ponteiros crus são seguros: os Texture3D donos
		// vivem em shared_ptr locais do Bake() durante toda a passada.
		struct BounceSource
		{
			const Texture3D* SH0 = nullptr;
			const Texture3D* SH1X = nullptr;
			const Texture3D* SH1Y = nullptr;
			const Texture3D* SH1Z = nullptr;
			glm::mat4 WorldToLocal{ 1.0f }; // sem escala
			glm::vec3 HalfExtents{ 1.0f };
		};

		void RenderFace(const RenderQueue& queue,
			const SceneEnvironment* environment,
			const glm::vec3& probePos,
			const glm::vec3& fwd, const glm::vec3& up,
			float farClip,
			uint32_t shadowMapID, const glm::mat4& lightSpaceMatrix,
			const BounceSource* bounce);

		// Desenha a cena (céu + geometria com sol/sombra/bounce) a partir
		// de um ponto, no FBO e viewport ATUALMENTE bindados — o miolo
		// compartilhado entre o RenderFace (bake de GI, 32x32 + readback)
		// e o CaptureReflection (faces do cubemap em resolução maior).
		void DrawSceneFromPoint(const RenderQueue& queue,
			const SceneEnvironment* environment,
			const glm::vec3& viewPos,
			const glm::vec3& fwd, const glm::vec3& up,
			float farClip,
			uint32_t shadowMapID, const glm::mat4& lightSpaceMatrix,
			const BounceSource* bounce);

		// Renderiza o shadow map do sol centrado em `center` (raio de
		// cobertura `radius`), devolvendo o depth map id e a light-space
		// matrix — compartilhado entre Bake e CaptureReflection.
		uint32_t RenderSunShadow(const RenderQueue& queue,
			const glm::vec3& center, float radius, glm::mat4& outLsm);

		// Resolução de cada face do mini cubemap — 32x32 é suficiente pra
		// projeção SH L1 (a SH só guarda a "média direcional" de qualquer
		// jeito) e mantém o readback barato.
		static constexpr int kFaceRes = 32;

		std::shared_ptr<Shader> m_GeoShader;   // geometria: albedo * sol + sombra
		std::shared_ptr<Shader> m_SkyShader;   // fullscreen: cubemap do céu
		std::shared_ptr<Shader> m_PrefilterShader; // GGX prefilter (Reflection Probes)
		std::shared_ptr<ShadowMapPass> m_ShadowPass; // sombra própria do bake

		uint32_t m_FBO = 0;
		uint32_t m_ColorTex = 0;
		uint32_t m_DepthRBO = 0;
		uint32_t m_SkyVAO = 0; // triângulo fullscreen (sem VBO — gl_VertexID)
		bool     m_Initialized = false;
	};
}