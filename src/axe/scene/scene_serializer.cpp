#include "scene_serializer.hpp"
#include "components.hpp"
#include "axe/script/script_component.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/mesh/mesh_factory.hpp"
#include "axe/mesh/mesh_loader.hpp"
#include "axe/log/log.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "axe/lighting/reflection_probe.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <imgui.h>
#include "axe/graphics/renderer/post_process_pass.hpp"
#include "axe/graphics/renderer/ssao_pass.hpp"
#include "axe/scene/scene_environment.hpp"

namespace axe
{

	using json = nlohmann::json;

	SceneSerializer::MaterialRecompileCallback SceneSerializer::s_MaterialRecompileCallback = nullptr;
	SceneSerializer::LightMaterialRecompileCallback    SceneSerializer::s_LightMaterialRecompileCallback = nullptr;
	SceneSerializer::ParticleMaterialRecompileCallback SceneSerializer::s_ParticleMaterialRecompileCallback = nullptr;

	// ══════════════════════════════════════════════════════════════════════
	//  Serialização CANÔNICA de uma entity — fonte ÚNICA da verdade.
	//  Antes a lógica por-componente estava copiada em 4 lugares (arquivo,
	//  snapshot, copiar/colar) e já tinha divergido: o snapshot não tinha os
	//  campos de spot/cookie/animação, NENHUM caminho tinha light material, e
	//  o arquivo não salvava Camera/Script/SpringArm. Centralizando aqui, um
	//  campo novo entra UMA vez e aparece em todos os caminhos.
	// ══════════════════════════════════════════════════════════════════════
	namespace
	{
		json SerializeEntityToJson(entt::entity entity, entt::registry& registry)
		{
			json e;
			e["id"] = (uint32_t)entity;
			json components;

			if (registry.any_of<FolderComponent>(entity))
			{
				auto& folder = registry.get<FolderComponent>(entity);
				components["Folder"]["color"] = { folder.Color.x, folder.Color.y, folder.Color.z, folder.Color.w };
			}

			if (auto* c = registry.try_get<NameComponent>(entity))
				components["Name"]["name"] = c->Name;

			if (auto* c = registry.try_get<TransformComponent>(entity))
			{
				components["Transform"]["position"] = { c->Data.Position.x, c->Data.Position.y, c->Data.Position.z };
				components["Transform"]["rotation"] = { c->Data.Rotation.x, c->Data.Rotation.y, c->Data.Rotation.z };
				components["Transform"]["scale"] = { c->Data.Scale.x,    c->Data.Scale.y,    c->Data.Scale.z };
			}

			if (auto* c = registry.try_get<MeshComponent>(entity))
				components["Mesh"]["uuid"] = c->AssetUUID;

			// So o UUID do .axeskel vai pro arquivo de cena. A malha, o
			// esqueleto e os clipes sao reconstruidos a partir do asset ao
			// carregar — e por isso que o asset precisa existir: sem ele, nao
			// haveria o que serializar alem de um caminho de FBX solto.
			if (auto* c = registry.try_get<SkeletalMeshComponent>(entity))
			{
				// Se o AssetUUID nao for um UUID de verdade (entidade criada por
				// um caminho antigo que gravava o PATH do fbx aqui), a cena
				// grava lixo e o load nao acha nada. Melhor gritar na hora de
				// SALVAR — quando ainda da pra consertar — do que descobrir no
				// proximo boot, com o personagem ja perdido.
				if (!c->Asset || c->AssetUUID.empty())
				{
					AXE_CORE_ERROR("SceneSerializer: a entidade tem SkeletalMesh mas NAO esta ligada a um "
						"asset .axeskel (AssetUUID='{}'). Ela NAO sera restaurada ao reabrir a cena. "
						"Arraste o .axeskel do Asset Browser para a cena.", c->AssetUUID);
				}

				components["SkeletalMesh"]["uuid"] = c->AssetUUID;
				components["SkeletalMesh"]["anim_graph"] = c->GraphAssetUUID;
				components["SkeletalMesh"]["clip"] = c->CurrentClip;
				components["SkeletalMesh"]["blend_time"] = c->BlendTime;
				components["SkeletalMesh"]["show_skeleton"] = c->ShowSkeleton;
			}

			if (auto* c = registry.try_get<MaterialComponent>(entity))
			{
				if (c->Data)
				{
					components["Material"]["material_asset_uuid"] = c->MaterialAssetUUID;
					components["Material"]["color"] = { c->Data->Color.r, c->Data->Color.g, c->Data->Color.b, c->Data->Color.a };
					components["Material"]["specular_strength"] = c->Data->SpecularStrength;
					components["Material"]["shininess"] = c->Data->Shininess;
					components["Material"]["metallic"] = c->Data->Metallic;
					components["Material"]["roughness"] = c->Data->Roughness;
					components["Material"]["ao"] = c->Data->AO;
					components["Material"]["use_pbr"] = c->Data->UsePBR;
					components["Material"]["albedo_uuid"] = c->Data->AlbedoUUID;
					components["Material"]["normal_uuid"] = c->Data->NormalUUID;
					components["Material"]["roughness_uuid"] = c->Data->RoughnessUUID;
					components["Material"]["metallic_uuid"] = c->Data->MetallicUUID;
					components["Material"]["ao_uuid"] = c->Data->AOUUID;
				}
			}

			if (auto* c = registry.try_get<LightComponent>(entity))
			{
				if (c->Data)
				{
					components["Light"]["direction"] = { c->Data->Direction.x, c->Data->Direction.y, c->Data->Direction.z };
					components["Light"]["color"] = { c->Data->Color.x, c->Data->Color.y, c->Data->Color.z };
					components["Light"]["intensity"] = c->Data->Intensity;
					components["Light"]["ambient"] = c->Data->AmbientStrength;
					components["Light"]["ibl_intensity"] = c->Data->IBLIntensity;
					components["Light"]["specular"] = c->Data->SpecularStrength;
					components["Light"]["shininess"] = c->Data->Shininess;
					components["Light"]["cookie_uuid"] = c->Data->CookieTextureUUID;
					components["Light"]["cookie_scale"] = c->Data->CookieScale;
					components["Light"]["light_material_uuid"] = c->Data->LightMaterialUUID;
				}
			}

			if (auto* c = registry.try_get<PointLightComponent>(entity))
			{
				if (c->Data)
				{
					components["PointLight"]["position"] = { c->Data->Position.x, c->Data->Position.y, c->Data->Position.z };
					components["PointLight"]["color"] = { c->Data->Color.x, c->Data->Color.y, c->Data->Color.z };
					components["PointLight"]["intensity"] = c->Data->Intensity;
					components["PointLight"]["radius"] = c->Data->Radius;
					components["PointLight"]["animated"] = c->Data->Animated;
					components["PointLight"]["anim_speed"] = c->Data->AnimSpeed;
					components["PointLight"]["anim_amplitude"] = c->Data->AnimAmplitude;
					components["PointLight"]["is_spot"] = c->Data->IsSpot;
					components["PointLight"]["direction"] = { c->Data->Direction.x, c->Data->Direction.y, c->Data->Direction.z };
					components["PointLight"]["inner_cone_angle"] = c->Data->InnerConeAngle;
					components["PointLight"]["outer_cone_angle"] = c->Data->OuterConeAngle;
					components["PointLight"]["cookie_uuid"] = c->Data->CookieTextureUUID;
					components["PointLight"]["light_material_uuid"] = c->Data->LightMaterialUUID;
				}
			}

			if (auto* c = registry.try_get<ProbeVolumeComponent>(entity))
			{
				auto& st = c->Settings;
				components["ProbeVolume"]["enabled"] = st.Enabled;
				components["ProbeVolume"]["resolution"] = { st.Resolution.x, st.Resolution.y, st.Resolution.z };
				components["ProbeVolume"]["intensity"] = st.Intensity;
				components["ProbeVolume"]["feather"] = st.Feather;
				components["ProbeVolume"]["bake_far_clip"] = st.BakeFarClip;
				components["ProbeVolume"]["show_probes"] = st.ShowProbes;
				components["ProbeVolume"]["occlude_sunlight"] = st.OccludeSunlight;
				components["ProbeVolume"]["bounces"] = st.Bounces;
				components["ProbeVolume"]["auto_bake_on_load"] = st.AutoBakeOnLoad;
				components["ProbeVolume"]["file_id"] = st.FileID;
				// O ProbeGrid (resultado do bake) NÃO é serializado — o
				// load dispara um rebake automático via BakeRequested.
			}

			if (auto* c = registry.try_get<ReflectionProbeComponent>(entity))
			{
				auto& st = c->Settings;
				components["ReflectionProbe"]["enabled"] = st.Enabled;
				components["ReflectionProbe"]["resolution"] = st.Resolution;
				components["ReflectionProbe"]["intensity"] = st.Intensity;
				components["ReflectionProbe"]["feather"] = st.Feather;
				components["ReflectionProbe"]["box_projection"] = st.BoxProjection;
				components["ReflectionProbe"]["bake_far_clip"] = st.BakeFarClip;
				// O cubemap capturado NÃO é serializado — recapturar é
				// barato (6 renders + prefilter), o load rebakeia sempre.
			}

			if (auto* c = registry.try_get<InteriorVolumeComponent>(entity))
			{
				components["InteriorVolume"]["enabled"] = c->Data.Enabled;
				components["InteriorVolume"]["intensity"] = c->Data.Intensity;
				components["InteriorVolume"]["blend_distance"] = c->Data.BlendDistance;
				components["InteriorVolume"]["affect_direct"] = c->Data.AffectDirect;
				components["InteriorVolume"]["affect_ambient"] = c->Data.AffectAmbient;
			}

			if (auto* c = registry.try_get<PostProcessComponent>(entity))
			{
				components["PostProcess"]["is_global"] = c->IsGlobal;
				components["PostProcess"]["exposure"] = c->Settings.Exposure;
				components["PostProcess"]["bloom_enabled"] = c->Settings.BloomEnabled;
				components["PostProcess"]["bloom_threshold"] = c->Settings.BloomThreshold;
				components["PostProcess"]["bloom_intensity"] = c->Settings.BloomIntensity;
				components["PostProcess"]["bloom_blur_passes"] = c->Settings.BloomBlurPasses;
				components["PostProcess"]["ssao_enabled"] = c->SSAO.Enabled;
				components["PostProcess"]["ssao_radius"] = c->SSAO.Radius;
				components["PostProcess"]["ssao_bias"] = c->SSAO.Bias;
				components["PostProcess"]["ssao_power"] = c->SSAO.Power;
				components["PostProcess"]["ssao_kernel"] = c->SSAO.KernelSize;
				// Fog
				auto& fog = c->Settings.Fog;
				components["PostProcess"]["fog_enabled"] = fog.Enabled;
				components["PostProcess"]["fog_color"] = { fog.FogColor.x, fog.FogColor.y, fog.FogColor.z };
				components["PostProcess"]["fog_density"] = fog.Density;
				components["PostProcess"]["fog_height_base"] = fog.HeightBase;
				components["PostProcess"]["fog_height_falloff"] = fog.HeightFalloff;
				components["PostProcess"]["fog_scatter"] = fog.ScatterStrength;
				components["PostProcess"]["fog_ambient"] = fog.AmbientStrength;
				components["PostProcess"]["fog_start"] = fog.FogStart;
				components["PostProcess"]["fog_end"] = fog.FogEnd;
				components["PostProcess"]["fog_steps"] = fog.Steps;
				components["PostProcess"]["fog_jitter"] = fog.StepJitter;
				// TAA
				auto& taa = c->Settings.TAA;
				components["PostProcess"]["taa_enabled"] = taa.Enabled;
				components["PostProcess"]["taa_blend"] = taa.BlendFactor;
				components["PostProcess"]["taa_sharpen"] = taa.Sharpen;
				components["PostProcess"]["taa_sharpen_amount"] = taa.SharpenAmount;
				components["PostProcess"]["taa_emissive_lum_min"] = taa.EmissiveLumMin;
				components["PostProcess"]["taa_emissive_lum_max"] = taa.EmissiveLumMax;
				components["PostProcess"]["taa_emissive_blend"] = taa.EmissiveBlendMax;
				components["PostProcess"]["taa_temporal_sens"] = taa.TemporalSensitivity;
				// SSR
				auto& ssr = c->Settings.SSR;
				components["PostProcess"]["ssr_enabled"] = ssr.Enabled;
				components["PostProcess"]["ssr_max_distance"] = ssr.MaxDistance;
				components["PostProcess"]["ssr_max_steps"] = ssr.MaxSteps;
				components["PostProcess"]["ssr_binary"] = ssr.BinaryRefine;
				components["PostProcess"]["ssr_thickness"] = ssr.Thickness;
				components["PostProcess"]["ssr_max_rough"] = ssr.MaxRoughness;
				components["PostProcess"]["ssr_intensity"] = ssr.Intensity;
				components["PostProcess"]["ssr_edge_fade"] = ssr.EdgeFade;
			}

			if (auto* c = registry.try_get<RigidbodyComponent>(entity))
			{
				components["Rigidbody"]["type"] = (int)c->Type;
				components["Rigidbody"]["mass"] = c->Mass;
				components["Rigidbody"]["friction"] = c->Friction;
				components["Rigidbody"]["restitution"] = c->Restitution;
				components["Rigidbody"]["linear_damping"] = c->LinearDamping;
				components["Rigidbody"]["angular_damping"] = c->AngularDamping;
				components["Rigidbody"]["use_gravity"] = c->UseGravity;
				components["Rigidbody"]["lock_rot_x"] = c->LockRotX;
				components["Rigidbody"]["lock_rot_y"] = c->LockRotY;
				components["Rigidbody"]["lock_rot_z"] = c->LockRotZ;
			}

			if (auto* c = registry.try_get<ColliderComponent>(entity))
			{
				components["Collider"]["shape"] = (int)c->Shape;
				components["Collider"]["offset"] = { c->Offset.x, c->Offset.y, c->Offset.z };
				components["Collider"]["half_extent"] = { c->HalfExtent.x, c->HalfExtent.y, c->HalfExtent.z };
				components["Collider"]["radius"] = c->Radius;
				components["Collider"]["height"] = c->Height;
				components["Collider"]["capsule_radius"] = c->CapsuleRadius;
				components["Collider"]["is_trigger"] = c->IsTrigger;
			}

			if (auto* c = registry.try_get<CharacterControllerComponent>(entity))
			{
				components["CharacterController"]["height"] = c->Height;
				components["CharacterController"]["radius"] = c->Radius;
				components["CharacterController"]["max_slope"] = c->MaxSlopeAngle;
				components["CharacterController"]["step_height"] = c->StepHeight;
				components["CharacterController"]["max_speed"] = c->MaxSpeed;
				components["CharacterController"]["jump_force"] = c->JumpForce;
			}

			if (auto* c = registry.try_get<CameraComponent>(entity))
			{
				components["Camera"]["fov"] = c->Fov;
				components["Camera"]["near"] = c->NearClip;
				components["Camera"]["far"] = c->FarClip;
				components["Camera"]["move_speed"] = c->MoveSpeed;
				components["Camera"]["sensitivity"] = c->Sensitivity;
				components["Camera"]["is_primary"] = c->IsPrimary;
			}

			if (auto* sa = registry.try_get<SpringArmComponent>(entity))
			{
				components["SpringArm"]["length"] = sa->Length;
				components["SpringArm"]["height_offset"] = sa->HeightOffset;
				components["SpringArm"]["socket_offset"] = { sa->SocketOffset.x, sa->SocketOffset.y, sa->SocketOffset.z };
				components["SpringArm"]["lag_speed"] = sa->LagSpeed;
				components["SpringArm"]["enable_lag"] = sa->EnableCameraLag;
				components["SpringArm"]["mouse_rotates"] = sa->MouseRotates;
			}

			if (auto* sc = registry.try_get<ScriptComponent>(entity))
			{
				components["Script"]["asset_path"] = sc->ScriptAssetPath;
				components["Script"]["dll_path"] = sc->DllPath;
				components["Script"]["name"] = sc->ScriptName;
				components["Script"]["compiled"] = sc->IsCompiled;
			}

			if (auto* c = registry.try_get<ParticleSystemComponent>(entity))
			{
				components["ParticleSystem"]["particle_asset_uuid"] = c->ParticleAssetUUID;
				components["ParticleSystem"]["playing"] = c->Playing;
			}

			if (auto* rel = registry.try_get<RelationshipComponent>(entity))
			{
				if (rel->Parent != entt::null)
					components["Relationship"]["parent"] = (uint32_t)rel->Parent;
				if (!rel->Children.empty())
				{
					json children = json::array();
					for (auto child : rel->Children)
						children.push_back((uint32_t)child);
					components["Relationship"]["children"] = children;
				}
			}

			e["components"] = components;
			return e;
		}

