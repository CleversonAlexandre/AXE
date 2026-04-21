#include "axe/graphics/renderer/cube_renderer.hpp"

#include "axe/log/log.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/buffer.hpp"
#include "axe/graphics/vertex_array.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/graphics/pipeline.hpp"
#include "axe/graphics/texture.hpp"

#include <cmath>

namespace axe
{
	namespace
	{
		void MultiplyMat4(const float* a, const float* b, float* result)
		{
			for (int col = 0; col < 4; ++col)
			{
				for (int row = 0; row < 4; ++row)
				{
					result[col * 4 + row] =
						a[0 * 4 + row] * b[col * 4 + 0] +
						a[1 * 4 + row] * b[col * 4 + 1] +
						a[2 * 4 + row] * b[col * 4 + 2] +
						a[3 * 4 + row] * b[col * 4 + 3];
				}
			}
		}

		void MakePerspective(float fovRadians, float aspect, float zNear, float zFar, float* out)
		{
			const float f = 1.0f / std::tan(fovRadians * 0.5f);

			for (int i = 0; i < 16; ++i)
				out[i] = 0.0f;

			out[0] = f / aspect;
			out[5] = f;
			out[10] = (zFar + zNear) / (zNear - zFar);
			out[11] = -1.0f;
			out[14] = (2.0f * zFar * zNear) / (zNear - zFar);
		}

		void MakeTranslation(float x, float y, float z, float* out)
		{
			for (int i = 0; i < 16; ++i)
				out[i] = 0.0f;

			out[0] = 1.0f;
			out[5] = 1.0f;
			out[10] = 1.0f;
			out[15] = 1.0f;

			out[12] = x;
			out[13] = y;
			out[14] = z;
		}

		void MakeRotationY(float angle, float* out)
		{
			const float c = std::cos(angle);
			const float s = std::sin(angle);

			for (int i = 0; i < 16; ++i)
				out[i] = 0.0f;

			out[0] = c;
			out[2] = -s;
			out[5] = 1.0f;
			out[8] = s;
			out[10] = c;
			out[15] = 1.0f;
		}

		void MakeRotationX(float angle, float* out)
		{
			const float c = std::cos(angle);
			const float s = std::sin(angle);

			for (int i = 0; i < 16; ++i)
				out[i] = 0.0f;

			out[0] = 1.0f;
			out[5] = c;
			out[6] = s;
			out[9] = -s;
			out[10] = c;
			out[15] = 1.0f;
		}
	}

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
			// back
			0, 1, 2,  2, 3, 0,
			// front
			4, 5, 6,  6, 7, 4,
			// left
			7, 4, 0,  0, 3, 7,
			// right
			6, 5, 1,  1, 2, 6,
			// bottom
			4, 5, 1,  1, 0, 4,
			// top
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

		AXE_CORE_INFO("CubeRenderer created");
	}

	void CubeRenderer::Render(float timeSeconds, float aspectRatio)
	{
		float rotY[16];
		float rotX[16];
		float model[16];

		MakeRotationY(timeSeconds, rotY);
		MakeRotationX(timeSeconds * 0.6f, rotX);
		MultiplyMat4(rotY, rotX, model);

		float view[16];
		MakeTranslation(0.0f, 0.0f, -3.0f, view);

		float projection[16];
		MakePerspective(45.0f * 3.14159265f / 180.0f, aspectRatio, 0.1f, 100.0f, projection);

		float viewProjection[16];
		MultiplyMat4(projection, view, viewProjection);

		m_Pipeline->Bind();
		m_VertexArray->Bind();
		m_Texture->Bind(0);

		m_Shader->SetMat4("u_Model", model);
		m_Shader->SetMat4("u_ViewProjection", viewProjection);

		RenderCommand::DrawIndexed(m_VertexArray);
	}
}