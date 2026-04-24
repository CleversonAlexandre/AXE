#include "line_renderer.hpp"

#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace axe
{
	struct LineVertex
	{
		glm::vec3 Position;
		glm::vec4 Color;
	};

	LineRenderer::LineRenderer()
	{
		const std::string vertexSrc = R"(
			#version 460 core

			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec4 a_Color;

			uniform mat4 u_ViewProjection;

			out vec4 v_Color;

			void main()
			{
				v_Color = a_Color;
				gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
			}
		)";

		const std::string fragmentSrc = R"(
			#version 460 core

			in vec4 v_Color;
			layout(location = 0) out vec4 color;

			void main()
			{
				color = v_Color;
			}
		)";

		m_Shader = Shader::Create(vertexSrc, fragmentSrc);

		LineVertex vertices[2];

		m_VertexArray = VertexArray::Create();

		m_VertexBuffer = VertexBuffer::Create(vertices, sizeof(vertices));

		BufferLayout layout =
		{
			{ ShaderDataType::Float3, sizeof(float) * 3, false },
			{ ShaderDataType::Float4, sizeof(float) * 4, false }
		};

		m_VertexArray->AddVertexBuffer(m_VertexBuffer, layout);
	}

	void LineRenderer::Begin(const glm::mat4& viewProjection)
	{
		m_ViewProjection = viewProjection;
		m_Shader->Bind();
		m_Shader->SetMat4("u_ViewProjection", glm::value_ptr(m_ViewProjection));
	}

	void LineRenderer::DrawBoundingBox(const glm::mat4& model, const glm::vec4& color)
	{
		glm::vec3 corners[8] =
		{
			{ -0.5f, -0.5f, -0.5f },
			{  0.5f, -0.5f, -0.5f },
			{  0.5f,  0.5f, -0.5f },
			{ -0.5f,  0.5f, -0.5f },

			{ -0.5f, -0.5f,  0.5f },
			{  0.5f, -0.5f,  0.5f },
			{  0.5f,  0.5f,  0.5f },
			{ -0.5f,  0.5f,  0.5f }
		};

		for (auto& corner : corners)
			corner = glm::vec3(model * glm::vec4(corner, 1.0f));

		DrawLine(corners[0], corners[1], color);
		DrawLine(corners[1], corners[2], color);
		DrawLine(corners[2], corners[3], color);
		DrawLine(corners[3], corners[0], color);

		DrawLine(corners[4], corners[5], color);
		DrawLine(corners[5], corners[6], color);
		DrawLine(corners[6], corners[7], color);
		DrawLine(corners[7], corners[4], color);

		DrawLine(corners[0], corners[4], color);
		DrawLine(corners[1], corners[5], color);
		DrawLine(corners[2], corners[6], color);
		DrawLine(corners[3], corners[7], color);
	}

	void LineRenderer::DrawLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color)
	{
		LineVertex vertices[2];
		vertices[0] = { a, color };
		vertices[1] = { b, color };

		m_VertexBuffer->SetData(vertices, sizeof(vertices));

		m_Shader->Bind();
		m_VertexArray->Bind();

		RenderCommand::DrawLines(m_VertexArray, 2);
	}

	void LineRenderer::End()
	{}
}