		// Aplica TODOS os componentes (menos Relationship, que é 2º passo do
		// caller via idMap) numa entity JÁ criada.
		void DeserializeEntityComponents(const json& components, entt::entity entity, entt::registry& registry)
		{
			if (components.contains("Folder"))
				if (auto* f = registry.try_get<FolderComponent>(entity))
				{
					auto& t = components["Folder"]["color"];
					f->Color = ImVec4(t[0], t[1], t[2], t[3]);
				}

			if (components.contains("Name"))
				if (auto* c = registry.try_get<NameComponent>(entity))
					c->Name = components["Name"]["name"];

			if (components.contains("Transform"))
			{
				auto& c = registry.get_or_emplace<TransformComponent>(entity);
				auto& t = components["Transform"];
				c.Data.Position = { t["position"][0], t["position"][1], t["position"][2] };
				c.Data.Rotation = { t["rotation"][0], t["rotation"][1], t["rotation"][2] };
				c.Data.Scale = { t["scale"][0],    t["scale"][1],    t["scale"][2] };
			}

			if (components.contains("SkeletalMesh"))
			{
				const auto& j = components["SkeletalMesh"];

				const std::string uuid = j.value("uuid", std::string{});

				// UUID VAZIO — a entidade tem SkeletalMeshComponent mas nunca
				// foi ligada a um asset .axeskel.
				//
				// Acontece com entidades criadas por caminhos que nao geram
				// asset (o menu antigo "Criar > Skeletal Mesh", por exemplo).
				// O componente e gravado, mas nao ha o que recarregar — e o
				// personagem some ao reabrir a cena, sem nenhuma pista.
				const AssetRecord* record = uuid.empty()
					? nullptr
					: AssetDatabase::Get().GetByUUID(uuid);

				if (uuid.empty())
				{
					// A entidade tem SkeletalMeshComponent mas nunca foi ligada
					// a um asset .axeskel. Acontece com entidades criadas por
					// caminhos que nao geram asset (o menu antigo
					// "Criar > Skeletal Mesh"). O componente e gravado, mas nao
					// ha o que recarregar — e o personagem some ao reabrir a
					// cena, sem nenhuma pista.
					AXE_CORE_ERROR("SceneSerializer: a entidade tem SkeletalMesh mas o AssetUUID esta VAZIO. "
						"Ela nao veio de um asset .axeskel — arraste o .axeskel do Asset Browser "
						"para a cena e salve de novo.");
				}
				else if (record && std::filesystem::exists(record->FilePath))
				{
					auto asset = SkeletalMeshAsset::LoadFromFile(record->FilePath);

					if (asset && asset->Resolve())
					{
						auto& sk = registry.emplace<SkeletalMeshComponent>(entity);
						sk.Asset = asset;
						sk.AssetUUID = uuid;
						sk.Data = asset->GetMesh();
						sk.Clips = asset->GetClips();

						// AnimGraph (opcional). Resolvido contra ESTE esqueleto —
						// e o que religa os clipes que o grafo referencia por nome.
						const std::string graphUuid = j.value("anim_graph", std::string{});

						if (!graphUuid.empty())
						{
							if (const AssetRecord* gr = AssetDatabase::Get().GetByUUID(graphUuid))
							{
								auto ga = AnimGraphAsset::LoadFromFile(gr->FilePath);

								if (ga && ga->Resolve(*asset))
								{
									sk.GraphAsset = ga;
									sk.GraphAssetUUID = graphUuid;
									sk.GraphInstance.SetAsset(ga);
								}
							}
							else
							{
								AXE_CORE_ERROR("SceneSerializer: AnimGraph de UUID '{}' nao encontrado — "
									"o personagem vai cair no clipe unico.", graphUuid);
							}
						}

						sk.CurrentClip = j.value("clip", -1);
						sk.BlendTime = j.value("blend_time", 0.2f);
						sk.ShowSkeleton = j.value("show_skeleton", false);

						// O clipe salvo pode nao existir mais (o usuario
						// removeu a animacao do asset). Cair na bind pose e
						// melhor do que indexar fora do vetor.
						if (sk.CurrentClip >= (int)sk.Clips.size())
							sk.CurrentClip = sk.Clips.empty() ? -1 : 0;
					}
				}
				else if (!record)
				{
					AXE_CORE_ERROR("SceneSerializer: o asset .axeskel de UUID '{}' nao esta no AssetDatabase. "
						"O arquivo foi movido/apagado, ou esta fora da pasta Assets do projeto?", uuid);
				}
				else
				{
					AXE_CORE_ERROR("SceneSerializer: o .axeskel existe no banco ('{}') mas o arquivo sumiu do disco.",
						record->FilePath.string());
				}
			}

			if (components.contains("Mesh"))
			{
				std::string uuid = components["Mesh"]["uuid"];

				if (MeshFactory::IsPrimitive(uuid))
				{
					auto& mc = registry.emplace<MeshComponent>(entity);
					mc.AssetUUID = uuid;
					mc.Data = MeshFactory::CreateByUUID(uuid);
				}
				else
				{
					const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);

					// ── MeshComponent apontando pra um ASSET DO ENGINE ────
					//
					// Entidade podre: um MeshComponent cujo asset e um
					// .axeskel/.axeanim (JSON). Nasceu de um bug ja corrigido
					// — arrastar um .axeskel caía no fallback de mesh estatica
					// — mas o caminho ficou GRAVADO na cena, e toda abertura
					// mandava JSON pro Assimp.
					//
					// Nao adianta so gritar: sem o NOME da entidade, o usuario
					// nao sabe qual apagar. Entao nomeamos a culpada E nao
					// criamos o componente — a cena se cura ao ser salva de
					// novo.
					// Pela EXTENSAO, nao por record->Type — um asset registrado
					// por build antigo tem Type velho no .axemeta, e a guarda
					// por Type deixaria o JSON passar pro MeshLoader (o erro
					// "No suitable reader found" / "unexpected colon").
					const std::string engineExt = record
						? record->FilePath.extension().string() : std::string{};

					const bool engineAsset =
						engineExt == ".axeskel" ||
						engineExt == ".axeanim" ||
						engineExt == ".axemat";

					if (engineAsset)
					{
						const std::string entityName =
							registry.all_of<NameComponent>(entity)
							? registry.get<NameComponent>(entity).Name
							: std::string("(sem nome)");

						AXE_CORE_ERROR("SceneSerializer: a entidade '{}' tem um MeshComponent apontando "
							"para '{}', que e um asset do engine ({}) e nao um modelo. "
							"O componente foi IGNORADO.",
							entityName, record->Name, AssetTypeToString(record->Type));

						AXE_CORE_ERROR("  -> Se for um personagem, apague a entidade '{}' e arraste o "
							".axeskel de novo. Salvar a cena remove o lixo.", entityName);
					}
					else if (record && std::filesystem::exists(record->FilePath))
					{
						auto& mc = registry.emplace<MeshComponent>(entity);
						mc.AssetUUID = uuid;
						mc.Data = MeshLoader::Load(record->FilePath.string()).MeshData;
					}
					else
					{
						auto& mc = registry.emplace<MeshComponent>(entity);
						mc.AssetUUID = uuid;
						AXE_CORE_WARN("SceneSerializer: asset '{}' não encontrado.", uuid);
					}
				}
			}

