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

		// ═══════════════════════════════════════════════════════════════════
		//  ASSET_VIEWER_V1 — o que a janela de inspecao precisa saber e mexer
		//
		//  Tudo isto vive AQUI, e nao no editor, pela regra da casa: o editor
		//  nao toca em GL. Filtro e wrap sao estado de textura do driver, e so
		//  o backend pode escrever neles.
		//
		//  Sao virtuais com corpo padrao, e nao puros, de proposito: um
		//  backend futuro que nao suporte algum deles nao deixa de compilar —
		//  ele simplesmente nao muda nada, e a janela ainda abre.
		// ═══════════════════════════════════════════════════════════════════

		// Canais que o ARQUIVO tinha ao ser lido (1=cinza, 3=RGB, 4=RGBA).
		// 0 = desconhecido (textura criada em memoria, sem arquivo).
		virtual std::uint32_t GetChannels() const { return 0; }

		// Caminho do arquivo de origem, ou vazio se foi criada em memoria.
		virtual const std::string& GetPath() const
		{
			static const std::string kEmpty;
			return kEmpty;
		}

		enum class Filter { Nearest, Linear, Trilinear };
		enum class Wrap { Repeat, Clamp, Mirror };

		virtual Filter GetFilter() const { return Filter::Trilinear; }
		virtual Wrap   GetWrap()   const { return Wrap::Repeat; }

		// Aplicam NA HORA, sem recarregar do disco: sao parametros de
		// amostragem, nao do conteudo. E o que permite o usuario comparar
		// Nearest e Linear vendo o resultado, em vez de adivinhar.
		virtual void SetFilter(Filter) {}
		virtual void SetWrap(Wrap) {}

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