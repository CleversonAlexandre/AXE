#pragma once
#include "axe/core/types.hpp"
#include "axe/graphics/texture.hpp"
#include <string>

namespace axe
{
	class AXE_API OpenGLTexture2D final : public Texture2D
	{
	public:
		OpenGLTexture2D(std::uint32_t width, std::uint32_t height);
		OpenGLTexture2D(const std::string& filepath);
		~OpenGLTexture2D() override;

		std::uint32_t GetWidth()      const override { return m_Width; }
		std::uint32_t GetHeight()     const override { return m_Height; }
		std::uint32_t GetRendererID() const override { return m_RendererID; }
		bool          IsLoaded()      const override { return m_Loaded; }

		void Bind(std::uint32_t slot = 0) const override;
		void Unbind() const override;

		// ── ASSET_VIEWER_V1 ─────────────────────────────────────────────
		std::uint32_t GetChannels() const override { return m_Channels; }
		const std::string& GetPath() const override { return m_Path; }

		Filter GetFilter() const override { return m_Filter; }
		Wrap   GetWrap()   const override { return m_Wrap; }
		void   SetFilter(Filter f) override;
		void   SetWrap(Wrap w) override;

	private:
		std::uint32_t m_Width = 0;
		std::uint32_t m_Height = 0;
		std::uint32_t m_RendererID = 0;
		bool          m_Loaded = false;

		// ASSET_VIEWER_V1 — guardados na importacao para a janela poder
		// mostrar sem reabrir o arquivo.
		std::uint32_t m_Channels = 0;
		std::string   m_Path;

		// O construtor de arquivo ja punha LINEAR_MIPMAP_LINEAR + REPEAT
		// (SRGB_TEXTURES_V1); estes campos so REFLETEM o que foi aplicado,
		// para a janela nao mentir sobre o estado atual.
		Filter m_Filter = Filter::Trilinear;
		Wrap   m_Wrap = Wrap::Repeat;
	};
}