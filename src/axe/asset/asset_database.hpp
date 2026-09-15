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

		// ═══════════════════════════════════════════════════════════════════
		//  SCENE_ASSET_V1 — REGISTRAR NAO BASTA PARA O ASSET APARECER
		//
		//  Esta funcao existia, palavra por palavra, dentro do
		//  SequencerWindow (`RegisterProjectAsset`). Ela sobe para ca porque
		//  agora tem DOIS usos reais — a sequence e a CENA — e porque as tres
		//  coisas que ela faz juntas sao justamente as que se esquece uma a
		//  uma quando cada janela reescreve o seu:
		//
		//   1. `Register` cria o record e grava o `.axemeta`. So isso NAO poe
		//      o arquivo na grade do Asset Browser: a grade filtra por
		//      `record.VirtualFolder == pasta selecionada`, e VirtualFolder
		//      NAO e o caminho em disco — e escrituracao do editor,
		//      preenchida so quando alguem importa ou arrasta o asset para
		//      uma pasta. Um record novo nasce com ela vazia, e o asset
		//      aparece apenas em "/ All".
		//
		//   2. Por isso a pasta virtual e DERIVADA do caminho em disco, uma
		//      unica vez. A convencao ja existe no sentido inverso — o
		//      `RelocateAssets` MOVE o arquivo para
		//      `<AssetsPath>/<VirtualFolder>` —, entao isto so fecha o ciclo
		//      no sentido que faltava. Ela so e preenchida quando esta VAZIA:
		//      se o usuario arrastou o asset para outra pasta do browser, a
		//      escolha dele vale mais que o disco, e regravar o arquivo nao
		//      pode desfaze-la.
		//
		//   3. `Save` grava o indice. Sem isso o record vive so nesta sessao
		//      — fechar e reabrir o editor perdia o registro, que e o pior
		//      sintoma possivel: funciona enquanto se olha e some depois.
		//
		//  `typeOverride` existe para o clipe assado, cuja extensao
		//  (`.axeclipbin`) NAO mapeia para tipo nenhum de proposito (ver
		//  AssetType::AnimationClip): ali o tipo tem de ser dito pelo
		//  chamador. Para uma cena `.axescene` a extensao ja basta.
		// ═══════════════════════════════════════════════════════════════════
		std::string RegisterInProject(const std::filesystem::path& filepath,
			AssetType typeOverride = AssetType::Unknown);

		// Varre uma pasta recursivamente e registra todos os assets
		void Scan(const std::filesystem::path& directory);

		// Lookups
		const AssetRecord* GetByUUID(const std::string& uuid) const;
		const AssetRecord* GetByPath(const std::filesystem::path& path) const;

		// ═══════════════════════════════════════════════════════════════════
		//  ASSET_VIEWER_V2 — gravar configuracao de importacao
		//
		//  Ponto UNICO para mexer nas settings: atualiza o registro em memoria
		//  E o `.axemeta` no disco na mesma chamada. Deixar o chamador fazer os
		//  dois passos convidaria a esquecer o segundo, e o sintoma seria o pior
		//  possivel — funciona na sessao e some ao reabrir, que foi exatamente
		//  a limitacao da fase 1.
		//
		//  Devolve false se o UUID nao existe. NAO invalida cache nem
		//  reimporta: quem sabe se e malha ou textura e a janela, e ela chama o
		//  invalidador certo depois. Misturar as duas coisas aqui poria o
		//  AssetDatabase conhecendo MeshLoader e Texture2D, que e acoplamento
		//  que ele nao tem hoje e nao precisa ter.
		// ═══════════════════════════════════════════════════════════════════
		bool SetImportSettings(const std::string& uuid, const AssetImportSettings& s);

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