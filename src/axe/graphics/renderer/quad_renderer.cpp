#include "axe/graphics/renderer/quad_renderer.hpp"

#include "axe/log/log.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/graphics/pipeline.hpp"
#include "axe/graphics/texture.hpp"

#include <cmath>
#include <array>

namespace axe
{
	QuadRenderer::QuadRenderer()
	{
		const float vertices[] =
		{
			// position              // color
			-0.5f, -0.5f, 0.0f,      1.0f, 0.0f, 0.0f,
			 0.5f, -0.5f, 0.0f,      0.0f, 1.0f, 0.0f,
			 0.5f,  0.5f, 0.0f,      0.0f, 0.0f, 1.0f,
			-0.5f,  0.5f, 0.0f,      1.0f, 1.0f, 0.0f
		};

		const std::uint32_t indices[] =
		{
			0, 1, 2,
			2, 3, 0
		};

		const std::string vertexSrc = R"(
			#version 460 core
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec3 a_Color;

			uniform mat4 u_Model;

			out vec3 v_Color;

			void main()
			{
				v_Color = a_Color;
				gl_Position = u_Model * vec4(a_Position, 1.0);
			}
		)";

		const std::string fragmentSrc = R"(
			#version 460 core
			in vec3 v_Color;
			layout(location = 0) out vec4 color;

			void main()
			{
				color = vec4(v_Color, 1.0);
			}
		)";

		m_Shader = Shader::Create(vertexSrc, fragmentSrc);

		auto vb = VertexBuffer::Create(vertices, sizeof(vertices));
		auto ib = IndexBuffer::Create(indices, 6);

		BufferLayout layout =
		{
			{ ShaderDataType::Float3, sizeof(float) * 3, false },
			{ ShaderDataType::Float3, sizeof(float) * 3, false }
		};

		m_VertexArray = VertexArray::Create();
		m_VertexArray->AddVertexBuffer(vb, layout);
		m_VertexArray->SetIndexBuffer(ib);

		PipelineSpecification spec;
		spec.Shader = m_Shader;
		spec.DepthTest = false;
		spec.Blend = false;

		m_Pipeline = Pipeline::Create(spec);

		m_Texture = Texture2D::Create(1, 1);

		AXE_CORE_INFO("QuadRenderer created");
	}

	void QuadRenderer::Render(float timeSeconds)
	{
		const float c = std::cos(timeSeconds);
		const float s = std::sin(timeSeconds);

		// Matriz 4x4 em column-major, adequada para OpenGL
		const float model[16] =
		{
			 c,  s,  0.0f, 0.0f,
			-s,  c,  0.0f, 0.0f,
			 0.0f, 0.0f, 1.0f, 0.0f,
			 0.0f, 0.0f, 0.0f, 1.0f
		};
	
		m_Pipeline->Bind();
		m_VertexArray->Bind();
		m_Texture->Bind(0);

		m_Shader->SetMat4("u_Model", model);

		RenderCommand::DrawIndexed(m_VertexArray);
	}
}