			if (components.contains("Material"))
			{
				auto& t = components["Material"];
				auto mat = std::make_shared<Material>(nullptr, "Material");
				mat->Color = { t["color"][0], t["color"][1], t["color"][2], t["color"][3] };
				mat->SpecularStrength = t["specular_strength"];
				mat->Shininess = t["shininess"];
				if (t.contains("metallic"))  mat->Metallic = t["metallic"];
				if (t.contains("roughness")) mat->Roughness = t["roughness"];
				if (t.contains("ao"))        mat->AO = t["ao"];
				if (t.contains("use_pbr"))   mat->UsePBR = t["use_pbr"];

				auto LoadTex = [&](const std::string& key, std::string& uuid, std::shared_ptr<Texture2D>& tex)
					{
						if (!t.contains(key)) return;
						uuid = t[key].get<std::string>();
						if (uuid.empty()) return;
						const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
						if (record && std::filesystem::exists(record->FilePath))
							tex = Texture2D::Create(record->FilePath.string());
						else
							AXE_CORE_WARN("SceneSerializer: textura '{}' não encontrada.", uuid);
					};
				LoadTex("albedo_uuid", mat->AlbedoUUID, mat->AlbedoMap);
				LoadTex("normal_uuid", mat->NormalUUID, mat->NormalMap);
				LoadTex("roughness_uuid", mat->RoughnessUUID, mat->RoughnessMap);
				LoadTex("metallic_uuid", mat->MetallicUUID, mat->MetallicMap);
				LoadTex("ao_uuid", mat->AOUUID, mat->AOMap);

				auto mc = MaterialComponent{ mat };
				if (t.contains("material_asset_uuid"))
				{
					mc.MaterialAssetUUID = t["material_asset_uuid"].get<std::string>();
					auto cb = SceneSerializer::GetMaterialRecompileCallback();
					if (cb && !mc.MaterialAssetUUID.empty())
						cb(mc.MaterialAssetUUID, mat.get());
				}
				registry.emplace<MaterialComponent>(entity, mc);
			}

