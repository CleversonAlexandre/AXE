#pragma once
#include "axe/utils/glm_config.hpp"   // EDITOR_CAM_PERSIST_V1
#include "axe/core/types.hpp"
#include "scene.hpp"
#include "axe/scene/scene_environment.hpp"
#include <filesystem>
#include <string>
#include <functional>
#include <memory>
#include <map>

namespace axe
{
	class Shader;
	class Texture2D;

	class AXE_API SceneSerializer
	{
	public:
		// Salva a cena em formato .axescene
		static bool Serialize(const Scene& scene, const std::filesystem::path& filepath,
			const SceneEnvironment* env = nullptr);

		// Carrega uma cena de um .axescene
		static bool Deserialize(const std::filesystem::path& filepath, Scene& scene,
			SceneEnvironment* env = nullptr);

		// ═══════════════════════════════════════════════════════════════════
		//  EDITOR_CAM_PERSIST_V1 — a camera do editor viaja com a cena
		//
		//  A camera do editor NAO e uma entidade: ela vive no ViewportRenderer.
		//  Entao o serializer nao tem como alcanca-la, e o editor nao tem como
		//  escrever no JSON. Esta struct e a caixa de correio entre os dois.
		//
		//  Escolhi caixa de correio estatica em vez de mais um parametro no
		//  Serialize/Deserialize porque os dois ja tem 3 parametros e sao
		//  chamados de varios pontos — parametro novo e mais um lugar para um
		//  chamador ficar para tras em silencio (foi o que aconteceu com o
		//  EnvironmentComponent).
		//
		//  Fluxo: o editor preenche PendingEditorCamera antes de salvar; depois
		//  de carregar, le LoadedEditorCamera e aplica se Valid.
		//
		//  Guardamos a ORBITA (foco/distancia/pitch/yaw), nao a posicao: e o
		//  estado completo desta camera, e a posicao e derivada dele.
		struct EditorCameraState
		{
			glm::vec3 FocalPoint{ 0.0f };
			float     Distance = 10.0f;
			float     Pitch = 0.0f;
			float     Yaw = 0.0f;
			bool      Valid = false;   // false = cena antiga, nao mexer na camera
		};

		static EditorCameraState PendingEditorCamera;  // editor -> arquivo
		static EditorCameraState LoadedEditorCamera;   // arquivo -> editor

		static std::string SerializeToString(const Scene& scene);
		static bool DeserializeFromString(const std::string& data, Scene& scene);

		// Serialização por entity — para undo/redo de criar/deletar
		static std::string  SerializeEntity(entt::entity entity, const Scene& scene);
		static entt::entity DeserializeEntity(const std::string& data, Scene& scene);

		// Restaura um grupo de entities (ex: pasta + filhos) reconstruindo a hierarquia
		// Retorna a entity raiz (primeira da lista)
		static entt::entity DeserializeEntities(const std::vector<std::string>& snapshots, Scene& scene);

		using MaterialRecompileCallback =
			std::function<void(const std::string& assetUUID, Material* material)>;

		static void SetMaterialRecompileCallback(MaterialRecompileCallback cb)
		{
			s_MaterialRecompileCallback = cb;
		}

		static MaterialRecompileCallback GetMaterialRecompileCallback()
		{
			return s_MaterialRecompileCallback;
		}

		static MaterialRecompileCallback s_MaterialRecompileCallback;

		// Light Material usa um callback análogo: o MaterialCompiler
		// (CompileLightFunctionFromFile) vive no editor, então a engine não
		// resolve o shader direto — o editor registra esta callback. Retorna
		// true se conseguiu compilar; preenche shader + samplers.
		using LightMaterialRecompileCallback =
			std::function<bool(const std::string& assetUUID,
				std::shared_ptr<Shader>& outShader,
				std::map<std::string, std::shared_ptr<Texture2D>>& outSamplers)>;

		static void SetLightMaterialRecompileCallback(LightMaterialRecompileCallback cb)
		{
			s_LightMaterialRecompileCallback = cb;
		}

		static LightMaterialRecompileCallback GetLightMaterialRecompileCallback()
		{
			return s_LightMaterialRecompileCallback;
		}

		// Mesmo padrão do LightMaterial, pra material de partícula.
		using ParticleMaterialRecompileCallback = LightMaterialRecompileCallback;

		static void SetParticleMaterialRecompileCallback(ParticleMaterialRecompileCallback cb)
		{
			s_ParticleMaterialRecompileCallback = cb;
		}

		static ParticleMaterialRecompileCallback GetParticleMaterialRecompileCallback()
		{
			return s_ParticleMaterialRecompileCallback;
		}

		// POSTPROCESS_DOMAIN_V1 — mesma assinatura, terceiro dominio.
		//
		// Sem este slot, o MaterialShaderCache caia no callback de PARTICULA
		// para materiais de post process (o ternario dele nao tinha um terceiro
		// ramo) e devolvia um shader de billboard ao passe de tela cheia.
		using PostProcessMaterialRecompileCallback = LightMaterialRecompileCallback;

		static void SetPostProcessMaterialRecompileCallback(PostProcessMaterialRecompileCallback cb)
		{
			s_PostProcessMaterialRecompileCallback = cb;
		}

		static PostProcessMaterialRecompileCallback GetPostProcessMaterialRecompileCallback()
		{
			return s_PostProcessMaterialRecompileCallback;
		}

		static LightMaterialRecompileCallback    s_LightMaterialRecompileCallback;
		static ParticleMaterialRecompileCallback s_ParticleMaterialRecompileCallback;
		static PostProcessMaterialRecompileCallback s_PostProcessMaterialRecompileCallback;

	};

} // namespace axe