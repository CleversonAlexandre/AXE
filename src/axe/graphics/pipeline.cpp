#include "axe/graphics/pipeline.hpp"
#include "axe/graphics/opengl/opengl_pipeline.hpp"

namespace axe
{
	std::shared_ptr<Pipeline> Pipeline::Create(const PipelineSpecification& spec)
	{
		return std::make_shared<OpenGLPipeline>(spec);
	}
}