			// Re-resolve do Light Material (shader/samplers) via callback do editor.
			auto ResolveLightMaterial = [&](const json& t, std::string& uuid,
				std::shared_ptr<Shader>& shader,
				std::map<std::string, std::shared_ptr<Texture2D>>& samplers)
				{
					uuid = t.value("light_material_uuid", std::string());
					if (uuid.empty()) return;
					auto cb = SceneSerializer::GetLightMaterialRecompileCallback();
					if (cb) cb(uuid, shader, samplers);
				};

			if (components.contains("Light"))
			{
				auto& t = components["Light"];
				auto light = std::make_shared<DirectionalLight>();
				light->Direction = { t["direction"][0], t["direction"][1], t["direction"][2] };
				light->Color = { t["color"][0], t["color"][1], t["color"][2] };
				light->Intensity = t["intensity"];
				light->AmbientStrength = t["ambient"];
				light->IBLIntensity = t.value("ibl_intensity", 1.0f);
				light->SpecularStrength = t["specular"];
				light->Shininess = t["shininess"];
				light->CookieScale = t.value("cookie_scale", 5.0f);
				std::string dirCookieUUID = t.value("cookie_uuid", std::string());
				if (!dirCookieUUID.empty())
				{
					const AssetRecord* record = AssetDatabase::Get().GetByUUID(dirCookieUUID);
					if (record)
					{
						light->CookieTexture = Texture2D::Create(record->FilePath.string());
						light->CookieTextureUUID = dirCookieUUID;
					}
				}
				ResolveLightMaterial(t, light->LightMaterialUUID, light->LightMaterialShader, light->LightMaterialSamplers);
				registry.emplace<LightComponent>(entity, light);
			}

