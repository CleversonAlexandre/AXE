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
		//Shader
		m_Spec.Shader->Bind();

		//Depth Test
		if (m_Spec.DepthTest)
			glEnable(GL_DEPTH_TEST);
		else
			glDisable(GL_DEPTH_TEST);

		//Depth Write
		glDepthMask(m_Spec.DepthWrite ? GL_TRUE: GL_FALSE);

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