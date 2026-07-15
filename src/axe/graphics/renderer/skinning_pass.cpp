#include "axe/graphics/renderer/skinning_pass.hpp"
#include "axe/graphics/opengl/opengl_skinning_pass.hpp"

namespace axe
{
	std::shared_ptr<SkinningPass> SkinningPass::Create()
	{
		return std::make_shared<OpenGLSkinningPass>();
	}

} // namespace axe