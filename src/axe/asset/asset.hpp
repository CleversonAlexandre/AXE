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
	struct AssetRecord
	{
		std::string           UUID;
		std::filesystem::path FilePath;
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