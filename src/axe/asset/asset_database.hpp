#pragma once
#include "axe/core/types.hpp"
#include "asset.hpp"
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <string>

namespace axe
{

	class AXE_API AssetDatabase
	{
	public:
		static AssetDatabase& Get();

		// Registra um asset pelo caminho — gera UUID se não tiver .axemeta
		// Retorna o UUID gerado ou existente
		std::string Register(const std::filesystem::path& filepath);

		// Varre uma pasta recursivamente e registra todos os assets
		void Scan(const std::filesystem::path& directory);

		// Lookups
		const AssetRecord* GetByUUID(const std::string& uuid) const;
		const AssetRecord* GetByPath(const std::filesystem::path& path) const;

		// Lista todos os assets de um tipo
		std::vector<const AssetRecord*> GetAllOfType(AssetType type) const;

		// Lista todos os assets
		const std::unordered_map<std::string, AssetRecord>& GetAll() const { return m_Records; }

		// Persiste o índice
		void Save(const std::filesystem::path& projectRoot);
		void Load(const std::filesystem::path& projectRoot);

		// Atualiza o caminho de um asset já registrado (rename/move).
		// Corrige o m_PathIndex (removendo a entrada antiga e criando a nova)
		// e reescreve o .axemeta. Sem isso, o caminho antigo continua
		// "registrado" em memória e um novo asset criado nesse mesmo caminho
		// reaproveita o UUID do asset antigo, corrompendo o registro dele.
		// Se newName for fornecido, também atualiza o Name do record.
		bool UpdatePath(const std::string& uuid, const std::filesystem::path& newPath,
			const std::string& newName = "");

		// ── Assets fora da raiz do projeto (PKG2) ────────────────────────────
		//
		// Arrastar um arquivo de fora para o editor REGISTRA O CAMINHO DE
		// ORIGEM; nada e copiado. Enquanto se usa so o editor, funciona — o
		// arquivo esta no disco. Mas o projeto nao e portatil: mandar a pasta
		// para outra maquina perde esses assets, e o empacotamento nao tem de
		// onde copia-los preservando a estrutura relativa.
		//
		// `ExternalAssets` lista os registrados fora da raiz.
		//
		// `ImportExternalAssets` copia cada um para `<root>/Assets/<subfolder>`,
		// junto com os arquivos-satelite (`.axemeta`, `.axemesh`,
		// `.axeskelbin`, `.axeclipbin`, `.axegraph`), e atualiza o indice via
		// UpdatePath. O arquivo de ORIGEM nao e apagado: ele nao pertence ao
		// projeto, e apagar coisa de fora da pasta do usuario nunca deve ser
		// efeito colateral de um botao no editor.
		//
		// O UUID e preservado, entao toda referencia existente continua valendo
		// — nenhuma cena, material ou grafo precisa ser reaberto.
		std::vector<const AssetRecord*> ExternalAssets(
			const std::filesystem::path& projectRoot) const;

		struct ImportExternalResult
		{
			std::size_t Imported = 0;
			std::vector<std::string> Failures;
		};

		ImportExternalResult ImportExternalAssets(
			const std::filesystem::path& projectRoot,
			const std::string& subfolder = "Imported");

		// Remove um asset do índice em memória (m_Records + m_PathIndex).
		// Sem isso, excluir um asset só removia o arquivo do disco — o
		// registro continuava vivo em memória e só desaparecia do browser
		// depois de reiniciar o editor (quando Load() filtra por existência
		// no disco). Os arquivos (.ext e .axemeta) devem já ter sido
		// removidos do disco pelo chamador antes de chamar este método.
		bool Unregister(const std::string& uuid);

		void Clear();

		void RegisterPrimitives();

	private:
		AssetDatabase() = default;

		std::string GenerateUUID() const;
		std::filesystem::path GetMetaPath(const std::filesystem::path& assetPath) const;
		bool ReadMeta(const std::filesystem::path& metaPath, AssetRecord& out) const;
		void WriteMeta(const AssetRecord& record) const;

		// UUID → AssetRecord
		std::unordered_map<std::string, AssetRecord> m_Records;

		// Caminho absoluto (string) → UUID
		std::unordered_map<std::string, std::string> m_PathIndex;
	};

} // namespace axe