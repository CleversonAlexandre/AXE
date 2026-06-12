#include "axe/graphics/renderer/cube_renderer.hpp"

#include "axe/log/log.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/graphics/pipeline.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/graphics/editor_camera.hpp"

#include "axe/graphics/renderer/cube_renderer.hpp"

#include "axe/graphics/render_command.hpp"
#include "axe/graphics/renderer_api.hpp"

#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace axe
{
	
		CubeRenderer::CubeRenderer()
		{
			const float vertices[] =
			{
				// position              // color
				-0.5f, -0.5f, -0.5f,     1.0f, 0.0f, 0.0f,
				 0.5f, -0.5f, -0.5f,     0.0f, 1.0f, 0.0f,
				 0.5f,  0.5f, -0.5f,     0.0f, 0.0f, 1.0f,
				-0.5f,  0.5f, -0.5f,     1.0f, 1.0f, 0.0f,

				-0.5f, -0.5f,  0.5f,     1.0f, 0.0f, 1.0f,
				 0.5f, -0.5f,  0.5f,     0.0f, 1.0f, 1.0f,
				 0.5f,  0.5f,  0.5f,     1.0f, 1.0f, 1.0f,
				-0.5f,  0.5f,  0.5f,     0.2f, 0.2f, 0.2f
			};

			const std::uint32_t indices[] =
			{
				0, 1, 2,  2, 3, 0,
				4, 5, 6,  6, 7, 4,
				7, 4, 0,  0, 3, 7,
				6, 5, 1,  1, 2, 6,
				4, 5, 1,  1, 0, 4,
				7, 6, 2,  2, 3, 7
			};

			const std::string vertexSrc = R"(
			#version 460 core
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec3 a_Color;

			uniform mat4 u_Model;
			uniform mat4 u_ViewProjection;

			out vec3 v_Color;

			void main()
			{
				v_Color = a_Color;
				gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
			}
		)";

			const std::string fragmentSrc = R"(
			in vec3 v_Color;

			uniform bool u_UseOverrideColor;
			uniform vec4 u_OverrideColor;

			layout(location = 0) out vec4 color;

			void main()
			{
				if (u_UseOverrideColor)
					color = u_OverrideColor;
				else
					color = vec4(v_Color, 1.0);
			}
		)";

			m_Shader = Shader::Create(vertexSrc, fragmentSrc);

			auto vb = VertexBuffer::Create(vertices, sizeof(vertices));
			auto ib = IndexBuffer::Create(indices, 36);

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
			spec.DepthTest = true;
			spec.Blend = false;

			m_Pipeline = Pipeline::Create(spec);
			m_Texture = Texture2D::Create(1, 1);

			//AXE_CORE_INFO("CubeRenderer created");
	}
	
	void CubeRenderer::Begin(const glm::mat4& viewProjection)
	{
		m_ViewProjection = viewProjection;
	}

	void CubeRenderer::DrawCube(const glm::mat4& model, bool selected, const glm::vec4& color)
	{
		m_Pipeline->Bind();
		m_VertexArray->Bind();
		m_Texture->Bind(0);

		m_Shader->SetMat4("u_Model", glm::value_ptr(model));
		m_Shader->SetMat4("u_ViewProjection", glm::value_ptr(m_ViewProjection));

		m_Shader->SetInt("u_UseOverrideColor", selected ? 1 : 0);

		if (selected)
		{
			m_Shader->SetFloat4("u_OverrideColor", color);
		}

		RenderCommand::DrawIndexed(m_VertexArray);
	}


	void CubeRenderer::End()
	{}

	void CubeRenderer::DrawCubeWireframe(const glm::mat4& model)
	{
		glm::mat4 wireModel = glm::scale(model, glm::vec3(1.01f));

		RenderCommand::SetPolygonMode(RendererAPI::PolygonMode::Line);

		DrawCube(wireModel, true, { 0.0f, 0.0f, 0.0f, 1.0f });

		RenderCommand::SetPolygonMode(RendererAPI::PolygonMode::Fill);
	}
}