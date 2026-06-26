#pragma once

#include "axe/core/types.hpp"
#include <memory>
#include <cstdint>
#include <string>

namespace axe
{
	class AXE_API Texture2D
	{
	public:
		virtual ~Texture2D() = default;

		virtual std::uint32_t GetWidth() const = 0;
		virtual std::uint32_t GetHeight() const = 0;

		virtual std::uint32_t GetRendererID() const = 0;
		virtual bool          IsLoaded()      const = 0;

		virtual void Bind(std::uint32_t slot = 0) const = 0;
		virtual void Unbind() const = 0;
		static std::shared_ptr<Texture2D> Create(std::uint32_t width, std::uint32_t height);
		static std::shared_ptr<Texture2D> Create(const std::string& filepath); // ← novo

		// Limpa o cache de texturas por filepath (ver texture.cpp). Útil ao
		// trocar de projeto.
		static void ClearCache();

		// Remove uma entrada específica do cache — usar ao reimportar uma
		// textura (artista substituiu o arquivo) pra forçar reler do disco
		// na próxima chamada a Create(filepath).
		static void InvalidateCache(const std::string& filepath);
	};
}