			if (components.contains("PointLight"))
			{
				auto& t = components["PointLight"];
				auto pl = std::make_shared<PointLight>();
				pl->Position = { t["position"][0], t["position"][1], t["position"][2] };
				pl->Color = { t["color"][0], t["color"][1], t["color"][2] };
				pl->Intensity = t["intensity"];
				pl->Radius = t["radius"];
				pl->Animated = t.value("animated", false);
				pl->AnimSpeed = t.value("anim_speed", 2.0f);
				pl->AnimAmplitude = t.value("anim_amplitude", 0.3f);
				pl->IsSpot = t.value("is_spot", false);
				if (t.contains("direction"))
					pl->Direction = { t["direction"][0], t["direction"][1], t["direction"][2] };
				pl->InnerConeAngle = t.value("inner_cone_angle", 25.0f);
				pl->OuterConeAngle = t.value("outer_cone_angle", 35.0f);
				std::string ptCookieUUID = t.value("cookie_uuid", std::string());
				if (!ptCookieUUID.empty())
				{
					const AssetRecord* record = AssetDatabase::Get().GetByUUID(ptCookieUUID);
					if (record)
					{
						pl->CookieTexture = Texture2D::Create(record->FilePath.string());
						pl->CookieTextureUUID = ptCookieUUID;
					}
				}
				ResolveLightMaterial(t, pl->LightMaterialUUID, pl->LightMaterialShader, pl->LightMaterialSamplers);
				registry.emplace<PointLightComponent>(entity, pl);
			}

			if (components.contains("ProbeVolume"))
			{
				auto& t = components["ProbeVolume"];
				ProbeVolumeComponent pv;
				pv.Settings.Enabled = t.value("enabled", true);
				if (t.contains("resolution") && t["resolution"].size() == 3)
					pv.Settings.Resolution = { t["resolution"][0], t["resolution"][1], t["resolution"][2] };
				pv.Settings.Intensity = t.value("intensity", 1.0f);
				pv.Settings.Feather = t.value("feather", 1.0f);
				pv.Settings.BakeFarClip = t.value("bake_far_clip", 150.0f);
				pv.Settings.ShowProbes = t.value("show_probes", false);
				pv.Settings.OccludeSunlight = t.value("occlude_sunlight", true);
				pv.Settings.Bounces = t.value("bounces", 2);
				pv.Settings.AutoBakeOnLoad = t.value("auto_bake_on_load", true);
				pv.Settings.FileID = t.value("file_id", 0u);
				// Rebake automático ao abrir a cena — o grid nunca é salvo.
				// No Play/Stop, o EditorLayer restaura o grid antigo e
				// CANCELA esta flag logo depois do Deserialize (o Stop
				// não pode custar um bake inteiro).
				pv.BakeRequested = pv.Settings.Enabled && pv.Settings.AutoBakeOnLoad;
				registry.emplace<ProbeVolumeComponent>(entity, pv);
			}

			if (components.contains("ReflectionProbe"))
			{
				auto& t = components["ReflectionProbe"];
				ReflectionProbeComponent rp;
				rp.Settings.Enabled = t.value("enabled", true);
				rp.Settings.Resolution = t.value("resolution", 128);
				rp.Settings.Intensity = t.value("intensity", 1.0f);
				rp.Settings.Feather = t.value("feather", 0.5f);
				rp.Settings.BoxProjection = t.value("box_projection", true);
				rp.Settings.BakeFarClip = t.value("bake_far_clip", 150.0f);
				// Recaptura automática ao abrir — barato, sempre atual
				rp.BakeRequested = rp.Settings.Enabled;
				registry.emplace<ReflectionProbeComponent>(entity, rp);
			}

			if (components.contains("InteriorVolume"))
			{
				auto& t = components["InteriorVolume"];
				InteriorVolumeComponent iv;
				iv.Data.Enabled = t.value("enabled", true);
				iv.Data.Intensity = t.value("intensity", 1.0f);
				iv.Data.BlendDistance = t.value("blend_distance", 0.5f);
				iv.Data.AffectDirect = t.value("affect_direct", true);
				iv.Data.AffectAmbient = t.value("affect_ambient", true);
				registry.emplace<InteriorVolumeComponent>(entity, iv);
			}

			if (components.contains("PostProcess"))
			{
				auto& t = components["PostProcess"];
				PostProcessComponent pp;
				pp.IsGlobal = t["is_global"];
				pp.Settings.Exposure = t["exposure"];
				pp.Settings.BloomEnabled = t["bloom_enabled"];
				pp.Settings.BloomThreshold = t["bloom_threshold"];
				pp.Settings.BloomIntensity = t["bloom_intensity"];
				pp.Settings.BloomBlurPasses = t["bloom_blur_passes"];
				pp.SSAO.Enabled = t.value("ssao_enabled", false);
				pp.SSAO.Radius = t.value("ssao_radius", 0.5f);
				pp.SSAO.Bias = t.value("ssao_bias", 0.025f);
				pp.SSAO.Power = t.value("ssao_power", 2.0f);
				pp.SSAO.KernelSize = t.value("ssao_kernel", 64);
				// Fog
				auto& fog = pp.Settings.Fog;
				fog.Enabled = t.value("fog_enabled", false);
				fog.Density = t.value("fog_density", 0.04f);
				fog.HeightBase = t.value("fog_height_base", 0.0f);
				fog.HeightFalloff = t.value("fog_height_falloff", 0.15f);
				fog.ScatterStrength = t.value("fog_scatter", 0.6f);
				fog.AmbientStrength = t.value("fog_ambient", 0.15f);
				fog.FogStart = t.value("fog_start", 2.0f);
				fog.FogEnd = t.value("fog_end", 80.0f);
				fog.Steps = t.value("fog_steps", 12);
				fog.StepJitter = t.value("fog_jitter", 0.5f);
				if (t.contains("fog_color") && t["fog_color"].size() == 3)
					fog.FogColor = { t["fog_color"][0], t["fog_color"][1], t["fog_color"][2] };
				// TAA
				auto& taa = pp.Settings.TAA;
				taa.Enabled = t.value("taa_enabled", false);
				taa.BlendFactor = t.value("taa_blend", 0.1f);
				taa.Sharpen = t.value("taa_sharpen", false);
				taa.SharpenAmount = t.value("taa_sharpen_amount", 0.3f);
				taa.EmissiveLumMin = t.value("taa_emissive_lum_min", 0.2f);
				taa.EmissiveLumMax = t.value("taa_emissive_lum_max", 1.2f);
				taa.EmissiveBlendMax = t.value("taa_emissive_blend", 0.85f);
				taa.TemporalSensitivity = t.value("taa_temporal_sens", 4.0f);
				// SSR
				auto& ssr = pp.Settings.SSR;
				ssr.Enabled = t.value("ssr_enabled", false);
				ssr.MaxDistance = t.value("ssr_max_distance", 20.0f);
				ssr.MaxSteps = t.value("ssr_max_steps", 40);
				ssr.BinaryRefine = t.value("ssr_binary", 5);
				ssr.Thickness = t.value("ssr_thickness", 0.5f);
				ssr.MaxRoughness = t.value("ssr_max_rough", 0.6f);
				ssr.Intensity = t.value("ssr_intensity", 1.0f);
				ssr.EdgeFade = t.value("ssr_edge_fade", 0.1f);
				registry.emplace<PostProcessComponent>(entity, pp);
			}

