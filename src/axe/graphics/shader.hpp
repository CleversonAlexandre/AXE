#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <string>
#include <glm/glm.hpp>

namespace axe
{
	class AXE_API  Shader
	{
	public:
		virtual ~Shader() = default;
		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual void SetFloat4(const std::string& name, float x, float y, float z, float w) = 0;
		virtual void SetMat4(const std::string& name, const float* valeu) = 0;

		virtual void SetInt(const std::string& name, int value) = 0;
		virtual void SetUint(const std::string& name, std::uint32_t value) = 0;
		virtual void SetFloat4(const std::string& name, const glm::vec4& value) = 0;

		static std::shared_ptr<Shader> Create(const std::string& vertexSource,
			const std::string& fragmentSource);
		virtual void SetFloat(const std::string& name, float value) = 0;
		virtual void SetFloat3(const std::string& name, const glm::vec3& value) = 0;

		// POSTPROCESS_DOMAIN_V1 — lacuna da interface: SetFloat3 e SetFloat4 ja
		// existiam, vec2 nao. O material de post process precisa do tamanho da
		// tela como vec2 (e um efeito de pixelizacao ou de scanline nao existe
		// sem ele). Contornar com um vec3 de z inutil faria glUniform3f numa
		// uniform vec2 — que falha em SILENCIO, sem erro e sem valor.
		virtual void SetFloat2(const std::string& name, const glm::vec2& value) = 0;

		virtual void SetMat3(const std::string& name, const float* value) = 0;
		virtual void SetBool(const std::string& name, bool value) = 0;
	};
}