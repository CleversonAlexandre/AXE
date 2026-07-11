#include "probe_bake_pass.hpp"
#include "axe/graphics/opengl/opengl_probe_bake_pass.hpp"

namespace axe
{
	std::shared_ptr<ProbeBakePass> ProbeBakePass::Create()
	{
		return std::make_shared<OpenGLProbeBakePass>();
	}
}