			if (components.contains("Rigidbody"))
			{
				auto& t = components["Rigidbody"];
				RigidbodyComponent rb;
				rb.Type = (BodyType)t.value("type", 0);
				rb.Mass = t.value("mass", 1.0f);
				rb.Friction = t.value("friction", 0.5f);
				rb.Restitution = t.value("restitution", 0.0f);
				rb.LinearDamping = t.value("linear_damping", 0.05f);
				rb.AngularDamping = t.value("angular_damping", 0.05f);
				rb.UseGravity = t.value("use_gravity", true);
				rb.LockRotX = t.value("lock_rot_x", false);
				rb.LockRotY = t.value("lock_rot_y", false);
				rb.LockRotZ = t.value("lock_rot_z", false);
				registry.emplace<RigidbodyComponent>(entity, rb);
			}

			if (components.contains("Collider"))
			{
				auto& t = components["Collider"];
				ColliderComponent col;
				col.Shape = (ColliderShape)t.value("shape", 0);
				col.Offset = { t["offset"][0], t["offset"][1], t["offset"][2] };
				col.HalfExtent = { t["half_extent"][0], t["half_extent"][1], t["half_extent"][2] };
				col.Radius = t.value("radius", 0.5f);
				col.Height = t.value("height", 1.8f);
				col.CapsuleRadius = t.value("capsule_radius", 0.3f);
				col.IsTrigger = t.value("is_trigger", false);
				registry.emplace<ColliderComponent>(entity, col);
			}

			if (components.contains("CharacterController"))
			{
				auto& t = components["CharacterController"];
				CharacterControllerComponent cc;
				cc.Height = t.value("height", 1.8f);
				cc.Radius = t.value("radius", 0.3f);
				cc.MaxSlopeAngle = t.value("max_slope", 45.0f);
				cc.StepHeight = t.value("step_height", 0.3f);
				cc.MaxSpeed = t.value("max_speed", 5.0f);
				cc.JumpForce = t.value("jump_force", 5.0f);
				registry.emplace<CharacterControllerComponent>(entity, cc);
			}

			if (components.contains("Camera"))
			{
				auto& t = components["Camera"];
				CameraComponent cam;
				cam.Fov = t.value("fov", 60.0f);
				cam.NearClip = t.value("near", 0.1f);
				cam.FarClip = t.value("far", 1000.0f);
				cam.MoveSpeed = t.value("move_speed", 5.0f);
				cam.Sensitivity = t.value("sensitivity", 0.1f);
				cam.IsPrimary = t.value("is_primary", true);
				registry.emplace<CameraComponent>(entity, cam);
			}

			if (components.contains("SpringArm"))
			{
				auto& t = components["SpringArm"];
				SpringArmComponent sa;
				sa.Length = t.value("length", 5.0f);
				sa.HeightOffset = t.value("height_offset", 0.0f);
				sa.LagSpeed = t.value("lag_speed", 8.0f);
				sa.EnableCameraLag = t.value("enable_lag", true);
				sa.MouseRotates = t.value("mouse_rotates", true);
				if (t.contains("socket_offset") && t["socket_offset"].size() == 3)
					sa.SocketOffset = { t["socket_offset"][0], t["socket_offset"][1], t["socket_offset"][2] };
				registry.emplace<SpringArmComponent>(entity, sa);
			}

			if (components.contains("Script"))
			{
				auto& t = components["Script"];
				ScriptComponent sc;
				sc.ScriptAssetPath = t.value("asset_path", "");
				sc.DllPath = t.value("dll_path", "");
				sc.ScriptName = t.value("name", "");
				sc.IsCompiled = t.value("compiled", false);
				registry.emplace<ScriptComponent>(entity, sc);
			}

