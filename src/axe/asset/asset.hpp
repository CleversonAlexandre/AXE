#pragma once
#include "axe/core/types.hpp"
#include <string>
#include <filesystem>
#include <cctype>

namespace axe
{

	enum class AssetType
	{
		Unknown,
		Mesh,       // .obj, .gltf, .glb
		Texture,    // .png, .jpg, .jpeg
		Scene,      // .axescene
		Audio,      // .wav, .mp3
		Script,      // .lua (futuro)
		Material,
		GameMode,    // .axegamemode
		ParticleSystem, // .axepart
		SkeletalMesh,   // .axeskel — personagem animado (aponta pro FBX + lista de animacoes)
		AnimGraph,      // .axeanim — state machine de animacao
		ControlRig,     // .axerig — hierarquia + grafo de rig (Forwards Solve)
		SoundCue,       // .axecue — grafo de selecao/parametros de som

		// ── CLIPE ASSADO (.axeclipbin vindo do Sequencer) ────────────────────
		//
		// NAO tem entrada em AssetTypeFromExtension, e isso e deliberado.
		//
		// A esmagadora maioria dos `.axeclipbin` do projeto e DERIVADA: o
		// cozido de um FBX de animacao, irmao dele, regeravel a qualquer
		// momento. Se a extensao mapeasse para um tipo, o Scan registraria
		// todos eles e o Asset Browser encheria de duplicatas de cada
		// animacao importada.
		//
		// O clipe ASSADO e diferente: nao veio de FBX nenhum, nao e regeravel,
		// e o unico jeito de acha-lo e ele aparecer. Por isso o bake o REGISTRA
		// explicitamente, com este tipo — a distincao passa a ser "alguem
		// registrou de proposito", e nao a extensao.
		AnimationClip,

		// .axeseq — uma Sequence do Sequencer (cutscene / animacao autorada).
		//
		// O formato ja existia e ja serializava tudo; o que nao existia era
		// TIPO. Sem ele o arquivo nao aparecia no Asset Browser, nao tinha
		// UUID e nao podia ser aberto por duplo clique — na pratica so havia
		// UMA sequence por projeto, no caminho fixo que os dois botoes da
		// janela usavam.
		Sequence
	};

	// Converte extensão para tipo
	static AssetType AssetTypeFromExtension(const std::string& extension)
	{
		// Normaliza o caso ANTES de comparar. O Windows preserva o caso do
		// nome do arquivo, e "Explosion.WAV" ou "Rock.PNG" chegavam aqui
		// exatamente assim — davam Unknown e eram PULADOS pelo
		// AssetDatabase::Scan, sem log e sem erro. O arquivo estava na pasta
		// Assets e simplesmente nao existia para o editor.
		std::string ext = extension;
		for (char& c : ext)
			c = (char)std::tolower((unsigned char)c);

		// .fbx e .dae entram como Mesh: sao FONTES. Clicar com o direito
		// num deles oferece "Importar como Skeletal Mesh", que gera o .axeskel.
		if (ext == ".obj" || ext == ".gltf" || ext == ".glb" ||
			ext == ".fbx" || ext == ".dae")                  return AssetType::Mesh;
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")return AssetType::Texture;
		if (ext == ".axescene")                              return AssetType::Scene;
		if (ext == ".wav" || ext == ".mp3" ||
			ext == ".flac")                                  return AssetType::Audio;
		if (ext == ".lua" || ext == ".axescript")           return AssetType::Script;
		if (ext == ".axemat")                                return AssetType::Material;
		if (ext == ".axegamemode")                           return AssetType::GameMode;
		if (ext == ".axepart")                                return AssetType::ParticleSystem;
		if (ext == ".axeskel")                                return AssetType::SkeletalMesh;
		if (ext == ".axeanim")                                return AssetType::AnimGraph;
		if (ext == ".axerig")                                 return AssetType::ControlRig;
		if (ext == ".axecue")                                 return AssetType::SoundCue;
		if (ext == ".axeseq")                                 return AssetType::Sequence;
		return AssetType::Unknown;
	}

	static std::string AssetTypeToString(AssetType type)
	{
		switch (type)
		{
		case AssetType::Mesh:		return "Mesh";
		case AssetType::Texture:	return "Texture";
		case AssetType::Scene:		return "Scene";
		case AssetType::Audio:		return "Audio";
		case AssetType::Script:		return "Script";
		case AssetType::Material:	return "Material";
		case AssetType::GameMode:	return "GameMode";
		case AssetType::ParticleSystem: return "ParticleSystem";
		case AssetType::SkeletalMesh:   return "SkeletalMesh";
		case AssetType::AnimGraph:      return "AnimGraph";
		case AssetType::ControlRig:     return "ControlRig";
		case AssetType::SoundCue:       return "SoundCue";
		case AssetType::Sequence:       return "Sequence";
		case AssetType::AnimationClip:  return "AnimationClip";

		default:					return "Unknown";
		}
	}

