#include "point_shadow_pass.hpp"
#include "axe/graphics/opengl/opengl_point_shadow_pass.hpp"

namespace axe
{
	std::shared_ptr<PointShadowPass> PointShadowPass::Create()
	{
		return std::make_shared<OpenGLPointShadowPass>();
	}
}