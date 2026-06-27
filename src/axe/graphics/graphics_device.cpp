#include "axe/graphics/graphics_device.hpp"

#include "axe/axe_window/window.hpp"
#include "axe/log/log.hpp"
#include "axe/graphics/render_command.hpp"

namespace axe
{
	bool GraphicsDevice::Initialize(Window* window)
	{
		m_Window = window;

		if (m_Window == nullptr)
		{
			AXE_CORE_ERROR("GraphicsDevice received null window");
			return false;
		}

		if (m_Window->GetNativeWindow() == nullptr)
		{
			AXE_CORE_ERROR("GraphicsDevice received invalid native window");
			return false;
		}

		// RenderCommand::Init() cria o backend (RendererAPI::Create) — TEM
		// que vir ANTES de qualquer comando de estado, senão s_RendererAPI
		// ainda é nulo. Antes este bloco usava glEnable/glBlendFunc crus e
		// chamava Init() depois; agora o estado inicial passa pela abstração.
		RenderCommand::Init();

		RenderCommand::SetBlend(true);
		RenderCommand::SetBlendFunc(RendererAPI::BlendFactor::SrcAlpha, RendererAPI::BlendFactor::OneMinusSrcAlpha);
		RenderCommand::SetDepthTest(true);

		//AXE_CORE_INFO("GraphicsDevice Initialized successfully");
		return true;
	}

	void GraphicsDevice::Shutdown()
	{
		AXE_CORE_INFO("GraphicsDevice shutdown");
	}

	void GraphicsDevice::BeginFrame()
	{
		// Tamanho do framebuffer vem da abstração de janela (que sabe lidar
		// com high-DPI), não de glfwGetFramebufferSize direto.
		int width = 0;
		int height = 0;
		m_Window->GetFramebufferSize(width, height);

		RenderCommand::SetViewport(0, 0, (uint32_t)width, (uint32_t)height);
		RenderCommand::SetClearColor(
			m_ClearColor[0],
			m_ClearColor[1],
			m_ClearColor[2],
			m_ClearColor[3]
		);
		RenderCommand::ClearColorDepth();
	}

	void GraphicsDevice::EndFrame()
	{
		// Por enquanto vazio.
		// Depois pode ter flush, stats, render passes, debug markers, etc.
	}

	void GraphicsDevice::SetClearColor(float r, float g, float b, float a)
	{
		m_ClearColor[0] = r;
		m_ClearColor[1] = g;
		m_ClearColor[2] = b;
		m_ClearColor[3] = a;
	}
}