	static AssetType AssetTypeFromString(const std::string& str)
	{
		if (str == "Mesh")    return AssetType::Mesh;
		if (str == "Texture") return AssetType::Texture;
		if (str == "Scene")   return AssetType::Scene;
		if (str == "Audio")   return AssetType::Audio;
		if (str == "Script")  return AssetType::Script;
		if (str == "Material")  return AssetType::Material;
		if (str == "GameMode")  return AssetType::GameMode;
		if (str == "ParticleSystem") return AssetType::ParticleSystem;

		// Estes tres faltavam. O sintoma nao era erro de compilacao nem
		// warning: era o AssetDatabase reler o proprio arquivo e devolver
		// Unknown para todo .axeskel, .axeanim e .axerig — ou seja, todo
		// asset de personagem e de rig perdia o tipo no load, e sumia dos
		// filtros do AssetPicker e do Asset Browser.
		//
		// AssetTypeToString ja escrevia os tres corretamente; so o caminho
		// de volta estava incompleto. Round-trip so quebra num sentido, que
		// e o jeito mais silencioso possivel de quebrar.
		if (str == "SkeletalMesh")   return AssetType::SkeletalMesh;
		if (str == "AnimGraph")      return AssetType::AnimGraph;
		if (str == "ControlRig")     return AssetType::ControlRig;
		if (str == "SoundCue")       return AssetType::SoundCue;
		if (str == "Sequence")       return AssetType::Sequence;
		if (str == "AnimationClip")  return AssetType::AnimationClip;

		return AssetType::Unknown;
	}

	// Um asset registrado no database
	// ═══════════════════════════════════════════════════════════════════════
	//  ASSET_VIEWER_V2 — O `.axemeta` VIRA ARQUIVO DE CONFIGURACAO
	//
	//  Ate aqui o `.axemeta` guardava so identidade: uuid, tipo, nome, caminho.
	//  Nao havia NENHUMA configuracao de importacao em lugar nenhum da engine —
	//  o que chegava do DCC era o que se usava, e corrigir escala ou pivo
	//  errado so dava por dois caminhos: voltar ao Blender e reexportar, ou
	//  compensar a mao no Transform de cada entidade que usasse a malha.
	//
	//  O segundo e o pior, porque a compensacao fica no projeto para sempre e
	//  briga de novo em cada socket, cada colisao, cada particula ancorada.
	//  A pistola deste projeto e o caso vivo: 5x maior que o real e com o pivo
	//  no canto, compensada com Rotation 90/180 e Scale 0.5 no script.
	//
	//  ── ONDE ESTA STRUCT MORA, E POR QUE AQUI ──────────────────────────────
	//
	//  Dentro do asset.hpp, junto do AssetRecord que a carrega. Arquivo novo
	//  obrigaria a regerar o projeto pelo premake (o `files` usa glob, e glob e
	//  resolvido na hora de gerar) — mesmo motivo que levou o SkyLight para
	//  dentro do directional_light.hpp e o ShadingModelID para o
	//  material_cooked.hpp.
	//
	//  ── A REGRA QUE SEPARA O QUE ENTRA AQUI ────────────────────────────────
	//
	//  Só entra o que muda o ASSET IMPORTADO, e nao o que muda um USO dele.
	//  Escala e pivo sao do asset: toda entidade que usar a malha quer a mesma
	//  correcao. Posicao na cena e do uso. Confundir os dois e como o ambiente
	//  foi parar dentro da luz direcional.
	// ═══════════════════════════════════════════════════════════════════════
	struct AssetImportSettings
	{
		// ── Malha ──────────────────────────────────────────────────────────

		// Multiplica todos os vertices na importacao. 1 = como veio do DCC.
		//
		// Uniforme de proposito: escala nao-uniforme quebra as normais (elas
		// precisariam da inversa transposta) e transforma esfera de colisao em
		// elipsoide, que nenhum motor de fisica aceita. Quem precisa de
		// nao-uniforme quer isso no USO, nao no asset.
		float MeshScale = 1.0f;

		// Move os vertices para que o centro do bounding box caia na origem.
		//
		// Conserta pivo no canto — o defeito que obriga a compensar rotacao a
		// mao e que estraga qualquer encaixe em socket.
		bool RecenterPivot = false;

