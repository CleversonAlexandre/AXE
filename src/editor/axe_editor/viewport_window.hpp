#pragma once
#include "axe/core/types.hpp"
#include <memory>
#include <functional>
#include <string>
#include <glm/glm.hpp>

#include "axe/graphics/editor_camera.hpp"
#include <imgui.h>
namespace axe
{
	class Framebuffer;
	class EditorCamera;
	class ViewportRenderer;

	class ViewportWindow
	{
	public:
		ViewportWindow();
		~ViewportWindow();

		void Initialize();
		void Draw();
		void OnResize(uint32_t width, uint32_t height);
		void DrawToolbar();


		bool IsInitialized() const { return m_Initialized; }
		bool IsHovered()     const { return m_IsHovered; }
		bool IsFocused()     const { return m_IsFocused; }

		uint32_t GetWidth()  const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		glm::vec2 GetMouseDelta()    const { return m_MouseDelta; }
		glm::vec2 GetMousePosition() const { return m_MousePosition; }
		glm::vec2 GetBoundsMin()     const { return m_BoundsMin; }
		glm::vec2 GetBoundsMax()     const { return m_BoundsMax; }
		glm::vec2 GetSize()          const { return { (float)m_Width, (float)m_Height }; }

		std::shared_ptr<Framebuffer> GetFramebuffer() const { return m_Framebuffer; }
		ImTextureID GetTextureID() const;

		using GuizmoDrawFunc = std::function<void(const glm::vec2&, const glm::vec2&)>;
		void SetGuizmoCallback(GuizmoDrawFunc func) { m_GuizmoCallback = func; }

		// Retorna string de preview (ex: "Entity\nCubo + Rigidbody") ou "" para assets sem preview
		using DragPreviewCallback = std::function<std::string(const std::string& uuid)>;

		// Asset drop — público
		using AssetDropCallback = std::function<void(const std::string&, float, float)>;
		void SetAssetDropCallback(AssetDropCallback cb) { m_AssetDropCallback = cb; }
		void SetDragPreviewCallback(DragPreviewCallback cb) { m_DragPreviewCallback = cb; }
		void SetDragEndCallback(std::function<void()> cb) { m_DragEndCallback = cb; }

		std::unique_ptr<EditorCamera> m_Camera;

		using PlayStateCallback = std::function<int()>; //retorna 0=Editor, 1=Play, 2=Pause
		using PlayActionCallback = std::function<void(int)>; //0=Play, 1=Pause, 1=Stop

		void SetPlayStateCallback(PlayStateCallback cb) { m_PlayStateCallback = cb; }
		void SetPlayActionCallback(PlayActionCallback cb) { m_PlayActionCallback = cb; }

		void SetViewportRenderer(ViewportRenderer* renderer) { m_ViewportRenderer = renderer; }

	private:
		ViewportRenderer* m_ViewportRenderer = nullptr;


	private:
		bool m_Initialized = false;
		bool m_IsHovered = false;
		bool m_IsFocused = false;

		std::uint32_t m_Width = 0;
		std::uint32_t m_Height = 0;

		glm::vec2 m_MousePosition{ 0.0f, 0.0f };
		glm::vec2 m_LastMousePosition{ 0.0f, 0.0f };
		glm::vec2 m_MouseDelta{ 0.0f, 0.0f };
		glm::vec2 m_BoundsMin{ 0.0f, 0.0f };
		glm::vec2 m_BoundsMax{ 0.0f, 0.0f };

		std::shared_ptr<Framebuffer> m_Framebuffer;

		GuizmoDrawFunc    m_GuizmoCallback;
		AssetDropCallback   m_AssetDropCallback;
		DragPreviewCallback m_DragPreviewCallback;
		std::function<void()> m_DragEndCallback;
		bool m_WasDragging = false;

		PlayStateCallback m_PlayStateCallback;
		PlayActionCallback m_PlayActionCallback;
	};
}