#pragma once

#include "axe/core/types.hpp"
#include "axe/graphics/pipeline.hpp"

namespace axe
{
	class AXE_API OpenGLPipeline final : public Pipeline
	{
	public:
		OpenGLPipeline(const PipelineSpecification& spec);

		void Bind() const override;
		//void Unbind() const override;

	private:
		PipelineSpecification m_Spec;
	};
}

