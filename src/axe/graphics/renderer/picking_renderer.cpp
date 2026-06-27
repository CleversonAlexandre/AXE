#include "picking_renderer.hpp"
#include "axe/graphics/shader.hpp"
#include "axe/graphics/pipeline.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/graphics/render_command.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/utils/glm_config.hpp"


namespace axe
{

	PickingRenderer::PickingRenderer()
	{
		// Shader minimalista — só precisa de posição
		// Escreve o ID do objeto como cor RGBA
		const std::string vertexSrc = R"(
		#version 460 core

		layout(location = 0) in vec3 a_Position;

		uniform mat4 u_Model;
		uniform mat4 u_ViewProjection;

		void main()
		{
			gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
		}
	)";

		const std::string fragmentSrc = R"(
		#version 460 core

		layout(location = 0) out vec4 FragColor;

		uniform uint u_ObjectID;

		void main()
		{
			// Decompõe o ID em 4 bytes e normaliza para [0, 1]
			float r = float((u_ObjectID >>  0) & 0xFFu) / 255.0;
			float g = float((u_ObjectID >>  8) & 0xFFu) / 255.0;
			float b = float((u_ObjectID >> 16) & 0xFFu) / 255.0;
			float a = float((u_ObjectID >> 24) & 0xFFu) / 255.0;

			FragColor = vec4(r, g, b, a);
		}
	)";

		m_Shader = Shader::Create(vertexSrc, fragmentSrc);

		PipelineSpecification spec;
		spec.Shader = m_Shader;
		spec.DepthTest = true;
		m_Pipeline = Pipeline::Create(spec);

		// Framebuffer dedicado ao picking
		FramebufferSpecification fbSpec;
		fbSpec.Width = 1280;
		fbSpec.Height = 720;
		m_Framebuffer = Framebuffer::Create(fbSpec);
	}

	void PickingRenderer::Begin(const glm::mat4& viewProjection)
	{
		m_ViewProjection = viewProjection;

		m_Framebuffer->Bind();
		RenderCommand::SetClearColor(0.0f, 0.0f, 0.0f, 0.0f); // ID 0 = nenhum objeto
		RenderCommand::ClearColorDepth();
	}

	void PickingRenderer::DrawMesh(const Mesh& mesh, const glm::mat4& model, std::uint32_t objectID)
	{
		m_Pipeline->Bind();
		mesh.GetVertexArray()->Bind();

		m_Shader->SetMat4("u_Model", glm::value_ptr(model));
		m_Shader->SetMat4("u_ViewProjection", glm::value_ptr(m_ViewProjection));
		m_Shader->SetUint("u_ObjectID", objectID);

		RenderCommand::DrawIndexed(mesh.GetVertexArray());
	}

	void PickingRenderer::DrawCube(const glm::mat4& model, std::uint32_t objectID)
	{
		// Objetos sem mesh usam um cubo gerado proceduralmente
		static std::shared_ptr<Mesh> s_CubeMesh = MeshFactory::CreateCube();
		DrawMesh(*s_CubeMesh, model, objectID);
	}

	void PickingRenderer::End()
	{
		m_Framebuffer->Unbind();
	}

	std::uint32_t PickingRenderer::ReadPixel(std::uint32_t x, std::uint32_t y) const
	{
		return m_Framebuffer->ReadPixel(x, y);
	}

	void PickingRenderer::Resize(std::uint32_t width, std::uint32_t height)
	{
		m_Framebuffer->Resize(width, height);
	}

} // namespace axe