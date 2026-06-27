#include "axe/graphics/opengl/opengl_pipeline.hpp"
#include "axe/graphics/shader.hpp"

#include <glad/glad.h>

namespace axe
{
	OpenGLPipeline::OpenGLPipeline(const PipelineSpecification& spec)
		: m_Spec(spec)
	{

	}

	void OpenGLPipeline::Bind() const
	{
		// Shader é OPCIONAL: um Pipeline pode representar só estado
		// fixed-function (cull/blend/depth) quando o passe binda os
		// shaders por conta própria — ex.: o GeometryPass, que troca de
		// shader por malha (material graph vs. shader fixo).
		if (m_Spec.Shader)
			m_Spec.Shader->Bind();

		//Depth Test
		if (m_Spec.DepthTest)
			glEnable(GL_DEPTH_TEST);
		else
			glDisable(GL_DEPTH_TEST);

		//Depth Write
		glDepthMask(m_Spec.DepthWrite ? GL_TRUE : GL_FALSE);

		//Depth Function
		switch (m_Spec.DepthFunc)
		{
		case RendererAPI::DepthFunc::Less:      glDepthFunc(GL_LESS);   break;
		case RendererAPI::DepthFunc::LessEqual: glDepthFunc(GL_LEQUAL); break;
		case RendererAPI::DepthFunc::Always:    glDepthFunc(GL_ALWAYS); break;
		}

		//Blending
		if (m_Spec.Blend)
		{
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
		else
		{
			glDisable(GL_BLEND);
		}

		//Face Culling
		switch (m_Spec.Cull)
		{
		case CullMode::Back:
			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			break;

		case CullMode::Front:
			glEnable(GL_CULL_FACE);
			glCullFace(GL_FRONT);
			break;

		case CullMode::None:
			glDisable(GL_CULL_FACE);
			break;
		}
	}
}