		// Assenta a base do bounding box em Y = 0 depois de recentrar.
		//
		// Para prop que fica no chao e o que se quer quase sempre: recentrar
		// sozinho deixa metade do objeto ENTERRADA. Os dois juntos dao
		// "centrado em XZ, apoiado no chao".
		bool DropToFloor = false;

		// ── Textura ────────────────────────────────────────────────────────
		//
		// Espelham o que o Asset Viewer ja mexia em tempo de execucao na fase
		// 1. A diferenca e que agora sobrevivem ao reabrir o motor — na fase 1
		// o ajuste morria com a sessao, que e meia funcionalidade.
		int TextureFilter = 2;   // 0 Nearest, 1 Linear, 2 Trilinear
		int TextureWrap = 0;     // 0 Repeat,  1 Clamp,  2 Mirror

		// ── ASSET_DEFAULTS_V1 — o que o asset leva consigo ao ser instanciado
		//
		// ── POR QUE ISTO E PROPRIEDADE DO ASSET ────────────────────────────
		//
		// "Toda vez que eu arrasto esta caixa para a cena eu troco o material
		// e desenho o mesmo collider a mao" e a definicao de uma propriedade
		// do ASSET vestida de tarefa repetitiva. Uma pistola tem um material e
		// tem uma forma de colisao; isso nao muda de instancia para instancia,
		// muda de asset para asset.
		//
		// Continua sendo so um PADRAO: o que e criado na cena e um
		// MaterialComponent e um ColliderComponent comuns, que o Inspector
		// edita como sempre. Mudar o padrao aqui nao mexe em nada que ja foi
		// colocado — do contrario o meta de um asset poderia alterar uma cena
		// salva pelas costas de quem a salvou.

		// Material aplicado ao instanciar. Vazio = usa o que veio do arquivo
		// (o material do proprio FBX), que e o comportamento de sempre.
		std::string DefaultMaterialUUID;

		// Forma do collider criado junto. -1 = nenhum.
		// Os outros valores sao o enum ColliderShape:
		//   0 Box, 1 Sphere, 2 Capsule, 3 Mesh, 4 ConvexHull
		//
		// int, e nao o enum: `asset.hpp` vive na camada de asset e nao conhece
		// fisica. Incluir physics_components.hpp aqui faria TODO consumidor de
		// AssetRecord arrastar o modulo de fisica junto.
		int CollisionShape = -1;

		// Folga em metros somada a cada lado do collider. Positivo alarga.
		//
		// Existe porque collider colado na malha prende em quina e em degrau:
		// o corpo encaixa milimetricamente no vao e trava. Uns poucos
		// centimetros de folga sao o que separa "anda" de "engancha".
		float CollisionPadding = 0.0f;

		// Collider que so DETECTA, sem empurrar. Zona de gatilho, area de
		// coleta, sensor de porta.
		bool CollisionIsTrigger = false;

		// Tudo no padrao? Serve para NAO gravar o bloco no `.axemeta` quando
		// nao ha nada a dizer: meta limpo continua limpo, e um diff no git so
		// aparece quando alguem realmente configurou alguma coisa.
		bool IsDefault() const
		{
			return MeshScale == 1.0f
				&& !RecenterPivot
				&& !DropToFloor
				&& TextureFilter == 2
				&& TextureWrap == 0
				&& DefaultMaterialUUID.empty()
				&& CollisionShape < 0
				&& CollisionPadding == 0.0f
				&& !CollisionIsTrigger;
		}
	};

	struct AssetRecord
	{
		std::string           UUID;
		std::filesystem::path FilePath;

		// ASSET_VIEWER_V2 — configuracao de importacao, lida e gravada no
		// `.axemeta` junto da identidade. Ver a nota na struct acima.
		AssetImportSettings   Import;
		AssetType             Type = AssetType::Unknown;
		std::string           Name;
		std::string           VirtualFolder = ""; // pasta virtual no browser

		// Apenas para Type == Script: subtipo lido do .axescript
		// Valores: "Entity", "Agent", "Character", "StaticObject", "Trigger"
		std::string           ScriptClassType;

		bool IsValid() const { return !UUID.empty() && !FilePath.empty(); }
	};

	// Pasta virtual do asset browser — pode ter cor e subpastas
	struct VirtualFolderDef
	{
		std::string Name;
		std::string Parent = "";  // "" = raiz
		uint32_t    Color = 0xFFFFFFFF; // RGBA
		bool        Expanded = true;
	};


} // namespace axe