			if (components.contains("ParticleSystem"))
			{
				auto& t = components["ParticleSystem"];
				ParticleSystemComponent ps;
				ps.ParticleAssetUUID = t.value("particle_asset_uuid", "");
				ps.Playing = t.value("playing", true);

				if (!ps.ParticleAssetUUID.empty())
				{
					const AssetRecord* record = AssetDatabase::Get().GetByUUID(ps.ParticleAssetUUID);
					if (record && std::filesystem::exists(record->FilePath))
					{
						ps.Data = ParticleSystemAsset::LoadFromFile(record->FilePath);

						// Recompila o material de partícula de cada emitter via callback.
						// Mesmo padrão do LightMaterial — zero GL vaza pro axe.dll.
						if (ps.Data)
						{
							auto cb = SceneSerializer::GetParticleMaterialRecompileCallback();
							if (cb)
							{
								for (auto& emitter : ps.Data->Emitters)
								{
									if (emitter.ParticleMaterialUUID.empty()) continue;
									cb(emitter.ParticleMaterialUUID,
										emitter.ParticleMaterialShader,
										emitter.ParticleMaterialSamplers);
								}
							}
							ps.EmitterRuntimes.resize(ps.Data->Emitters.size());
						}
					}
					else
						AXE_CORE_WARN("SceneSerializer: ParticleSystem asset '{}' não encontrado.", ps.ParticleAssetUUID);
				}

				registry.emplace<ParticleSystemComponent>(entity, ps);
			}
		}
	} // anonymous namespace


	// Path do .axeprobes irmão do .axescene: "Cena.axescene" → "Cena.axeprobes"
	// (formato legado, um volume). Com multi-volume, cada volume usa o
	// próprio FileID: "Cena.a1b2c3d4.axeprobes".
	static std::filesystem::path ProbesPathFor(const std::filesystem::path& scenePath)
	{
		std::filesystem::path p = scenePath;
		p.replace_extension(".axeprobes");
		return p;
	}

	static std::filesystem::path ProbesPathFor(const std::filesystem::path& scenePath,
		uint32_t fileID)
	{
		char hex[16];
		std::snprintf(hex, sizeof(hex), "%08x", fileID);
		std::filesystem::path p = scenePath;
		p.replace_extension("");
		p += std::string(".") + hex + ".axeprobes";
		return p;
	}

	bool SceneSerializer::Serialize(const Scene& scene, const std::filesystem::path& filepath,
		const SceneEnvironment* env)
	{
		auto& registry = const_cast<Scene&>(scene).GetRegistry();

		// Gera FileID pros Probe Volumes que ainda não têm — PRECISA
		// acontecer antes do dump do JSON pra o id ir junto no .axescene
		// (o id identifica o "Cena.<id>.axeprobes" de cada volume).
		{
			static std::mt19937 s_Rng{ std::random_device{}() };
			for (auto entity : registry.view<ProbeVolumeComponent>())
			{
				auto& pvc = registry.get<ProbeVolumeComponent>(entity);
				while (pvc.Settings.FileID == 0)
					pvc.Settings.FileID = (uint32_t)s_Rng();
			}
		}

		json root;
		root["scene"]["name"] = filepath.stem().string();
		root["scene"]["version"] = "1.0";

		if (env)
		{
			root["scene"]["environment"]["hdri_path"] = env->SkyboxPath;
			root["scene"]["environment"]["skybox_rotation"] = env->SkyboxRotation;
		}

		json entities = json::array();

		for (auto entity : registry.storage<entt::entity>())
		{
			if (!registry.valid(entity)) continue;
			entities.push_back(SerializeEntityToJson(entity, registry));
		}

		root["entities"] = entities;

		// Cria as pastas se não existirem
		std::filesystem::create_directories(filepath.parent_path());

		std::ofstream file(filepath);
		if (!file.is_open())
		{
			AXE_CORE_ERROR("SceneSerializer: falha ao abrir '{}' para escrita.", filepath.string());
			return false;
		}

		file << root.dump(4);
		AXE_CORE_INFO("SceneSerializer: cena salva em '{}'", filepath.string());

		// ── .axeprobes — grids de Light Probes bakeados ──────────────────
		// Um arquivo POR VOLUME, nomeado pelo FileID persistente do volume
		// ("Cena.<id>.axeprobes"), salvo sempre que a cena é salva com o
		// grid válido em memória: o estado do GI acompanha o da cena.
		// O FileID é gerado no início do Serialize (antes do dump do
		// JSON), então aqui todo volume já tem id.
		{
			for (auto entity : registry.view<ProbeVolumeComponent>())
			{
				auto& pvc = registry.get<ProbeVolumeComponent>(entity);
				if (pvc.Settings.FileID == 0) continue; // sem id = nunca salvo
				auto path = ProbesPathFor(filepath, pvc.Settings.FileID);
				if (pvc.Grid && pvc.Grid->IsValid() && pvc.Grid->HasCPUData())
					SaveProbeGridToFile(path.string(), *pvc.Grid);
				else
				{
					// volume sem grid: remove arquivo órfão de save anterior
					std::error_code ec;
					std::filesystem::remove(path, ec);
				}
			}
			// higiene do formato legado de volume único
			std::error_code ec;
			std::filesystem::remove(ProbesPathFor(filepath), ec);
		}

		return true;
	}

	bool SceneSerializer::Deserialize(const std::filesystem::path& filepath, Scene& scene,
		SceneEnvironment* env)
	{
		if (!std::filesystem::exists(filepath))
		{
			AXE_CORE_ERROR("SceneSerializer: arquivo '{}' não encontrado.", filepath.string());
			return false;
		}

		std::ifstream file(filepath);
		if (!file.is_open()) return false;

		json root;
		try {
			root = json::parse(file);

			if (env && root["scene"].contains("environment"))
			{
				auto& envJson = root["scene"]["environment"];
				std::string hdriPath = envJson.value("hdri_path", "");
				env->SkyboxRotation = envJson.value("skybox_rotation", 0.0f);
				if (!hdriPath.empty() && std::filesystem::exists(hdriPath))
					env->LoadHDRI(hdriPath);
			}
		}

		catch (const json::exception& e)
		{
			AXE_CORE_ERROR("SceneSerializer: erro ao parsear '{}': {}", filepath.string(), e.what());
			return false;
		}

		auto& registry = scene.GetRegistry();

		// Mapa de ID antigo → entity nova
		std::unordered_map<uint32_t, entt::entity> idMap;

		// --- Passo 1: cria entities em ordem REVERSA do array e aplica os
		// componentes via caminho canônico (mesma lógica do snapshot). A
		// ordem reversa cancela a iteração reversa do entt -> hierarquia
		// estável também no load de disco. ---
		const auto& entitiesJson = root["entities"];
		for (auto it = entitiesJson.rbegin(); it != entitiesJson.rend(); ++it)
		{
			const auto& e = *it;
			uint32_t oldID = e["id"];
			const auto& components = e["components"];

			entt::entity entity;
			if (components.contains("Folder"))
				entity = scene.CreateFolder("Entity");
			else
				entity = scene.CreateEntity("Entity");

			idMap[oldID] = entity;

			DeserializeEntityComponents(components, entity, registry);
		}

		// --- Passo 2: reconstrói hierarquia ---
		for (const auto& e : root["entities"])
		{
			uint32_t oldID = e["id"];
			if (!idMap.count(oldID)) continue;

			entt::entity entity = idMap[oldID];
			const auto& components = e["components"];

			if (components.contains("Relationship"))
			{
				auto& rel = components["Relationship"];
				if (rel.contains("parent"))
				{
					uint32_t oldParent = rel["parent"];
					if (idMap.count(oldParent))
						scene.SetParent(entity, idMap[oldParent], false); // load: transform ja esta em local space
				}
			}
		}

		// ── .axeprobes — tenta abrir a cena com o GI já pronto ──────────
		// Para CADA volume: se existe "Cena.<FileID>.axeprobes" com a
		// resolução das Settings, substitui o rebake automático que o
		// load agendou — a cena abre instantânea. Resolução diferente ou
		// arquivo ausente = rebake automático segue valendo pra esse
		// volume. Compat: volume sem FileID (cena salva antes do multi-
		// volume) tenta o "Cena.axeprobes" legado uma única vez.
		{
			auto& reg = scene.GetRegistry();
			bool legacyTried = false;
			for (auto entity : reg.view<ProbeVolumeComponent>())
			{
				auto& pvc = reg.get<ProbeVolumeComponent>(entity);

				std::filesystem::path path;
				if (pvc.Settings.FileID != 0)
					path = ProbesPathFor(filepath, pvc.Settings.FileID);
				else if (!legacyTried)
				{
					path = ProbesPathFor(filepath); // formato legado
					legacyTried = true;
				}
				else continue;

				auto loaded = LoadProbeGridFromFile(path.string());
				if (loaded && loaded->IsValid()
					&& loaded->Resolution == pvc.Settings.Resolution)
				{
					pvc.Grid = loaded;
					pvc.BakeRequested = false;
				}
				else if (loaded)
				{
					AXE_CORE_INFO("SceneSerializer: '{}' com resolução "
						"{}x{}x{} difere das Settings ({}x{}x{}) — rebake automático.",
						path.string(),
						loaded->Resolution.x, loaded->Resolution.y, loaded->Resolution.z,
						pvc.Settings.Resolution.x, pvc.Settings.Resolution.y,
						pvc.Settings.Resolution.z);
				}
			}
		}

		//AXE_CORE_INFO("SceneSerializer: cena carregada de '{}'", filepath.string());
		return true;
	}

	std::string SceneSerializer::SerializeToString(const Scene& scene)
	{
		auto& registry = const_cast<Scene&>(scene).GetRegistry();

		json root;
		root["scene"]["name"] = "snapshot";
		root["scene"]["version"] = "1.0";

		json entities = json::array();

		for (auto entity : registry.storage<entt::entity>())
		{
			if (!registry.valid(entity)) continue;
			entities.push_back(SerializeEntityToJson(entity, registry));
		}

		root["entities"] = entities;
		return root.dump();
	}


	bool SceneSerializer::DeserializeFromString(const std::string& data, Scene& scene)
	{
		try
		{
			json root = json::parse(data);
			auto& registry = scene.GetRegistry();

			// Mapa de ID antigo -> entity nova
			std::unordered_map<uint32_t, entt::entity> idMap;

			// Passo 1: cria entities em ordem REVERSA do array e aplica os
			// componentes via caminho canônico. A ordem reversa cancela a
			// iteração reversa do entt, tornando o round-trip serializar->
			// recriar IDEMPOTENTE — sem isso a hierarquia embaralhava a cada
			// Play/Stop.
			const auto& entitiesJson = root["entities"];
			for (auto it = entitiesJson.rbegin(); it != entitiesJson.rend(); ++it)
			{
				const auto& e = *it;
				uint32_t oldID = e["id"];
				const auto& components = e["components"];

				entt::entity entity;
				if (components.contains("Folder"))
					entity = scene.CreateFolder("Folder");
				else
					entity = scene.CreateEntity("Entity");

				idMap[oldID] = entity;

				DeserializeEntityComponents(components, entity, registry);
			}

			// Passo 2: reconstrói hierarquia
			for (const auto& e : root["entities"])
			{
				uint32_t oldID = e["id"];
				if (!idMap.count(oldID)) continue;
				entt::entity entity = idMap[oldID];
				const auto& components = e["components"];

				if (components.contains("Relationship"))
				{
					auto& rel = components["Relationship"];
					if (rel.contains("parent"))
					{
						uint32_t oldParent = rel["parent"];
						if (idMap.count(oldParent))
							scene.SetParent(entity, idMap[oldParent], false); // load: transform ja esta em local space
					}
				}
			}

			AXE_CORE_INFO("SceneSerializer: snapshot restaurado.");
			return true;
		}
		catch (const json::exception& e)
		{
			AXE_CORE_ERROR("SceneSerializer: erro ao deserializar snapshot: {}", e.what());
			return false;
		}
	}


	std::string SceneSerializer::SerializeEntity(entt::entity entity, const Scene& scene)
	{
		auto& registry = const_cast<Scene&>(scene).GetRegistry();
		if (!registry.valid(entity)) return "";

		return SerializeEntityToJson(entity, registry).dump();
	}

	entt::entity SceneSerializer::DeserializeEntity(const std::string& data, Scene& scene)
	{
		if (data.empty()) return entt::null;

		try
		{
			json e = json::parse(data);
			auto& registry = scene.GetRegistry();
			const auto& components = e["components"];

			entt::entity entity;
			if (components.contains("Folder"))
				entity = scene.CreateFolder("Folder");
			else
				entity = scene.CreateEntity("Entity");

			DeserializeEntityComponents(components, entity, registry);

			return entity;
		}
		catch (const json::exception& ex)
		{
			AXE_CORE_ERROR("SceneSerializer::DeserializeEntity falhou: {}", ex.what());
			return entt::null;
		}
	}

	entt::entity SceneSerializer::DeserializeEntities(
		const std::vector<std::string>& snapshots, Scene& scene)
	{
		if (snapshots.empty()) return entt::null;

		auto& registry = scene.GetRegistry();

		// Mapa de ID antigo → entity nova
		std::unordered_map<uint32_t, entt::entity> idMap;

		// Passo 1 — cria todas as entities sem hierarquia
		for (const auto& snap : snapshots)
		{
			if (snap.empty()) continue;
			try
			{
				json e = json::parse(snap);
				uint32_t oldID = e["id"];
				const auto& components = e["components"];

				entt::entity entity;
				if (components.contains("Folder"))
					entity = scene.CreateFolder("Folder");
				else
					entity = scene.CreateEntity("Entity");

				idMap[oldID] = entity;

				DeserializeEntityComponents(components, entity, registry);
			}
			catch (...) {}
		}

		// Passo 2 — reconstrói hierarquia com IDs mapeados
		for (const auto& snap : snapshots)
		{
			if (snap.empty()) continue;
			try
			{
				json e = json::parse(snap);
				uint32_t oldID = e["id"];
				if (!idMap.count(oldID)) continue;

				entt::entity entity = idMap[oldID];
				const auto& components = e["components"];

				if (components.contains("Relationship"))
				{
					auto& rel = components["Relationship"];
					// Só seta parent se o pai está no mesmo grupo restaurado
					if (rel.contains("parent"))
					{
						uint32_t oldParent = rel["parent"];
						if (idMap.count(oldParent))
							scene.SetParent(entity, idMap[oldParent], false); // load: transform ja esta em local space
					}
				}
			}
			catch (...) {}
		}

		// Retorna a primeira entity (raiz do grupo)
		if (!idMap.empty())
		{
			try
			{
				json first = json::parse(snapshots[0]);
				uint32_t firstID = first["id"];
				if (idMap.count(firstID))
					return idMap[firstID];
			}
			catch (...) {}
		}

		return entt::null;
	}

} // namespace axe