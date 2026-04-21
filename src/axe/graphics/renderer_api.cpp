#include "renderer_api.hpp"
#include "axe/graphics/opengl/opengl_renderer_api.hpp"

namespace axe
{
	RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;

	std::unique_ptr<RendererAPI> RendererAPI::Create()
	{
		switch (s_API)
		{
		case API::OpenGL:
			return std::make_unique<OpenGLRendererAPI>();

		case API::None:			
		default:
			break;
		}
	}

}