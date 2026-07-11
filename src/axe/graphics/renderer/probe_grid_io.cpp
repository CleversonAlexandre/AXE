#include "axe/lighting/probe_volume.hpp"
#include "axe/graphics/texture3d.hpp"
#include "axe/log/log.hpp"
#include <fstream>
#include <cstring>

namespace axe
{
	// ── .axeprobes — IO binário do ProbeGrid ─────────────────────────────
	// Formato (little-endian, ver comentário no probe_volume.hpp):
	//   char[8]  magic   = "AXEPROBE"
	//   uint32   version = 1
	//   int32[3] resolution
	//   4 blocos de float[total*4]: SH0, SH1X, SH1Y, SH1Z
	//
	// Filosofia: dados crus, sem compressão — um grid 16x16x16 (máximo)
	// custa 1 MB; o típico fica em ~100-300 KB. Simplicidade > bytes.

	static constexpr char     kMagic[8] = { 'A','X','E','P','R','O','B','E' };
	static constexpr uint32_t kVersion = 1;

	bool SaveProbeGridToFile(const std::string& path, const ProbeGrid& grid)
	{
		if (!grid.IsValid() || !grid.HasCPUData())
		{
			AXE_CORE_WARN("ProbeGridIO: grid inválido ou sem dados CPU — "
				"'{}' não foi salvo (rebakeie antes de salvar).", path);
			return false;
		}

		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f.is_open())
		{
			AXE_CORE_ERROR("ProbeGridIO: falha ao abrir '{}' para escrita.", path);
			return false;
		}

		f.write(kMagic, sizeof(kMagic));
		f.write(reinterpret_cast<const char*>(&kVersion), sizeof(kVersion));
		f.write(reinterpret_cast<const char*>(&grid.Resolution), sizeof(int32_t) * 3);

		auto writeBlock = [&](const std::vector<float>& v)
			{
				f.write(reinterpret_cast<const char*>(v.data()),
					(std::streamsize)(v.size() * sizeof(float)));
			};
		writeBlock(grid.DataSH0);
		writeBlock(grid.DataSH1X);
		writeBlock(grid.DataSH1Y);
		writeBlock(grid.DataSH1Z);

		if (!f.good())
		{
			AXE_CORE_ERROR("ProbeGridIO: erro de escrita em '{}'.", path);
			return false;
		}

		AXE_CORE_INFO("ProbeGridIO: grid {}x{}x{} salvo em '{}'.",
			grid.Resolution.x, grid.Resolution.y, grid.Resolution.z, path);
		return true;
	}

	std::shared_ptr<ProbeGrid> LoadProbeGridFromFile(const std::string& path)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f.is_open()) return nullptr; // ausência de arquivo não é erro

		char magic[8] = {};
		uint32_t version = 0;
		glm::ivec3 res{ 0 };

		f.read(magic, sizeof(magic));
		f.read(reinterpret_cast<char*>(&version), sizeof(version));
		f.read(reinterpret_cast<char*>(&res), sizeof(int32_t) * 3);

		if (!f.good() || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0)
		{
			AXE_CORE_WARN("ProbeGridIO: '{}' não é um .axeprobes válido.", path);
			return nullptr;
		}
		if (version != kVersion)
		{
			AXE_CORE_WARN("ProbeGridIO: '{}' tem versão {} (esperada {}) — "
				"ignorado, será rebakeado.", path, version, kVersion);
			return nullptr;
		}
		// Sanidade: mesmo range do Inspector/bake (2..16 por eixo)
		if (res.x < 2 || res.y < 2 || res.z < 2 ||
			res.x > 16 || res.y > 16 || res.z > 16)
		{
			AXE_CORE_WARN("ProbeGridIO: '{}' com resolução inválida "
				"{}x{}x{} — ignorado.", path, res.x, res.y, res.z);
			return nullptr;
		}

		const size_t count = (size_t)res.x * res.y * res.z * 4;
		auto grid = std::make_shared<ProbeGrid>();
		grid->Resolution = res;
		grid->DataSH0.resize(count);
		grid->DataSH1X.resize(count);
		grid->DataSH1Y.resize(count);
		grid->DataSH1Z.resize(count);

		auto readBlock = [&](std::vector<float>& v)
			{
				f.read(reinterpret_cast<char*>(v.data()),
					(std::streamsize)(v.size() * sizeof(float)));
			};
		readBlock(grid->DataSH0);
		readBlock(grid->DataSH1X);
		readBlock(grid->DataSH1Y);
		readBlock(grid->DataSH1Z);

		if (!f.good())
		{
			AXE_CORE_WARN("ProbeGridIO: '{}' truncado/corrompido — "
				"ignorado, será rebakeado.", path);
			return nullptr;
		}

		// Sobe pra GPU — requer contexto GL ativo, o que é garantido: o
		// load de cena acontece na main thread do editor, com o contexto
		// corrente (mesma premissa do resto do load: meshes, texturas).
		grid->SH0 = Texture3D::CreateRGBA16F(res.x, res.y, res.z, grid->DataSH0.data());
		grid->SH1X = Texture3D::CreateRGBA16F(res.x, res.y, res.z, grid->DataSH1X.data());
		grid->SH1Y = Texture3D::CreateRGBA16F(res.x, res.y, res.z, grid->DataSH1Y.data());
		grid->SH1Z = Texture3D::CreateRGBA16F(res.x, res.y, res.z, grid->DataSH1Z.data());

		AXE_CORE_INFO("ProbeGridIO: grid {}x{}x{} carregado de '{}' — "
			"cena abre com GI pronto, sem rebake.",
			res.x, res.y, res.z, path);
		return grid;
	}

} // namespace axe