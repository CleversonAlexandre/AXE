#include "opengl_probe_bake_pass.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/texture3d.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/material/material.hpp"
#include "axe/log/log.hpp"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include <climits>

namespace axe
{
	// ── Shader de geometria do bake ──────────────────────────────────────────
	// Forward mínimo: albedo (textura ou cor do material) iluminado pelo sol
	// com sombra. É a "cena vista pela probe" — o suficiente pra capturar UM
	// bounce de luz solar (parede iluminada rebate luz colorida na probe) e,
	// principalmente, a OCLUSÃO (probe dentro de sala só enxerga superfícies
	// escuras). Alpha = 1.0 marca geometria (o céu escreve 0.0) — usado na
	// projeção pra calcular a visibilidade do céu de cada probe.
	static const char* s_GeoVert = R"(
		#version 460 core
		layout(location = 0) in vec3 a_Position;
		layout(location = 1) in vec3 a_Normal;
		layout(location = 2) in vec2 a_TexCoord;

		uniform mat4 u_VP;
		uniform mat4 u_Model;

		out vec3 v_WorldPos;
		out vec3 v_Normal;
		out vec2 v_TexCoord;

		void main()
		{
			vec4 world = u_Model * vec4(a_Position, 1.0);
			v_WorldPos = world.xyz;
			v_Normal   = mat3(transpose(inverse(u_Model))) * a_Normal;
			v_TexCoord = a_TexCoord;
			gl_Position = u_VP * world;
		}
	)";

	static const char* s_GeoFrag = R"(
		#version 460 core
		out vec4 FragColor;

		in vec3 v_WorldPos;
		in vec3 v_Normal;
		in vec2 v_TexCoord;

		uniform vec3  u_AlbedoColor;
		uniform sampler2D u_AlbedoMap;
		uniform int   u_HasAlbedoMap;

		// Emissive MÉDIO do material (Material::BakedEmissive) — radiância
		// própria da superfície: soma direto, sem sol, sem albedo. É o que
		// faz telas de arcade banharem as paredes no GI e aparecerem
		// acesas nas reflection probes (mesma captura).
		uniform vec3  u_Emissive;

		uniform int   u_HasSun;
		uniform vec3  u_SunDirection;
		uniform vec3  u_SunColor;     // cor * intensidade
		uniform mat4  u_LightSpaceMatrix;
		uniform sampler2D u_ShadowMap;
		uniform int   u_HasShadowMap;

		// ── Bounce (2ª passada em diante) ────────────────────────────────
		// Grid SH da passada ANTERIOR usado como termo ambiente das
		// superfícies: é isso que transforma 1 bounce em 2 — a parede que
		// a passada 1 viu iluminada pelo sol agora ilumina o chão na
		// passada 2, e a probe captura luz que "dobrou a esquina".
		uniform int  u_HasBounceGrid;
		uniform mat4 u_GridWorldToLocal; // sem escala (escala → HalfExtents)
		uniform vec3 u_GridHalfExtents;
		uniform sampler3D u_SH0;
		uniform sampler3D u_SH1X;
		uniform sampler3D u_SH1Y;
		uniform sampler3D u_SH1Z;

		// Mesma reconstrução L1 do Lighting Pass (convolução cosseno,
		// A0=pi, A1=2pi/3, resultado dividido por pi — semântica de
		// "multiplica pelo albedo e vira radiância de saída").
		vec3 BounceIrradiance(vec3 worldPos, vec3 N)
		{
			vec3 local = (u_GridWorldToLocal * vec4(worldPos, 1.0)).xyz;
			vec3 uvw = clamp(local / (2.0 * u_GridHalfExtents) + 0.5, 0.0, 1.0);
			vec4 sh0  = texture(u_SH0,  uvw);
			vec3 sh1x = texture(u_SH1X, uvw).rgb;
			vec3 sh1y = texture(u_SH1Y, uvw).rgb;
			vec3 sh1z = texture(u_SH1Z, uvw).rgb;
			const float Y00 = 0.282095;
			const float Y1  = 0.488603;
			const float A0  = 3.14159265;
			const float A1  = 2.09439510;
			vec3 E = Y00 * A0 * sh0.rgb
			       + Y1  * A1 * (sh1x * N.x + sh1y * N.y + sh1z * N.z);
			return max(E, vec3(0.0)) / 3.14159265;
		}

		float Shadow(vec3 fragPos, vec3 N, vec3 L)
		{
			if (u_HasShadowMap == 0) return 0.0;
			vec4 ls = u_LightSpaceMatrix * vec4(fragPos, 1.0);
			vec3 proj = ls.xyz / ls.w * 0.5 + 0.5;
			if (proj.z > 1.0) return 0.0;
			float bias = max(0.005 * (1.0 - dot(N, L)), 0.005);
			float d = texture(u_ShadowMap, proj.xy).r;
			return proj.z - bias > d ? 1.0 : 0.0;
		}

		void main()
		{
			// Backface = a probe está enxergando a "casca" da geometria
			// por DENTRO — forte indício de probe enterrada em parede/
			// chão. Marca com alpha = -1 (RGBA16F aceita negativo): a
			// projeção na CPU usa a fração de texels backface pra
			// invalidar a probe, e esses texels não entram na SH (não
			// são luz real) nem contam como céu na visibilidade.
			if (!gl_FrontFacing)
			{
				FragColor = vec4(0.0, 0.0, 0.0, -1.0);
				return;
			}

			vec3 albedo = u_AlbedoColor;
			if (u_HasAlbedoMap == 1)
				albedo *= texture(u_AlbedoMap, v_TexCoord).rgb;

			vec3 N = normalize(v_Normal);
			vec3 lit = vec3(0.0);
			if (u_HasSun == 1)
			{
				vec3 L = normalize(-u_SunDirection);
				float NdotL = max(dot(N, L), 0.0);
				float shadow = Shadow(v_WorldPos, N, L);
				lit = u_SunColor * NdotL * (1.0 - shadow);
			}

			// Termo ambiente das superfícies: na 1ª passada, um piso
			// constante mínimo (evita superfícies 100% pretas e transição
			// dura na porta); da 2ª em diante, a irradiância do grid da
			// passada anterior — o segundo bounce propriamente dito.
			vec3 ambientTerm = vec3(0.03);
			if (u_HasBounceGrid == 1)
				ambientTerm = BounceIrradiance(v_WorldPos, N) + vec3(0.005);

			vec3 color = albedo * (lit + ambientTerm) + u_Emissive;

			// alpha=1.0 => geometria (céu escreve 0.0)
			FragColor = vec4(color, 1.0);
		}
	)";

	// ── Shader do céu do bake ────────────────────────────────────────────────
	// Triângulo fullscreen desenhado ANTES da geometria (depth write off):
	// reconstrói a direção do olho a partir da inversa da VP e sampleia o
	// cubemap do environment (ou uma cor sólida quando não há HDRI). O céu
	// escreve alpha=0.0 — a projeção usa isso pra medir a visibilidade do
	// céu de cada probe (SH0.a), que atenua o IBL especular em interiores.
	static const char* s_SkyVert = R"(
		#version 460 core
		out vec2 v_NDC;
		void main()
		{
			// Triângulo fullscreen via gl_VertexID — sem VBO
			vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
			v_NDC = pos;
			gl_Position = vec4(pos, 1.0, 1.0);
		}
	)";

	static const char* s_SkyFrag = R"(
		#version 460 core
		out vec4 FragColor;
		in vec2 v_NDC;

		uniform mat4 u_InvVP;
		uniform samplerCube u_Sky;
		uniform int  u_HasSky;
		uniform vec3 u_SkyColor;

		void main()
		{
			vec4 nearP = u_InvVP * vec4(v_NDC, -1.0, 1.0);
			vec4 farP  = u_InvVP * vec4(v_NDC,  1.0, 1.0);
			vec3 dir = normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);

			vec3 sky = (u_HasSky == 1) ? texture(u_Sky, dir).rgb : u_SkyColor;
			FragColor = vec4(sky, 0.0); // alpha=0.0 => céu
		}
	)";

	void OpenGLProbeBakePass::Initialize()
	{
		if (m_Initialized) return;
		try
		{
			m_GeoShader = Shader::Create(s_GeoVert, s_GeoFrag);
			m_SkyShader = Shader::Create(s_SkyVert, s_SkyFrag);
		}
		catch (const std::exception& e)
		{
			AXE_CORE_ERROR("OpenGLProbeBakePass shader error: {}", e.what());
			return;
		}

		// Render target 32x32 RGBA16F + depth — reusado pra todas as faces
		glCreateTextures(GL_TEXTURE_2D, 1, &m_ColorTex);
		glTextureStorage2D(m_ColorTex, 1, GL_RGBA16F, kFaceRes, kFaceRes);

		glCreateRenderbuffers(1, &m_DepthRBO);
		glNamedRenderbufferStorage(m_DepthRBO, GL_DEPTH_COMPONENT24, kFaceRes, kFaceRes);

		glCreateFramebuffers(1, &m_FBO);
		glNamedFramebufferTexture(m_FBO, GL_COLOR_ATTACHMENT0, m_ColorTex, 0);
		glNamedFramebufferRenderbuffer(m_FBO, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_DepthRBO);

		if (glCheckNamedFramebufferStatus(m_FBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			AXE_CORE_ERROR("OpenGLProbeBakePass: FBO incompleto!");
			return;
		}

		// VAO vazio pro triângulo fullscreen (posições via gl_VertexID)
		glCreateVertexArrays(1, &m_SkyVAO);

		// Sombra própria do bake — 2048 dá de sobra pra um volume local
		m_ShadowPass = ShadowMapPass::Create();
		m_ShadowPass->Initialize(2048);

		m_Initialized = true;
	}

	void OpenGLProbeBakePass::RenderFace(const RenderQueue& queue,
		const SceneEnvironment* environment,
		const glm::vec3& probePos,
		const glm::vec3& fwd, const glm::vec3& up,
		float farClip,
		uint32_t shadowMapID, const glm::mat4& lightSpaceMatrix,
		const BounceSource* bounce)
	{
		// Casca: binda o FBO 32x32 do bake de GI e delega o desenho ao
		// miolo compartilhado com o CaptureReflection.
		glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
		glViewport(0, 0, kFaceRes, kFaceRes);
		DrawSceneFromPoint(queue, environment, probePos, fwd, up,
			farClip, shadowMapID, lightSpaceMatrix, bounce);
	}

	void OpenGLProbeBakePass::DrawSceneFromPoint(const RenderQueue& queue,
		const SceneEnvironment* environment,
		const glm::vec3& viewPos,
		const glm::vec3& fwd, const glm::vec3& up,
		float farClip,
		uint32_t shadowMapID, const glm::mat4& lightSpaceMatrix,
		const BounceSource* bounce)
	{
		const glm::vec3& probePos = viewPos; // corpo original usa este nome
		glm::mat4 view = glm::lookAt(probePos, probePos + fwd, up);
		glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.05f, farClip);
		glm::mat4 vp = proj * view;
		glm::mat4 invVP = glm::inverse(vp);

		glClearColor(0.f, 0.f, 0.f, 0.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// ── Céu primeiro (depth write off, passa sempre) ────────────────────
		glDepthMask(GL_FALSE);
		glDisable(GL_DEPTH_TEST);
		m_SkyShader->Bind();
		m_SkyShader->SetMat4("u_InvVP", glm::value_ptr(invVP));
		if (environment && environment->HasSkybox())
		{
			environment->Skybox->Bind(0);
			m_SkyShader->SetInt("u_Sky", 0);
			m_SkyShader->SetInt("u_HasSky", 1);
		}
		else
		{
			m_SkyShader->SetInt("u_HasSky", 0);
			// Fallback: azul-céu neutro modulado pela cor do sol, pra não
			// bakear um mundo de breu quando não há HDRI carregado.
			glm::vec3 skyCol = queue.Light
				? queue.Light->Color * (0.4f * queue.Light->Intensity)
				: glm::vec3(0.4f, 0.5f, 0.7f);
			m_SkyShader->SetFloat3("u_SkyColor", skyCol);
		}
		glBindVertexArray(m_SkyVAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);

		// ── Geometria ───────────────────────────────────────────────────────
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);

		// Culling DESLIGADO de propósito: com culling, uma probe enterrada
		// numa parede enxergava ATRAVÉS dela (backfaces descartadas) e
		// capturava o céu do lado de fora — probe enterrada ficava CLARA,
		// com skyVis alto, vazando luz pra dentro da sala e furando a
		// oclusão do sol. Sem culling, ela enxerga a backface, que o
		// shader marca com alpha=-1 pra detecção de probe inválida.
		glDisable(GL_CULL_FACE);

		m_GeoShader->Bind();
		m_GeoShader->SetMat4("u_VP", glm::value_ptr(vp));

		if (queue.Light)
		{
			m_GeoShader->SetInt("u_HasSun", 1);
			m_GeoShader->SetFloat3("u_SunDirection", queue.Light->Direction);
			m_GeoShader->SetFloat3("u_SunColor",
				queue.Light->Color * queue.Light->Intensity * queue.Light->LightMaterialResult);
		}
		else m_GeoShader->SetInt("u_HasSun", 0);

		if (shadowMapID != 0)
		{
			glBindTextureUnit(2, shadowMapID);
			m_GeoShader->SetInt("u_ShadowMap", 2);
			m_GeoShader->SetInt("u_HasShadowMap", 1);
			m_GeoShader->SetMat4("u_LightSpaceMatrix", glm::value_ptr(lightSpaceMatrix));
		}
		else m_GeoShader->SetInt("u_HasShadowMap", 0);

		// Bounce — grid SH da passada anterior nos units 3-6 (0=céu,
		// 1=albedo, 2=sombra já ocupados)
		if (bounce && bounce->SH0)
		{
			bounce->SH0->Bind(3);
			bounce->SH1X->Bind(4);
			bounce->SH1Y->Bind(5);
			bounce->SH1Z->Bind(6);
			m_GeoShader->SetInt("u_SH0", 3);
			m_GeoShader->SetInt("u_SH1X", 4);
			m_GeoShader->SetInt("u_SH1Y", 5);
			m_GeoShader->SetInt("u_SH1Z", 6);
			m_GeoShader->SetMat4("u_GridWorldToLocal", glm::value_ptr(bounce->WorldToLocal));
			m_GeoShader->SetFloat3("u_GridHalfExtents", bounce->HalfExtents);
			m_GeoShader->SetInt("u_HasBounceGrid", 1);
		}
		else m_GeoShader->SetInt("u_HasBounceGrid", 0);

		for (const auto& dc : queue.Meshes)
		{
			if (!dc.Mesh) continue;
			// Transparentes não entram no bake — vidro não bloqueia luz
			if (dc.Material && dc.Material->IsTransparent) continue;

			glm::vec3 albedoColor(0.7f);
			glm::vec3 emissive(0.0f);
			bool hasMap = false;
			if (dc.Material)
			{
				albedoColor = glm::vec3(dc.Material->Color);
				emissive = dc.Material->BakedEmissive;
				if (dc.Material->HasAlbedoMap())
				{
					dc.Material->AlbedoMap->Bind(1);
					m_GeoShader->SetInt("u_AlbedoMap", 1);
					hasMap = true;
				}
			}
			m_GeoShader->SetFloat3("u_AlbedoColor", albedoColor);
			m_GeoShader->SetFloat3("u_Emissive", emissive);
			m_GeoShader->SetInt("u_HasAlbedoMap", hasMap ? 1 : 0);

			m_GeoShader->SetMat4("u_Model", glm::value_ptr(dc.Transform));
			dc.Mesh->GetVertexArray()->Bind();
			glDrawElements(GL_TRIANGLES, dc.Mesh->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
		}
	}

	uint32_t OpenGLProbeBakePass::RenderSunShadow(const RenderQueue& queue,
		const glm::vec3& center, float radius, glm::mat4& outLsm)
	{
		if (!queue.Light || !queue.Light->CastShadows) return 0;

		outLsm = ShadowMapPass::CalcLightSpaceMatrix(
			queue.Light->Direction, radius, center);

		m_ShadowPass->Begin(outLsm);
		for (const auto& dc : queue.Meshes)
			if (dc.Mesh) m_ShadowPass->DrawMesh(*dc.Mesh, dc.Transform);
		m_ShadowPass->End();
		return m_ShadowPass->GetDepthMapID();
	}

	std::shared_ptr<ProbeGrid> OpenGLProbeBakePass::Bake(const RenderQueue& queue,
		const SceneEnvironment* environment,
		const ProbeBakeRequest& request)
	{
		if (!m_Initialized)
		{
			AXE_CORE_ERROR("ProbeBakePass: não inicializado!");
			return nullptr;
		}

		glm::ivec3 res = glm::clamp(request.Resolution, glm::ivec3(2), glm::ivec3(16));
		const int total = res.x * res.y * res.z;
		AXE_CORE_INFO("ProbeBake: {}x{}x{} = {} probes, {} bounce(s) ({} renders)...",
			res.x, res.y, res.z, total, glm::clamp(request.Bounces, 1, 4),
			total * 6 * glm::clamp(request.Bounces, 1, 4));

		// Salva o viewport atual — RenderFace bind um FBO 32x32 e chama
		// glViewport pra ele; sem restaurar, o próximo desenho no
		// framebuffer PADRÃO (tela/ImGui) herdaria esse viewport minúsculo
		// até que algum outro Framebuffer::Bind() por acaso o corrigisse.
		// Não é a causa do bug do thumbnail (aquele é forward, FBO
		// próprio), mas é um vazamento de estado real — corrigido por
		// robustez.
		GLint prevViewport[4];
		glGetIntegerv(GL_VIEWPORT, prevViewport);

		// ── Sombra do bake — um shadow map centrado no volume ───────────────
		glm::mat4 lsm(1.0f);
		uint32_t shadowID = RenderSunShadow(queue,
			glm::vec3(request.LocalToWorld[3]),
			glm::length(request.HalfExtents) * 2.0f + 10.0f, lsm);

		// ── 6 faces do mini cubemap (fwd/up padrão) ─────────────────────────
		struct Face { glm::vec3 Fwd, Up; };
		const Face faces[6] = {
			{ { 1, 0, 0}, {0, 1, 0} }, { {-1, 0, 0}, {0, 1, 0} },
			{ { 0, 1, 0}, {0, 0, 1} }, { { 0,-1, 0}, {0, 0,-1} },
			{ { 0, 0, 1}, {0, 1, 0} }, { { 0, 0,-1}, {0, 1, 0} },
		};

		// Buffers CPU dos coeficientes SH (RGBA por probe)
		std::vector<float> sh0(total * 4, 0.f), sh1x(total * 4, 0.f),
			sh1y(total * 4, 0.f), sh1z(total * 4, 0.f);
		std::vector<float> pixels(kFaceRes * kFaceRes * 4);

		// Validade por probe — probe "enterrada" (dentro de parede/chão)
		// é detectada pela fração do ângulo sólido ocupada por BACKFACES
		// (alpha=-1 vindo do shader). Um pouco de backface é normal perto
		// de geometria fina/one-sided; acima do limiar, a probe está
		// dentro de algo e seus coeficientes seriam veneno na interpolação
		// trilinear (mancha escura vazando pela parede).
		std::vector<uint8_t> valid(total, 1);
		const float kBuriedBackfaceFrac = 0.25f;

		// Constantes da base SH L1
		const float Y00 = 0.282095f;
		const float Y1 = 0.488603f;

		glm::vec3 he = request.HalfExtents;
		glm::vec3 step = 2.0f * he / glm::vec3(res); // célula por probe

		// ── Correção de probes enterradas (por passada!) ────────────────────
		// Cada probe inválida herda os 4 coeficientes RGBA (SH + skyVis) da
		// probe VÁLIDA mais próxima no grid. Corrigir aqui (em vez de pesos
		// de validade no shader) mantém a interpolação trilinear do hardware
		// intocada. Roda ao fim de CADA passada: o grid corrigido é a fonte
		// de luz da passada seguinte — sem isso, probes enterradas
		// envenenariam o segundo bounce. Busca O(n²) no pior caso, mas
		// n ≤ 4096 e só roda no bake offline.
		auto FixBuriedProbes = [&]()
			{
				int buried = 0;
				for (int i = 0; i < total; i++) if (!valid[i]) buried++;

				if (buried > 0 && buried < total)
				{
					auto coordOf = [&](int i) -> glm::ivec3 {
						int xy = res.x * res.y;
						return { i % res.x, (i / res.x) % res.y, i / xy };
						};

					for (int i = 0; i < total; i++)
					{
						if (valid[i]) continue;
						glm::ivec3 ci = coordOf(i);

						int best = -1;
						int bestD = INT_MAX;
						for (int j = 0; j < total; j++)
						{
							if (!valid[j]) continue;
							glm::ivec3 d = coordOf(j) - ci;
							int dist = d.x * d.x + d.y * d.y + d.z * d.z;
							if (dist < bestD) { bestD = dist; best = j; }
						}

						if (best >= 0)
							for (int c = 0; c < 4; c++)
							{
								sh0[i * 4 + c] = sh0[best * 4 + c];
								sh1x[i * 4 + c] = sh1x[best * 4 + c];
								sh1y[i * 4 + c] = sh1y[best * 4 + c];
								sh1z[i * 4 + c] = sh1z[best * 4 + c];
							}
					}
					AXE_CORE_INFO("ProbeBake: {} probe(s) enterrada(s) corrigida(s) "
						"(coeficientes copiados da vizinha valida mais proxima).", buried);
				}
				else if (buried == total)
				{
					AXE_CORE_WARN("ProbeBake: TODAS as {} probes estao dentro de "
						"geometria — o volume provavelmente esta mal posicionado. "
						"Nada foi corrigido.", total);
				}
			};

		// ── Loop de passadas (multi-bounce) ─────────────────────────────────
		// Passada 1: sol direto + piso constante mínimo → 1 bounce.
		// Passada N: idem, mas as superfícies usam o grid da passada N-1
		// como ambiente → luz que "dobra a esquina" (sol → parede → chão →
		// probe). O grid intermediário sobe pra GPU como Texture3D
		// temporário entre as passadas; os shared_ptr locais mantêm as
		// texturas vivas enquanto o BounceSource (ponteiros crus) as usa.
		const int bounces = glm::clamp(request.Bounces, 1, 4);
		glm::mat4 gridWorldToLocal = glm::inverse(request.LocalToWorld);

		BounceSource bounce{};
		std::shared_ptr<Texture3D> bSH0, bSH1X, bSH1Y, bSH1Z;

		for (int pass = 0; pass < bounces; pass++)
		{
			std::fill(sh0.begin(), sh0.end(), 0.f);
			std::fill(sh1x.begin(), sh1x.end(), 0.f);
			std::fill(sh1y.begin(), sh1y.end(), 0.f);
			std::fill(sh1z.begin(), sh1z.end(), 0.f);
			std::fill(valid.begin(), valid.end(), (uint8_t)1);

			const BounceSource* bounceSrc = (pass > 0) ? &bounce : nullptr;

			for (int z = 0; z < res.z; z++)
				for (int y = 0; y < res.y; y++)
					for (int x = 0; x < res.x; x++)
					{
						// Probe no CENTRO da célula — evita probes coladas na
						// parede da caixa (que enxergariam metade dentro da
						// geometria vizinha).
						glm::vec3 local = -he + (glm::vec3(x, y, z) + 0.5f) * step;
						glm::vec3 pos = glm::vec3(request.LocalToWorld * glm::vec4(local, 1.0f));

						glm::vec3 L00(0.f), L1x(0.f), L1y(0.f), L1z(0.f);
						float skySolid = 0.f, totalSolid = 0.f, backSolid = 0.f;

						for (int f = 0; f < 6; f++)
						{
							RenderFace(queue, environment, pos,
								faces[f].Fwd, faces[f].Up,
								request.FarClip, shadowID, lsm, bounceSrc);

							glReadPixels(0, 0, kFaceRes, kFaceRes,
								GL_RGBA, GL_FLOAT, pixels.data());

							// Base ortonormal da face — a MESMA usada no
							// lookAt/perspective, então direção do texel =
							// fwd + u*right + v*upv (fov 90°, tan(45°)=1).
							glm::vec3 fwd = faces[f].Fwd;
							glm::vec3 right = glm::normalize(glm::cross(fwd, faces[f].Up));
							glm::vec3 upv = glm::cross(right, fwd);

							for (int j = 0; j < kFaceRes; j++)
								for (int i = 0; i < kFaceRes; i++)
								{
									float u = 2.f * (i + 0.5f) / kFaceRes - 1.f;
									float v = 2.f * (j + 0.5f) / kFaceRes - 1.f;

									// Ângulo sólido do texel do cubemap
									float lenSq = u * u + v * v + 1.f;
									float dw = 4.f / (kFaceRes * kFaceRes)
										/ (lenSq * std::sqrt(lenSq));

									glm::vec3 dir = glm::normalize(fwd + u * right + v * upv);

									const float* px = &pixels[(j * kFaceRes + i) * 4];

									// Backface (alpha=-1): não é luz real — não
									// entra na SH e conta como OCLUSÃO (não céu)
									// na visibilidade. Sem este desvio, o -1
									// passaria no teste "< 0.5" e backface
									// contaria como CÉU, inflando o skyVis de
									// probes enterradas e furando a oclusão
									// do sol.
									if (px[3] < -0.5f)
									{
										backSolid += dw;
										totalSolid += dw;
										continue;
									}

									glm::vec3 radiance(px[0], px[1], px[2]);

									// Projeção nos 4 coeficientes SH L1
									L00 += radiance * (Y00 * dw);
									L1x += radiance * (Y1 * dir.x * dw);
									L1y += radiance * (Y1 * dir.y * dw);
									L1z += radiance * (Y1 * dir.z * dw);

									totalSolid += dw;
									if (px[3] < 0.5f) skySolid += dw; // alpha=0 => céu
								}
						}

						float skyVis = totalSolid > 0.f ? skySolid / totalSolid : 1.f;

						// Probe enterrada — marca inválida; os coeficientes
						// serão substituídos pelos da vizinha válida mais
						// próxima no FixBuriedProbes desta passada.
						float backFrac = totalSolid > 0.f ? backSolid / totalSolid : 0.f;
						int probeIdx = z * res.y * res.x + y * res.x + x;
						if (backFrac > kBuriedBackfaceFrac)
							valid[probeIdx] = 0;

						int idx = probeIdx * 4;
						sh0[idx + 0] = L00.r; sh0[idx + 1] = L00.g; sh0[idx + 2] = L00.b;
						sh0[idx + 3] = skyVis;
						sh1x[idx + 0] = L1x.r; sh1x[idx + 1] = L1x.g; sh1x[idx + 2] = L1x.b; sh1x[idx + 3] = 0.f;
						sh1y[idx + 0] = L1y.r; sh1y[idx + 1] = L1y.g; sh1y[idx + 2] = L1y.b; sh1y[idx + 3] = 0.f;
						sh1z[idx + 0] = L1z.r; sh1z[idx + 1] = L1z.g; sh1z[idx + 2] = L1z.b; sh1z[idx + 3] = 0.f;
					}

			FixBuriedProbes();

			// Grid intermediário vira a fonte de luz da próxima passada
			if (pass + 1 < bounces)
			{
				bSH0 = Texture3D::CreateRGBA16F(res.x, res.y, res.z, sh0.data());
				bSH1X = Texture3D::CreateRGBA16F(res.x, res.y, res.z, sh1x.data());
				bSH1Y = Texture3D::CreateRGBA16F(res.x, res.y, res.z, sh1y.data());
				bSH1Z = Texture3D::CreateRGBA16F(res.x, res.y, res.z, sh1z.data());
				bounce.SH0 = bSH0.get();
				bounce.SH1X = bSH1X.get();
				bounce.SH1Y = bSH1Y.get();
				bounce.SH1Z = bSH1Z.get();
				bounce.WorldToLocal = gridWorldToLocal;
				bounce.HalfExtents = he;
				AXE_CORE_INFO("ProbeBake: passada {}/{} concluida.", pass + 1, bounces);
			}
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

		auto grid = std::make_shared<ProbeGrid>();
		grid->Resolution = res;
		grid->SH0 = Texture3D::CreateRGBA16F(res.x, res.y, res.z, sh0.data());
		grid->SH1X = Texture3D::CreateRGBA16F(res.x, res.y, res.z, sh1x.data());
		grid->SH1Y = Texture3D::CreateRGBA16F(res.x, res.y, res.z, sh1y.data());
		grid->SH1Z = Texture3D::CreateRGBA16F(res.x, res.y, res.z, sh1z.data());

		// Guarda a cópia CPU no grid (move — os buffers já cumpriram o
		// papel de staging): é o que o SceneSerializer usa pra salvar o
		// .axeprobes ao lado da cena, sem readback da GPU.
		grid->DataSH0 = std::move(sh0);
		grid->DataSH1X = std::move(sh1x);
		grid->DataSH1Y = std::move(sh1y);
		grid->DataSH1Z = std::move(sh1z);

		AXE_CORE_INFO("ProbeBake: concluido ({} probes, {} bounce(s)).", total, bounces);
		return grid;
	}

	// ── Reflection Probe: captura + prefilter GGX ────────────────────────

	OpenGLProbeBakePass::OpenGLReflectionCapture::~OpenGLReflectionCapture()
	{
		if (m_PrefilteredID) glDeleteTextures(1, &m_PrefilteredID);
	}

	// Shader de prefilter GGX (LearnOpenGL clássico): amostra o cubemap
	// de captura com importance sampling da NDF e escreve um mip por
	// nível de roughness. Compilado sob demanda na primeira captura.
	static const char* kPrefilterVS = R"(
#version 460 core
layout(location = 0) in vec3 a_Pos;
out vec3 v_LocalPos;
uniform mat4 u_View;
uniform mat4 u_Proj;
void main() { v_LocalPos = a_Pos; gl_Position = u_Proj * u_View * vec4(a_Pos, 1.0); }
)";

	static const char* kPrefilterFS = R"(
#version 460 core
in vec3 v_LocalPos;
layout(location = 0) out vec4 FragColor;
uniform samplerCube u_EnvMap;
uniform float u_Roughness;

const float PI = 3.14159265359;

float RadicalInverse_VdC(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}
vec2 Hammersley(uint i, uint N) { return vec2(float(i) / float(N), RadicalInverse_VdC(i)); }

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
	float a = roughness * roughness;
	float phi = 2.0 * PI * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
	vec3 up = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
	vec3 tangent = normalize(cross(up, N));
	vec3 bitangent = cross(N, tangent);
	return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

void main()
{
	vec3 N = normalize(v_LocalPos);
	vec3 R = N, V = N;

	const uint SAMPLE_COUNT = 256u; // captura local 128 — 256 amostras bastam
	float totalWeight = 0.0;
	vec3 color = vec3(0.0);
	for (uint i = 0u; i < SAMPLE_COUNT; ++i)
	{
		vec2 Xi = Hammersley(i, SAMPLE_COUNT);
		vec3 H = ImportanceSampleGGX(Xi, N, u_Roughness);
		vec3 L = normalize(2.0 * dot(V, H) * H - V);
		float NdotL = max(dot(N, L), 0.0);
		if (NdotL > 0.0) { color += texture(u_EnvMap, L).rgb * NdotL; totalWeight += NdotL; }
	}
	FragColor = vec4(color / max(totalWeight, 0.0001), 1.0);
}
)";

	std::shared_ptr<ReflectionCapture> OpenGLProbeBakePass::CaptureReflection(
		const RenderQueue& queue,
		const SceneEnvironment* environment,
		const ReflectionBakeRequest& request,
		const ProbeVolumeData* giVolume)
	{
		if (!IsInitialized())
		{
			AXE_CORE_WARN("ReflectionProbe: bake pass nao inicializado.");
			return nullptr;
		}

		const int res = glm::clamp(request.Resolution, 32, 512);
		AXE_CORE_INFO("ReflectionProbe: capturando {}x{} em ({:.1f},{:.1f},{:.1f})...",
			res, res, request.Position.x, request.Position.y, request.Position.z);

		GLint prevViewport[4];
		glGetIntegerv(GL_VIEWPORT, prevViewport);

		// Sombra do sol — mesma preparação do bake de GI, centrada na probe
		glm::mat4 lsm(1.0f);
		uint32_t shadowID = RenderSunShadow(queue, request.Position,
			glm::max(request.FarClip, 50.0f), lsm);

		// GI da cena como ambiente das superfícies capturadas — o reflexo
		// fica consistente com a iluminação bakeada (paredes refletidas com
		// o mesmo bounce que têm na tela)
		BounceSource bounce{};
		const BounceSource* bounceSrc = nullptr;
		if (giVolume && giVolume->Grid && giVolume->Grid->IsValid())
		{
			bounce.SH0 = giVolume->Grid->SH0.get();
			bounce.SH1X = giVolume->Grid->SH1X.get();
			bounce.SH1Y = giVolume->Grid->SH1Y.get();
			bounce.SH1Z = giVolume->Grid->SH1Z.get();
			bounce.WorldToLocal = giVolume->WorldToLocal;
			bounce.HalfExtents = giVolume->HalfExtents;
			bounceSrc = &bounce;
		}

		// ── 1. Cubemap de captura HDR ────────────────────────────────────
		uint32_t captureCube = 0;
		glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &captureCube);
		glBindTexture(GL_TEXTURE_CUBE_MAP, captureCube);
		for (int f = 0; f < 6; f++)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA16F,
				res, res, 0, GL_RGBA, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		// RBO de depth no tamanho da captura
		uint32_t rbo = 0;
		glCreateRenderbuffers(1, &rbo);
		glNamedRenderbufferStorage(rbo, GL_DEPTH_COMPONENT24, res, res);

		uint32_t fbo = 0;
		glCreateFramebuffers(1, &fbo);
		glNamedFramebufferRenderbuffer(fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

		// Mesmas 6 faces do bake de GI (fwd/up padrão de cubemap)
		struct Face { glm::vec3 Fwd, Up; };
		const Face faces[6] = {
			{ { 1, 0, 0}, {0,-1, 0} }, { {-1, 0, 0}, {0,-1, 0} },
			{ { 0, 1, 0}, {0, 0, 1} }, { { 0,-1, 0}, {0, 0,-1} },
			{ { 0, 0, 1}, {0,-1, 0} }, { { 0, 0,-1}, {0,-1, 0} },
		};

		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		for (int f = 0; f < 6; f++)
		{
			glNamedFramebufferTextureLayer(fbo, GL_COLOR_ATTACHMENT0, captureCube, 0, f);
			glViewport(0, 0, res, res);
			DrawSceneFromPoint(queue, environment, request.Position,
				faces[f].Fwd, faces[f].Up, request.FarClip, shadowID, lsm, bounceSrc);
		}
		glGenerateTextureMipmap(captureCube);

		// ── 2. Prefilter GGX — mips por roughness ────────────────────────
		if (!m_PrefilterShader)
			m_PrefilterShader = Shader::Create(kPrefilterVS, kPrefilterFS);

		constexpr int kPrefRes = 128;
		constexpr int kMips = 5;
		uint32_t prefiltered = 0;
		glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &prefiltered);
		glBindTexture(GL_TEXTURE_CUBE_MAP, prefiltered);
		for (int f = 0; f < 6; f++)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA16F,
				kPrefRes, kPrefRes, 0, GL_RGBA, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP); // aloca a cadeia de mips

		glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

		m_PrefilterShader->Bind();
		m_PrefilterShader->SetInt("u_EnvMap", 0);
		glBindTextureUnit(0, captureCube);
		m_PrefilterShader->SetMat4("u_Proj", glm::value_ptr(proj));

		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glBindVertexArray(m_SkyVAO); // cubo unitário do bake — reuso

		for (int mip = 0; mip < kMips; mip++)
		{
			int mipRes = kPrefRes >> mip;
			glViewport(0, 0, mipRes, mipRes);
			float roughness = (float)mip / (float)(kMips - 1);
			m_PrefilterShader->SetFloat("u_Roughness", roughness);

			for (int f = 0; f < 6; f++)
			{
				glm::mat4 view = glm::lookAt(glm::vec3(0.0f),
					faces[f].Fwd, faces[f].Up);
				m_PrefilterShader->SetMat4("u_View", glm::value_ptr(view));
				glNamedFramebufferTextureLayer(fbo, GL_COLOR_ATTACHMENT0,
					prefiltered, mip, f);
				glClear(GL_COLOR_BUFFER_BIT);
				glDrawArrays(GL_TRIANGLES, 0, 36);
			}
		}
		glEnable(GL_DEPTH_TEST);

		// ── 3. Limpeza — só o prefiltered sobrevive ──────────────────────
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &fbo);
		glDeleteRenderbuffers(1, &rbo);
		glDeleteTextures(1, &captureCube);
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

		AXE_CORE_INFO("ReflectionProbe: captura concluida ({} mips GGX).", kMips);
		return std::make_shared<OpenGLReflectionCapture>(prefiltered);
	}
}