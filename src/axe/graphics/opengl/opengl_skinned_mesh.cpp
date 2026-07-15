#include "axe/graphics/opengl/opengl_skinning_pass.hpp"
#include "axe/graphics/compute_shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/animation/skinned_mesh.hpp"
#include "axe/log/log.hpp"

namespace axe
{
	namespace
	{
		// local_size_x = 64: múltiplo do wavefront de 64 da GCN (RX 580).
		// Em NVIDIA (warp 32) 64 também é ótimo — 2 warps por grupo.
		constexpr std::uint32_t kLocalSizeX = 64;

		// IMPORTANTE — os structs abaixo têm que bater BYTE A BYTE com
		// SkinnedVertex e Vertex do lado C++. Por isso são declarados como
		// escalares soltos (float/int) em vez de vec3/vec2: em std430, um
		// vec3 tem alinhamento de 16 bytes, o que inseriria PADDING que o
		// C++ não tem — e todo vértice a partir do primeiro leria lixo.
		//
		// SkinnedVertex = 14 floats + 4 ints + 4 floats = 88 bytes
		// Vertex        = 14 floats                     = 56 bytes
		//
		// Se um dia alguém mexer no SkinnedVertex e esquecer daqui, o
		// static_assert em skinned_mesh_instance.cpp grita no build.
		const char* kSkinningSource = R"(
#version 450 core

layout(local_size_x = 64) in;

struct SkinnedVertexIn
{
	float px, py, pz;
	float nx, ny, nz;
	float u,  v;
	float tx, ty, tz;
	float bx, by, bz;
	int   b0, b1, b2, b3;
	float w0, w1, w2, w3;
};

struct StaticVertexOut
{
	float px, py, pz;
	float nx, ny, nz;
	float u,  v;
	float tx, ty, tz;
	float bx, by, bz;
};

layout(std430, binding = 0) readonly  buffer InBuf   { SkinnedVertexIn  i_Verts[]; };
layout(std430, binding = 1) writeonly buffer OutBuf  { StaticVertexOut  o_Verts[]; };

// Palette num SSBO, NÃO num array de uniforms. Consequência boa: o limite
// de 128 bones (AXE_MAX_BONES) some — um SSBO comporta milhares. Rigs
// pesados de rosto/mão passam a caber sem tocar em nada.
layout(std430, binding = 2) readonly  buffer BoneBuf { mat4 i_Bones[]; };

uniform uint u_VertexCount;

void main()
{
	uint id = gl_GlobalInvocationID.x;

	// O último work group quase sempre sobra do fim do array — sem esta
	// guarda, ele escreve fora do buffer.
	if (id >= u_VertexCount)
		return;

	SkinnedVertexIn v = i_Verts[id];

	vec4 pos    = vec4(v.px, v.py, v.pz, 1.0);
	vec3 normal = vec3(v.nx, v.ny, v.nz);
	vec3 tang   = vec3(v.tx, v.ty, v.tz);
	vec3 bitan  = vec3(v.bx, v.by, v.bz);

	int   ids[4]     = int[4](v.b0, v.b1, v.b2, v.b3);
	float weights[4] = float[4](v.w0, v.w1, v.w2, v.w3);

	vec4 skinnedPos    = vec4(0.0);
	vec3 skinnedNormal = vec3(0.0);
	vec3 skinnedTang   = vec3(0.0);
	vec3 skinnedBitan  = vec3(0.0);

	for (int k = 0; k < 4; ++k)
	{
		int   b = ids[k];
		float w = weights[k];

		// -1 = slot vazio (ver SkinnedVertex::BoneIDs). Peso 0 tambem sai.
		if (b < 0 || w <= 0.0)
			continue;

		mat4 m = i_Bones[b];

		skinnedPos += (m * pos) * w;

		// Normais/tangentes usam só a parte 3x3 (sem translação).
		//
		// Nota honesta: o correto matematicamente seria a transposta da
		// inversa da 3x3, pra sobreviver a escala NÃO-UNIFORME nos bones.
		// Inverter 4 matrizes por vértice na GPU é caro demais, e rig com
		// escala não-uniforme é raro (e má prática). Todo mundo — Unreal
		// inclusive — usa a 3x3 direta aqui. Se um dia um rig com squash
		// and stretch deformar as normais errado, a causa é ESTA linha.
		mat3 m3 = mat3(m);

		skinnedNormal += (m3 * normal) * w;
		skinnedTang   += (m3 * tang)   * w;
		skinnedBitan  += (m3 * bitan)  * w;
	}

	// Pesos somam 1.0 (NormalizeWeights no loader), mas a soma PONDERADA de
	// matrizes de rotação não é uma rotação — ela encolhe o vetor nas
	// juntas. Renormalizar aqui é o que evita a iluminação escurecer
	// exatamente nos cotovelos e joelhos.
	skinnedNormal = normalize(skinnedNormal);
	skinnedTang   = normalize(skinnedTang);
	skinnedBitan  = normalize(skinnedBitan);

	StaticVertexOut o;
	o.px = skinnedPos.x;    o.py = skinnedPos.y;    o.pz = skinnedPos.z;
	o.nx = skinnedNormal.x; o.ny = skinnedNormal.y; o.nz = skinnedNormal.z;
	o.u  = v.u;             o.v  = v.v;             // UV não deforma
	o.tx = skinnedTang.x;   o.ty = skinnedTang.y;   o.tz = skinnedTang.z;
	o.bx = skinnedBitan.x;  o.by = skinnedBitan.y;  o.bz = skinnedBitan.z;

	o_Verts[id] = o;
}
)";
	}

	bool OpenGLSkinningPass::Initialize()
	{
		if (m_Shader)
			return true;

		if (!ComputeShader::IsSupported())
		{
			AXE_CORE_ERROR("SkinningPass: GPU/driver sem suporte a compute shader (precisa de GL 4.3+). "
				"Personagens serao desenhados na bind pose.");
			return false;
		}

		m_Shader = ComputeShader::Create(kSkinningSource);

		if (!m_Shader)
		{
			AXE_CORE_ERROR("SkinningPass: falha ao compilar o compute shader de skinning.");
			return false;
		}

		AXE_CORE_INFO("SkinningPass: skin cache inicializado (local_size_x = {}).", kLocalSizeX);
		return true;
	}

	void OpenGLSkinningPass::Execute(const SkinnedMesh& source,
		const VertexBuffer& boneBuffer,
		const VertexBuffer& output,
		std::uint32_t vertexCount)
	{
		if (!m_Shader || vertexCount == 0)
			return;

		m_Shader->Bind();

		// Os bindings batem com os `layout(std430, binding = N)` do GLSL.
		source.GetVertexBuffer()->BindAsStorage(0);
		output.BindAsStorage(1);
		boneBuffer.BindAsStorage(2);

		m_Shader->SetUint("u_VertexCount", vertexCount);

		// Teto de divisão: o último grupo sobra e é cortado pela guarda
		// `if (id >= u_VertexCount)` dentro do shader.
		const std::uint32_t groups = (vertexCount + kLocalSizeX - 1) / kLocalSizeX;

		m_Shader->Dispatch(groups);

		++m_DispatchesThisFrame;
	}

	void OpenGLSkinningPass::Flush()
	{
		if (!m_Shader || m_DispatchesThisFrame == 0)
			return;

		// UMA barreira pra todos os personagens do frame. Se fosse uma por
		// personagem, a GPU teria que terminar cada um antes de começar o
		// próximo — jogando fora justamente o paralelismo que o compute traz.
		m_Shader->BarrierForVertexRead();
		m_Shader->Unbind();

		m_DispatchesThisFrame = 0;
	}

} // namespace axe