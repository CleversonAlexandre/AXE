#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/opengl/opengl_framebuffer.hpp"

namespace axe
{
	std::shared_ptr<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec)
	{
		return std::make_shared<OpenGLFramebuffer>(spec);
	}
}