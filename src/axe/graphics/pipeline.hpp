#pragma once

#include "axe/core/types.hpp"
#include "axe/graphics/renderer_api.hpp" // RendererAPI::DepthFunc
#include <memory>

namespace axe
{
	class Shader;

	enum class CullMode
	{
		None = 0,
		Back,
		Front
	};
	struct PipelineSpecification
	{
		std::shared_ptr<Shader> Shader;
		bool DepthTest = true;
		bool DepthWrite = true;

		// Função de comparação de profundidade. Agora faz parte do "PSO":
		// o passe não precisa mais chamar SetDepthFunc à mão.
		RendererAPI::DepthFunc DepthFunc = RendererAPI::DepthFunc::Less;

		bool Blend = false;

		CullMode Cull = CullMode::Back;
	};

	class AXE_API Pipeline
	{
	public:
		virtual ~Pipeline() = default;

		virtual void Bind() const = 0;
		//virtual void Unbind() const = 0;

		static std::shared_ptr<Pipeline> Create(const PipelineSpecification& spec);
	};
}