#include "axe/graphics/texture.hpp"
#include "axe/graphics/opengl/opengl_texture.hpp"
#include <string>
#include <unordered_map>
#include "axe/asset/asset_database.hpp"
#include <algorithm>

namespace axe
{

	std::shared_ptr<Texture2D> Texture2D::Create(std::uint32_t width, std::uint32_t height)
	{
		return std::make_shared<OpenGLTexture2D>(width, height);
	}

	// NOTA (ASSET_VIEWER_V2): o Asset Viewer agora pode chamar SetFilter/SetWrap
	// numa instancia cacheada. Como a instancia e COMPARTILHADA, o ajuste vale
	// para todo mundo que usa a mesma textura — que e exatamente o que se
	// espera de "mudei o filtro deste asset", e o mesmo resultado que o
	// .axemeta produz no proximo load.
	//
	// Cache por filepath — evita decodificar a imagem (stbi_load) e recriar
	// a textura na GPU toda vez que o mesmo arquivo é referenciado (ex:
	// vários materiais usando a mesma textura, ou a cena sendo
	// (re)serializada ao abrir/Play/Stop). Texture2D só expõe métodos de
	// leitura (Bind/Unbind/GetWidth/GetHeight/...), então é seguro
	// compartilhar a mesma instância entre N donos via shared_ptr.
	static std::unordered_map<std::string, std::shared_ptr<Texture2D>> s_TextureCache;

	void Texture2D::ClearCache()
	{
		s_TextureCache.clear();
	}

	void Texture2D::InvalidateCache(const std::string& filepath)
	{
		s_TextureCache.erase(filepath);
	}

	std::shared_ptr<Texture2D> Texture2D::Create(const std::string& filepath)
	{
		auto cached = s_TextureCache.find(filepath);
		if (cached != s_TextureCache.end())
			return cached->second;

		auto tex = std::make_shared<OpenGLTexture2D>(filepath);

		// ── ASSET_VIEWER_V2 — filtro e wrap vem do .axemeta ────────────────
		//
		// Aplicado AQUI, e nao no construtor, por dois motivos:
		//
		//  1. O construtor vive no backend OpenGL, que nao conhece (nem deve
		//     conhecer) o AssetDatabase. Ele so sabe criar a textura com o
		//     padrao; quem sabe o que o PROJETO pediu e esta camada.
		//
		//  2. So o caminho por filepath tem meta. A sobrecarga por
		//     largura/altura (render targets, texturas geradas) nao tem
		//     arquivo nenhum e continua intocada.
		//
		// Se nao ha projeto aberto, ou o arquivo esta fora dele, GetByPath
		// devolve nullptr e a textura fica no padrao — que e o comportamento
		// de antes desta mudanca.
		if (tex->IsLoaded())
		{
			if (const AssetRecord* rec = AssetDatabase::Get().GetByPath(filepath))
			{
				const AssetImportSettings& imp = rec->Import;
				tex->SetFilter(static_cast<Texture2D::Filter>(
					std::clamp(imp.TextureFilter, 0, 2)));
				tex->SetWrap(static_cast<Texture2D::Wrap>(
					std::clamp(imp.TextureWrap, 0, 2)));
			}

			s_TextureCache[filepath] = tex;
		}
		return tex;
	}

} // namespace axe