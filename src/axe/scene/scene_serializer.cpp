#include "scene_serializer.hpp"
#include "components.hpp"
#include "axe/script/script_component.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/project/project_manager.hpp"   // PKG3 — raiz para resolver o HDRI
#include "axe/mesh/mesh_factory.hpp"
#include "axe/mesh/mesh_cooked.hpp"   // B2.1
#include "axe/asset/asset_import_hooks.hpp"   // B2.4
#include "axe/material/material_cooked.hpp"   // B4 — shader cozido (.axeshader)
#include "axe/log/log.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "axe/audio/audio_source_component.hpp"
#include "axe/audio/audio_listener_component.hpp"
#include "axe/lighting/reflection_probe.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include "axe/graphics/renderer/post_process_pass.hpp"
#include "axe/graphics/renderer/ssao_pass.hpp"
#include "axe/scene/scene_environment.hpp"

namespace axe
{

	using json = nlohmann::json;

	SceneSerializer::MaterialRecompileCallback SceneSerializer::s_MaterialRecompileCallback = nullptr;
	SceneSerializer::LightMaterialRecompileCallback    SceneSerializer::s_LightMaterialRecompileCallback = nullptr;
	SceneSerializer::ParticleMaterialRecompileCallback SceneSerializer::s_ParticleMaterialRecompileCallback = nullptr;
	// POSTPROCESS_DOMAIN_V1
	SceneSerializer::PostProcessMaterialRecompileCallback SceneSerializer::s_PostProcessMaterialRecompileCallback = nullptr;
	// VOLUME_DOMAIN_V1
	SceneSerializer::VolumeMaterialRecompileCallback      SceneSerializer::s_VolumeMaterialRecompileCallback = nullptr;

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

					// ── SHADOW_PERSIST_V1 ────────────────────────────────────
					//
					// Estes tres NUNCA foram gravados. Quem ajustava Shadow
					// Distance ou Bias no Inspector via a mudanca em tela,
					// salvava a cena, reabria — e encontrava o valor padrao de
					// volta, sem erro nenhum para explicar.
					//
					// Nao e um bug do renderer, e por isso passou tanto tempo:
					// a imagem estava certa o tempo todo, so o arquivo e que
					// nao guardava o que a fez ficar certa.
					components["Light"]["cast_shadows"] = c->Data->CastShadows;
					components["Light"]["shadow_distance"] = c->Data->ShadowDistance;
					components["Light"]["shadow_bias"] = c->Data->ShadowBias;

					// PCSS_V1
					components["Light"]["sun_angular_deg"] = c->Data->SunAngularDegrees;
					components["Light"]["max_penumbra_texels"] = c->Data->MaxPenumbraTexels;

					// ── SKY_PERSIST_V1 ───────────────────────────────────────
					//
					// O CEU PROCEDURAL INTEIRO nunca foi gravado. Ligar "Ativar
					// Ceu Procedural", ajustar hora, turbidez e nuvens, salvar
					// e reabrir devolvia tudo ao padrao — com o ceu DESLIGADO.
					//
					// Consequencia que passou despercebida: o SKY_IBL_V1, que
					// so age com o ceu procedural ligado, era impossivel de
					// manter numa cena salva. A luz de ambiente vinda do ceu
					// existia e nunca sobrevivia a um reload.
					//
					// AmbientShadowFactor entra pelo mesmo motivo: e ele que
					// decide se a sombra fica preta (interior) ou aberta ao
					// ambiente (exterior), e voltava ao padrao a cada reload.
					components["Light"]["ambient_shadow_factor"] = c->Data->AmbientShadowFactor;

					components["Light"]["contact_shadow_length"] = c->Data->ContactShadowLength;
					components["Light"]["procedural_sky"] = c->Data->ProceduralSky;
					components["Light"]["tod_enabled"] = c->Data->TimeOfDayEnabled;
					components["Light"]["tod_hour"] = c->Data->Hour;
					components["Light"]["tod_speed"] = c->Data->DaySpeed;
					components["Light"]["sun_latitude"] = c->Data->SunLatitude;
					components["Light"]["turbidity"] = c->Data->Turbidity;
					components["Light"]["cloud_coverage"] = c->Data->CloudCoverage;
					components["Light"]["cloud_speed"] = c->Data->CloudSpeed;
					components["Light"]["cloud_color"] = {
						c->Data->CloudColor.x, c->Data->CloudColor.y, c->Data->CloudColor.z };
					components["Light"]["night_color"] = {
						c->Data->NightColor.x, c->Data->NightColor.y, c->Data->NightColor.z };
				}
			}

			// ── ENV_PERSIST_V1 — o EnvironmentComponent NUNCA foi salvo ──
			//
			// Nem HDRIPath, nem SkyboxRotation. A cena guardava o HDRI no
			// bloco "environment" do topo (que descreve o SceneEnvironment do
			// RENDER), mas o COMPONENTE da entidade voltava vazio a cada load
			// — e e ele que o viewport_renderer le todo frame para sincronizar.
			// Resultado: o que ele configurava na entidade Environment nao
			// sobrevivia a reabrir o projeto.
			if (auto* c = registry.try_get<EnvironmentComponent>(entity))
			{
				components["Environment"]["hdri_path"] = c->HDRIPath;
				components["Environment"]["skybox_rotation"] = c->SkyboxRotation;
				components["Environment"]["use_hdri"] = c->UseHDRI;
			}

			// ── SKY_LIGHT_V1 ─────────────────────────────────────────────
			if (auto* c = registry.try_get<SkyLightComponent>(entity))
			{
				if (c->Data)
				{
					components["SkyLight"]["enabled"] = c->Data->Enabled;
					components["SkyLight"]["intensity"] = c->Data->Intensity;
					components["SkyLight"]["color"] = {
						c->Data->Color.x, c->Data->Color.y, c->Data->Color.z };
					components["SkyLight"]["shadow_factor"] = c->Data->ShadowFactor;
					components["SkyLight"]["constant_ambient"] = c->Data->ConstantAmbient;

					// SKY_OWNS_SKY_V1 — configuracao do ceu, que veio do
					// DirectionalLight. A presenca da chave "procedural_sky"
					// e o que o loader usa para saber se este SkyLight ja e
					// da versao nova (ver a migracao no fim do Deserialize).
					components["SkyLight"]["sun_dependent"] = c->Data->SunDependent;
					components["SkyLight"]["procedural_sky"] = c->Data->ProceduralSky;
					components["SkyLight"]["turbidity"] = c->Data->Turbidity;
					components["SkyLight"]["cloud_coverage"] = c->Data->CloudCoverage;
					components["SkyLight"]["cloud_speed"] = c->Data->CloudSpeed;
					components["SkyLight"]["clouds_soften_sun"] = c->Data->CloudsSoftenSun;
					components["SkyLight"]["cloud_color"] = {
						c->Data->CloudColor.x, c->Data->CloudColor.y, c->Data->CloudColor.z };
					components["SkyLight"]["night_color"] = {
						c->Data->NightColor.x, c->Data->NightColor.y, c->Data->NightColor.z };
					components["SkyLight"]["tod_enabled"] = c->Data->TimeOfDayEnabled;
					components["SkyLight"]["tod_hour"] = c->Data->Hour;
					components["SkyLight"]["tod_speed"] = c->Data->DaySpeed;
					components["SkyLight"]["sun_latitude"] = c->Data->SunLatitude;
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

				// POSTPROCESS_DOMAIN_V1 — material de efeito de tela inteira.
				components["PostProcess"]["user_material"] = c->Settings.UserMaterialUUID;
				components["PostProcess"]["user_blend_point"] = (int)c->Settings.UserBlendPoint;
				components["PostProcess"]["user_intensity"] = c->Settings.UserIntensity;
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
				// VOLUME_DOMAIN_V1
				components["PostProcess"]["fog_material"] = fog.MaterialUUID;
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
				components["CharacterController"]["orient_to_movement"] = c->OrientRotationToMovement;
				components["CharacterController"]["rotation_rate"] = c->RotationRate;
				components["CharacterController"]["show_debug"] = c->ShowDebug;
				components["CharacterController"]["capsule_offset"] =
				{ c->CapsuleOffset.x, c->CapsuleOffset.y, c->CapsuleOffset.z };
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

			// Caminho. Os pontos sao LOCAIS a entidade — ver SplineComponent.
			if (auto* sp = registry.try_get<SplineComponent>(entity))
			{
				auto pts = nlohmann::json::array();
				for (const auto& p : sp->Points)
					pts.push_back({ p.x, p.y, p.z });

				components["Spline"]["points"] = pts;
				components["Spline"]["closed"] = sp->Closed;
				components["Spline"]["always_visible"] = sp->AlwaysVisible;
			}

			// Cutscene. Ver SequencePlayerComponent: os campos que comecam com
			// `_` sao estado vivo e NAO entram aqui de proposito — gravar
			// "_Playing" faria a cena carregar com uma cutscene ja rodando.
			if (auto* sp = registry.try_get<SequencePlayerComponent>(entity))
			{
				components["SequencePlayer"]["sequence_uuid"] = sp->SequenceUUID;
				components["SequencePlayer"]["play_on_start"] = sp->PlayOnStart;
				components["SequencePlayer"]["loop"] = sp->Loop;
				components["SequencePlayer"]["camera_cut"] = sp->CameraCut;
			}

			if (auto* sa = registry.try_get<SpringArmComponent>(entity))
			{
				components["SpringArm"]["length"] = sa->Length;
				components["SpringArm"]["height_offset"] = sa->HeightOffset;
				components["SpringArm"]["socket_offset"] = { sa->SocketOffset.x, sa->SocketOffset.y, sa->SocketOffset.z };
				components["SpringArm"]["lag_speed"] = sa->LagSpeed;
				components["SpringArm"]["enable_lag"] = sa->EnableCameraLag;
				components["SpringArm"]["mouse_rotates"] = sa->MouseRotates;
				// BP_CAMERA_V1
				components["SpringArm"]["rig_mode"] = (int)sa->Mode;
				components["SpringArm"]["fixed_yaw"] = sa->FixedYaw;
				components["SpringArm"]["fixed_pitch"] = sa->FixedPitch;
			}

			if (auto* sc = registry.try_get<ScriptComponent>(entity))
			{
				// ── UUID e a referencia; o caminho e diagnostico ─────────────
				//
				// `asset_path` e ABSOLUTO (`C:\\Users\\...\\BP_Player.axescript`),
				// porque vem de AssetRecord::FilePath. Isso quebra em dois
				// cenarios reais: abrir o projeto em outra maquina, e o jogo
				// EMPACOTADO, onde a estrutura de pastas e outra. Todo script
				// anexado a uma entidade viraria referencia morta.
				//
				// E o mesmo motivo pelo qual o `dll_path` saiu daqui no SC4 —
				// ver a nota logo abaixo. O asset_path tinha o mesmo defeito e
				// ficou para tras.
				//
				// Os DOIS sao gravados de proposito:
				//
				//   asset_uuid — a referencia de verdade, portatil, lida
				//                primeiro no load.
				//   asset_path — nao e mais lido quando ha UUID. Fica porque um
				//                `.axescene` aberto no editor de texto deve
				//                dizer QUAL script e aquele; um UUID sozinho
				//                torna o arquivo ilegivel para depuracao. E
				//                serve de ultimo recurso se o UUID sumir do
				//                banco.
				{
					std::string scriptUuid;

					if (!sc->ScriptAssetPath.empty())
						if (const AssetRecord* rec = AssetDatabase::Get().GetByPath(sc->ScriptAssetPath))
							scriptUuid = rec->UUID;

					components["Script"]["asset_uuid"] = scriptUuid;
					components["Script"]["asset_path"] = sc->ScriptAssetPath;
				}
				// SC4 — dll_path NAO e mais gravado. E dado derivado (depende de
				// onde o projeto esta no disco desta maquina), e ScriptWorld o
				// reconstroi via ScriptPaths::ResolveDll no inicio da cena. Mesma
				// regra que ja vale para Capture/Grid dos probes: o que da para
				// refazer no load nao vai para o arquivo. Cenas antigas continuam
				// carregando — o campo e simplesmente ignorado na leitura.
				components["Script"]["name"] = sc->ScriptName;
				components["Script"]["compiled"] = sc->IsCompiled;
			}

			if (auto* c = registry.try_get<ParticleSystemComponent>(entity))
			{
				components["ParticleSystem"]["particle_asset_uuid"] = c->ParticleAssetUUID;
				components["ParticleSystem"]["playing"] = c->Playing;
			}

			if (auto* c = registry.try_get<AudioSourceComponent>(entity))
			{
				// So o que foi AUTORADO. `Data` e um cache e `_Voice` e
				// estado de execucao — gravar qualquer um deles poria no
				// .axescene um dado que so faz sentido dentro da sessao que
				// o produziu.
				components["AudioSource"]["clip_uuid"] = c->ClipAssetUUID;
				components["AudioSource"]["volume"] = c->Volume;
				components["AudioSource"]["pitch"] = c->Pitch;
				components["AudioSource"]["loop"] = c->Loop;
				components["AudioSource"]["play_on_start"] = c->PlayOnStart;
				components["AudioSource"]["is_3d"] = c->Is3D;
				components["AudioSource"]["min_distance"] = c->MinDistance;
				components["AudioSource"]["max_distance"] = c->MaxDistance;
				components["AudioSource"]["category"] = SoundCategoryToString(c->Category);
				components["AudioSource"]["bus"] = AudioBusToString(c->Bus);
				components["AudioSource"]["priority"] = c->Priority;
				components["AudioSource"]["doppler"] = c->DopplerFactor;
				components["AudioSource"]["fade_in"] = c->FadeInTime;
				components["AudioSource"]["fade_out"] = c->FadeOutTime;
			}

			if (auto* c = registry.try_get<AudioListenerComponent>(entity))
			{
				components["AudioListener"]["is_primary"] = c->IsPrimary;
				components["AudioListener"]["use_camera_orientation"] = c->UseCameraOrientation;
			}

			// SC43 — anexo a socket. O Target e um id de ENTIDADE, entao ele
			// so pode ser religado no segundo passo (idMap), junto com o
			// parent — ver ApplySocketAttachmentTargets abaixo.
			if (auto* att = registry.try_get<SocketAttachmentComponent>(entity))
			{
				components["SocketAttachment"]["socket"] = att->SocketName;

				if (att->Target != entt::null)
					components["SocketAttachment"]["target"] = (uint32_t)att->Target;
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

		// ── SC43: religa o Target do anexo ───────────────────────────────
		//
		// Mesmo problema do parent: o id gravado e o da SESSAO em que a cena
		// foi salva, e o EnTT nao promete reproduzi-lo. So depois que todas
		// as entidades existem e o idMap esta completo da para traduzir.
		//
		// Alvo fora do idMap (a arma foi salva, o personagem nao — copiar so
		// a arma para outra cena, por exemplo) deixa Target nulo: o anexo
		// fica inerte e a entidade vira um objeto solto, em vez de apontar
		// para um handle reciclado que hoje e outra coisa qualquer.
		static void ApplySocketAttachmentTarget(Scene& scene, entt::entity entity,
			const nlohmann::json& components,
			const std::unordered_map<uint32_t, entt::entity>& idMap)
		{
			if (!components.contains("SocketAttachment"))
				return;

			const auto& j = components["SocketAttachment"];

			auto& att = scene.GetRegistry().get_or_emplace<SocketAttachmentComponent>(entity);
			att.SocketName = j.value("socket", std::string{});
			att.Target = entt::null;
			att._Valid = false;
			att._BoneIndex = -1;
			att._ResolvedFor.clear();

			if (!j.contains("target"))
				return;

			const uint32_t oldTarget = j["target"];
			const auto it = idMap.find(oldTarget);

			if (it != idMap.end())
				att.Target = it->second;
			else
				AXE_CORE_WARN("SceneSerializer: anexo ao socket '{}' perdeu o alvo "
					"(entidade {} nao esta nesta cena).", att.SocketName, oldTarget);
		}

		// Aplica TODOS os componentes (menos Relationship, que é 2º passo do
		// caller via idMap) numa entity JÁ criada.
		// ── SKY_OWNS_SKY_V1 ──────────────────────────────────────────────────
		//
		// Sinaliza que a cena carregada tem um SkyLight da versao ANTERIOR (sem
		// os campos de ceu). Precisa ser estatica de arquivo porque quem
		// descobre isso e o loader de componentes — uma funcao livre, chamada
		// tanto pelo Deserialize quanto pelo DeserializeEntities (colar de
		// entidades) — e quem age e a migracao no fim do Deserialize. Passar
		// por parametro obrigaria a mudar a assinatura de todo o caminho,
		// inclusive de um chamador que nao tem nada a ver com migracao.
		//
		// Sempre ZERADA no inicio do Deserialize: sem isso, um paste de
		// entidades poderia deixar a flag ligada e disparar uma migracao na
		// proxima cena aberta.
		static bool s_SkyNeedsSkyFieldsMigration = false;

		// SKY_OFF_V1 -> ENV_PERSIST_V1: a static que segurava o use_hdri ate o
		// componente existir SAIU. Ela so era necessaria porque o
		// EnvironmentComponent nao era serializado; agora ele e, com os tres
		// campos juntos, e o valor chega pelo caminho normal de componente.

		void DeserializeEntityComponents(const json& components, entt::entity entity, entt::registry& registry)
		{
			if (components.contains("Folder"))
				if (auto* f = registry.try_get<FolderComponent>(entity))
				{
					auto& t = components["Folder"]["color"];
					f->Color = glm::vec4(t[0], t[1], t[2], t[3]);   // S0a
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

						// B2.1 — cozido primeiro; FBX so se nao houver.
						mc.Data = MeshCooked::TryLoadFor(record->FilePath);

						// B2.4 — importador registrado (editor) ou nada (jogo).
						if (!mc.Data)
							mc.Data = AssetImportHooks::ImportMesh(record->FilePath);
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

					// EDITOR: o callback recompila do grafo — fonte da verdade,
					// porque la o `.axegraph` pode ter mudado desde o ultimo
					// cozimento. JOGO: callback nulo; o shader vem COZIDO do
					// `.axeshader` que o editor gravou no compile (B4 — mesmo
					// desenho do `.axemesh`/`.axeskelbin`).
					auto cb = SceneSerializer::GetMaterialRecompileCallback();
					if (cb && !mc.MaterialAssetUUID.empty())
						cb(mc.MaterialAssetUUID, mat.get());
					else if (!mc.MaterialAssetUUID.empty())
					{
						if (!CookedMaterial::LoadAndApply(mc.MaterialAssetUUID, *mat))
							AXE_CORE_WARN("SceneSerializer: material '{}' sem .axeshader "
								"cozido - abra a cena no editor (ou compile o material) "
								"para gerar, e reempacote.", mc.MaterialAssetUUID);
					}
				}
				registry.emplace<MaterialComponent>(entity, mc);
			}

			// Re-resolve do Light Material (shader/samplers).
			//
			// EDITOR: o callback recompila a light function do grafo. JOGO:
			// callback nulo, e o shader vem do `.axeshader` cozido no dominio
			// LightFunction (PKG9 — mesmo desenho do material de superficie do
			// B4). Sem isto, a luz ficava com Color/Intensity dos defaults no
			// jogo, e parte da diferenca de iluminacao entre editor e game.exe
			// vinha exatamente daqui.
			auto ResolveLightMaterial = [&](const json& t, std::string& uuid,
				std::shared_ptr<Shader>& shader,
				std::map<std::string, std::shared_ptr<Texture2D>>& samplers)
				{
					uuid = t.value("light_material_uuid", std::string());
					if (uuid.empty()) return;

					auto cb = SceneSerializer::GetLightMaterialRecompileCallback();

					if (cb)
					{
						cb(uuid, shader, samplers);
						return;
					}

					if (!CookedMaterial::LoadShaderAndSamplers(uuid,
						CookedMaterialDomain::LightFunction, shader, samplers))
					{
						AXE_CORE_WARN("SceneSerializer: light material '{}' sem .axeshader "
							"cozido - a luz vai usar os valores padrao. Abra a cena no "
							"editor para gerar e reempacote.", uuid);
					}
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

				// SHADOW_PERSIST_V1 — defaults iguais aos do struct, para cena
				// gravada antes disto abrir exatamente como abria.
				light->CastShadows = t.value("cast_shadows", true);
				light->ShadowDistance = t.value("shadow_distance", 50.0f);
				light->ShadowBias = t.value("shadow_bias", 0.005f);

				// PCSS_V1
				light->SunAngularDegrees = t.value("sun_angular_deg", 0.53f);
				light->MaxPenumbraTexels = t.value("max_penumbra_texels", 16.0f);

				// SKY_PERSIST_V1 — defaults iguais aos do struct.
				light->AmbientShadowFactor = t.value("ambient_shadow_factor", 1.0f);

				light->ContactShadowLength = t.value("contact_shadow_length", 0.15f);
				light->ProceduralSky = t.value("procedural_sky", false);
				light->TimeOfDayEnabled = t.value("tod_enabled", false);
				light->Hour = t.value("tod_hour", 12.0f);
				light->DaySpeed = t.value("tod_speed", 1.0f);
				light->SunLatitude = t.value("sun_latitude", -23.0f);
				light->Turbidity = t.value("turbidity", 2.5f);
				light->CloudCoverage = t.value("cloud_coverage", 0.4f);
				light->CloudSpeed = t.value("cloud_speed", 0.015f);
				if (t.contains("cloud_color") && t["cloud_color"].size() == 3)
					light->CloudColor = { t["cloud_color"][0], t["cloud_color"][1], t["cloud_color"][2] };
				if (t.contains("night_color") && t["night_color"].size() == 3)
					light->NightColor = { t["night_color"][0], t["night_color"][1], t["night_color"][2] };
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

			// ── ENV_PERSIST_V1 ───────────────────────────────────────────
			if (components.contains("Environment"))
			{
				auto& t = components["Environment"];
				EnvironmentComponent ec;
				ec.HDRIPath = t.value("hdri_path", std::string());
				ec.SkyboxRotation = t.value("skybox_rotation", 0.0f);
				ec.UseHDRI = t.value("use_hdri", true);
				registry.emplace<EnvironmentComponent>(entity, ec);
			}

			// ── SKY_LIGHT_V1 ─────────────────────────────────────────────
			if (components.contains("SkyLight"))
			{
				auto& t = components["SkyLight"];
				auto sky = std::make_shared<SkyLight>();
				sky->Enabled = t.value("enabled", true);
				sky->Intensity = t.value("intensity", 1.0f);
				if (t.contains("color") && t["color"].is_array() && t["color"].size() == 3)
					sky->Color = { t["color"][0], t["color"][1], t["color"][2] };
				sky->ShadowFactor = t.value("shadow_factor", 1.0f);
				sky->ConstantAmbient = t.value("constant_ambient", 0.0f);

				// ── SKY_OWNS_SKY_V1 ──────────────────────────────────────
				//
				// Um SkyLight salvo pela versao ANTERIOR nao tem nenhuma
				// destas chaves: ele foi criado quando o ceu ainda morava no
				// DirectionalLight. Carregar os defaults da struct nesse caso
				// desligaria o ceu procedural de quem ja o usava — cena
				// mudando de aparencia sozinha, sem erro nenhum.
				//
				// A ausencia de "procedural_sky" e a marca dessa versao, e e
				// so o que a migracao no fim do Deserialize precisa saber.
				if (!t.contains("procedural_sky"))
					s_SkyNeedsSkyFieldsMigration = true;

				sky->SunDependent = t.value("sun_dependent", true);
				sky->ProceduralSky = t.value("procedural_sky", false);
				sky->Turbidity = t.value("turbidity", 2.5f);
				sky->CloudCoverage = t.value("cloud_coverage", 0.4f);
				sky->CloudSpeed = t.value("cloud_speed", 0.015f);
				sky->CloudsSoftenSun = t.value("clouds_soften_sun", true);
				if (t.contains("cloud_color") && t["cloud_color"].is_array() && t["cloud_color"].size() == 3)
					sky->CloudColor = { t["cloud_color"][0], t["cloud_color"][1], t["cloud_color"][2] };
				if (t.contains("night_color") && t["night_color"].is_array() && t["night_color"].size() == 3)
					sky->NightColor = { t["night_color"][0], t["night_color"][1], t["night_color"][2] };
				sky->TimeOfDayEnabled = t.value("tod_enabled", false);
				sky->Hour = t.value("tod_hour", 12.0f);
				sky->DaySpeed = t.value("tod_speed", 1.0f);
				sky->SunLatitude = t.value("sun_latitude", -23.0f);

				registry.emplace<SkyLightComponent>(entity, sky);
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

				// POSTPROCESS_DOMAIN_V1 — .value() com default: cena salva ANTES
				// desta rodada abre normalmente, sem efeito nenhum.
				pp.Settings.UserMaterialUUID = t.value("user_material", std::string());
				pp.Settings.UserBlendPoint = (PostProcessBlendPoint)
					t.value("user_blend_point", (int)PostProcessBlendPoint::AfterTonemap);
				pp.Settings.UserIntensity = t.value("user_intensity", 1.0f);
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
				// VOLUME_DOMAIN_V1 — ausente em cena antiga = sem material =
				// fog embutido, que e exatamente o que a cena antiga tinha.
				fog.MaterialUUID = t.value("fog_material", std::string());
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

				// ── PPVOLUME_ONE_PATH_V1 — AUTO-CURA ─────────────────────────
				//
				// Cena gravada antes de o Post Process Volume ganhar os tres
				// volumes companheiros (ou criada pelo caminho antigo do
				// bootstrap) abre com a entidade INCOMPLETA: o Inspector nao
				// mostra Interior Volume, Probe Volume nem Reflection Probe, e
				// a unica saida seria apagar e recriar — perdendo exposure,
				// SSAO e o resto que ja estava ajustado ali.
				//
				// Aqui eles sao completados, DESATIVADOS. Nao muda um pixel da
				// imagem: e exatamente o que a criacao de hoje produz.
				//
				// Os blocos de InteriorVolume/ProbeVolume/ReflectionProbe rodam
				// ANTES deste ponto no mesmo laco, entao o que veio do arquivo
				// ja esta aplicado e nao e sobrescrito.
				bool healed = false;
				if (!registry.any_of<InteriorVolumeComponent>(entity))
				{
					registry.emplace<InteriorVolumeComponent>(entity).Data.Enabled = false;
					healed = true;
				}
				if (!registry.any_of<ProbeVolumeComponent>(entity))
				{
					registry.emplace<ProbeVolumeComponent>(entity).Settings.Enabled = false;
					healed = true;
				}
				if (!registry.any_of<ReflectionProbeComponent>(entity))
				{
					registry.emplace<ReflectionProbeComponent>(entity).Settings.Enabled = false;
					healed = true;
				}

				if (healed)
					AXE_CORE_INFO("PPVOLUME_ONE_PATH_V1: Post Process Volume completado com os "
						"volumes que faltavam (desativados). Salve a cena para gravar.");
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
				cc.OrientRotationToMovement = t.value("orient_to_movement", false);
				cc.RotationRate = t.value("rotation_rate", 720.0f);
				cc.ShowDebug = t.value("show_debug", true);

				if (t.contains("capsule_offset") && t["capsule_offset"].is_array()
					&& t["capsule_offset"].size() == 3)
				{
					cc.CapsuleOffset = { t["capsule_offset"][0].get<float>(),
										 t["capsule_offset"][1].get<float>(),
										 t["capsule_offset"][2].get<float>() };
				}
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

			if (components.contains("Spline"))
			{
				auto& t = components["Spline"];
				SplineComponent sp;

				if (t.contains("points") && t["points"].is_array())
				{
					for (const auto& p : t["points"])
					{
						if (p.is_array() && p.size() >= 3)
							sp.Points.push_back({ p[0].get<float>(),
												  p[1].get<float>(),
												  p[2].get<float>() });
					}
				}

				sp.Closed = t.value("closed", false);
				sp.AlwaysVisible = t.value("always_visible", false);

				// _Dirty ja nasce true: a tabela e reconstruida no primeiro uso.
				registry.emplace<SplineComponent>(entity, std::move(sp));
			}

			if (components.contains("SequencePlayer"))
			{
				auto& t = components["SequencePlayer"];
				SequencePlayerComponent sp;
				sp.SequenceUUID = t.value("sequence_uuid", std::string{});
				sp.PlayOnStart = t.value("play_on_start", false);
				sp.Loop = t.value("loop", false);
				sp.CameraCut = t.value("camera_cut", true);
				registry.emplace<SequencePlayerComponent>(entity, sp);
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
				// BP_CAMERA_V1 — defaults IGUAIS aos do struct: uma cena gravada
				// antes destes campos carrega em Orbit com os mesmos angulos que
				// o Play ja usava fixo, entao nada muda de comportamento.
				sa.Mode = (CameraRigMode)t.value("rig_mode", (int)CameraRigMode::Orbit);
				sa.FixedYaw = t.value("fixed_yaw", -90.0f);
				sa.FixedPitch = t.value("fixed_pitch", -10.0f);
				registry.emplace<SpringArmComponent>(entity, sa);
			}

			if (components.contains("Script"))
			{
				auto& t = components["Script"];
				ScriptComponent sc;
				// UUID primeiro; caminho so para cena gravada antes desta
				// mudanca. Uma cena antiga e migrada no proximo save, sem
				// nenhum passo manual.
				{
					const std::string scriptUuid = t.value("asset_uuid", std::string{});

					if (!scriptUuid.empty())
					{
						if (const AssetRecord* rec = AssetDatabase::Get().GetByUUID(scriptUuid))
						{
							sc.ScriptAssetPath = rec->FilePath.string();
						}
						else
						{
							// UUID gravado e ausente do banco. Nao e o caso de
							// cair no caminho antigo em silencio: se o indice
							// estiver desatualizado o caminho tambem falha, e o
							// usuario precisa saber que a referencia se perdeu.
							AXE_CORE_WARN("SceneSerializer: script uuid '{}' not found in the "
								"asset database. Falling back to the stored path.",
								scriptUuid);

							sc.ScriptAssetPath = t.value("asset_path", "");
						}
					}
					else
					{
						sc.ScriptAssetPath = t.value("asset_path", "");
					}
				}
				// dll_path de cena antiga e deliberadamente descartado: aponta para
				// bin/temp_scripts, que este patch abandonou. ScriptWorld resolve.
				sc.ScriptName = t.value("name", "");
				sc.IsCompiled = t.value("compiled", false);
				registry.emplace<ScriptComponent>(entity, sc);
			}

			if (components.contains("AudioSource"))
			{
				auto& t = components["AudioSource"];
				AudioSourceComponent src;
				src.ClipAssetUUID = t.value("clip_uuid", "");
				src.Volume = t.value("volume", 1.0f);
				src.Pitch = t.value("pitch", 1.0f);
				src.Loop = t.value("loop", false);
				src.PlayOnStart = t.value("play_on_start", true);
				src.Is3D = t.value("is_3d", true);
				src.MinDistance = t.value("min_distance", 1.0f);
				src.MaxDistance = t.value("max_distance", 100.0f);
				src.Category = SoundCategoryFromString(t.value("category", "Generic").c_str());
				src.Bus = AudioBusFromString(t.value("bus", "SFX").c_str());
				src.Priority = t.value("priority", 0.5f);
				src.DopplerFactor = t.value("doppler", 1.0f);
				src.FadeInTime = t.value("fade_in", 0.0f);
				src.FadeOutTime = t.value("fade_out", 0.0f);

				// `Data` fica NULO de proposito — o AudioWorld resolve no
				// primeiro uso. Decodificar aqui faria o tempo de abertura
				// da cena crescer com o numero de fontes, e exigiria que o
				// device de audio ja existisse durante o load, o que amarra
				// o serializer a um subsistema de que ele nao precisa.
				registry.emplace<AudioSourceComponent>(entity, src);
			}

			if (components.contains("AudioListener"))
			{
				AudioListenerComponent lc;
				lc.IsPrimary = components["AudioListener"].value("is_primary", true);

				// Default true: cena salva antes do A8 passa a usar a
				// orientacao da camera. E mudanca de comportamento, mas na
				// direcao certa — quem tinha listener no personagem estava
				// com o panning presso a ele sem ter escolhido isso.
				lc.UseCameraOrientation =
					components["AudioListener"].value("use_camera_orientation", true);
				registry.emplace<AudioListenerComponent>(entity, lc);
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

						// Resolve o material de partícula de cada emitter.
						// Mesmo padrão do LightMaterial — zero GL vaza pro axe.dll.
						//
						// EDITOR: o callback recompila do grafo. JOGO: callback
						// nulo, e o shader vem do `.axeshader` cozido no domínio
						// Particle (PKG9). Sem isto o emitter ficava sem shader
						// no jogo — as partículas apareciam no editor e não no
						// game.exe.
						if (ps.Data)
						{
							auto cb = SceneSerializer::GetParticleMaterialRecompileCallback();

							for (auto& emitter : ps.Data->Emitters)
							{
								if (emitter.ParticleMaterialUUID.empty()) continue;

								if (cb)
								{
									cb(emitter.ParticleMaterialUUID,
										emitter.ParticleMaterialShader,
										emitter.ParticleMaterialSamplers);
									continue;
								}

								if (!CookedMaterial::LoadShaderAndSamplers(
									emitter.ParticleMaterialUUID,
									CookedMaterialDomain::Particle,
									emitter.ParticleMaterialShader,
									emitter.ParticleMaterialSamplers))
								{
									AXE_CORE_WARN("SceneSerializer: particle material '{}' sem "
										".axeshader cozido - o emitter vai usar o shader padrao. "
										"Abra a cena no editor para gerar e reempacote.",
										emitter.ParticleMaterialUUID);
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

	// EDITOR_CAM_PERSIST_V1 — as duas caixas de correio (ver o header).
	SceneSerializer::EditorCameraState SceneSerializer::PendingEditorCamera{};
	SceneSerializer::EditorCameraState SceneSerializer::LoadedEditorCamera{};

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

		// EDITOR_CAM_PERSIST_V1 — so grava se o editor preencheu. O jogo
		// empacotado tambem chama o Serialize e nao tem camera de editor;
		// gravar zeros ali plantaria uma orbita invalida na cena.
		if (PendingEditorCamera.Valid)
		{
			auto& c = root["scene"]["editor_camera"];
			c["focal"] = { PendingEditorCamera.FocalPoint.x,
						   PendingEditorCamera.FocalPoint.y,
						   PendingEditorCamera.FocalPoint.z };
			c["distance"] = PendingEditorCamera.Distance;
			c["pitch"] = PendingEditorCamera.Pitch;
			c["yaw"] = PendingEditorCamera.Yaw;
		}

		json entities = json::array();

		for (auto entity : registry.storage<entt::entity>())
		{
			if (!registry.valid(entity)) continue;

			// Preview de ferramenta de autoria — nao vai para o arquivo.
			// Ver EditorTransientComponent em components.hpp: sem este pulo,
			// fechar o Sequencer e salvar deixaria a arma de preview anexada
			// ao personagem para sempre.
			if (registry.all_of<EditorTransientComponent>(entity)) continue;

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

		// SKY_OWNS_SKY_V1 — zera antes de carregar; o loader de componentes a
		// liga se encontrar um SkyLight sem os campos de ceu.
		s_SkyNeedsSkyFieldsMigration = false;

		// EDITOR_CAM_PERSIST_V1 — invalida antes; so vira Valid se o arquivo
		// tiver o bloco. Cena antiga nao mexe na camera do usuario.
		LoadedEditorCamera = EditorCameraState{};

		json root;
		try {
			root = json::parse(file);

			if (root["scene"].contains("editor_camera"))
			{
				auto& c = root["scene"]["editor_camera"];
				if (c.contains("focal") && c["focal"].is_array() && c["focal"].size() == 3)
					LoadedEditorCamera.FocalPoint = { c["focal"][0], c["focal"][1], c["focal"][2] };
				LoadedEditorCamera.Distance = c.value("distance", 10.0f);
				LoadedEditorCamera.Pitch = c.value("pitch", 0.0f);
				LoadedEditorCamera.Yaw = c.value("yaw", 0.0f);
				LoadedEditorCamera.Valid = true;
			}

			if (env && root["scene"].contains("environment"))
			{
				auto& envJson = root["scene"]["environment"];
				std::string hdriPath = envJson.value("hdri_path", "");
				env->SkyboxRotation = envJson.value("skybox_rotation", 0.0f);

				// ── PKG3: resolver o HDRI em mais de um lugar ────────────
				//
				// `hdri_path` costuma ser relativo AO DIRETORIO ATUAL
				// ("resources/quarry_04_puresky_2k.hdr"), porque no editor o
				// processo roda ao lado de `resources/`. O jogo empacotado nao
				// tem essa pasta, e o `exists()` cru falhava em silencio: sem
				// erro, sem skybox, fundo preto — foi o que apareceu no
				// primeiro build.
				//
				// Ordem: como esta (absoluto ou relativo ao cwd), depois
				// relativo a RAIZ DO PROJETO, depois ao lado do EXECUTAVEL.
				// A raiz vem antes do executavel porque um HDRI que o usuario
				// escolheu mora no projeto; o do executavel e o default que
				// veio com a engine.
				if (!hdriPath.empty())
				{
					std::error_code hec;
					std::filesystem::path resolved;

					auto tryPath = [&](const std::filesystem::path& p)
						{
							if (!resolved.empty()) return;
							if (!p.empty() && std::filesystem::exists(p, hec))
								resolved = p;
						};

					tryPath(hdriPath);

					if (ProjectManager::Get().HasProject())
						tryPath(ProjectManager::Get().GetCurrent().RootPath / hdriPath);

					tryPath(std::filesystem::current_path(hec) / hdriPath);

					if (!resolved.empty())
					{
						env->LoadHDRI(resolved.string());
					}
					else
					{
						// Falhar em silencio aqui custou uma sessao de
						// depuracao: o sintoma (fundo preto) nao aponta para
						// um arquivo faltando.
						AXE_CORE_WARN("SceneSerializer: HDRI '{}' not found - "
							"the scene will render without a skybox.", hdriPath);
					}
				}
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

			// ── GHOST_ENTITY_V1 — cenas ja contaminadas se curam sozinhas ────
			//
			// Uma entrada sem componente NENHUM (`"components": null`) nao
			// descreve entidade alguma: nem nome, nem transform, nem pasta.
			// Recria-la produzia a linha "Entity" vazia da Hierarchy — e como
			// ela era recriada a cada load e regravada a cada save, apagar a
			// mao nunca resolvia.
			//
			// A causa esta consertada no scene_snapshot.cpp (ver a nota longa
			// la). Este pulo e para os arquivos que ja foram gravados: sem ele
			// o usuario teria de limpar a cena a mao, uma linha por vez, e as
			// suas cenas antigas continuariam sujas para sempre.
			//
			// O pulo tambem tira o id do `idMap`, o que e o certo: um
			// Relationship que apontasse para um fantasma simplesmente nao
			// encontra o pai e a entidade fica na raiz — que e onde ela ja
			// estaria de qualquer forma.
			if (components.is_null() || components.empty())
			{
				AXE_CORE_WARN("SceneSerializer [GHOST_ENTITY_V1]: entidade {} sem componente "
					"nenhum no arquivo — ignorada. Salve a cena para limpar o registro.",
					oldID);
				continue;
			}

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

			ApplySocketAttachmentTarget(scene, entity, components, idMap);   // SC43
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

		// ═════════════════════════════════════════════════════════════════
		//  SKY_LIGHT_V1 — MIGRACAO DE CENA ANTIGA
		//
		//  Antes desta versao a luz de ambiente nao tinha entidade: intensidade
		//  do IBL, ambiente chapado e fator de sombra moravam DENTRO do
		//  DirectionalLight. Toda cena salva ate aqui esta nesse formato.
		//
		//  Aqui a cena ganha um Sky Light SEMEADO com esses valores. O
		//  resultado e que ela abre com EXATAMENTE a mesma aparencia de antes
		//  — e a partir daqui o ambiente tem dono proprio e pode ser ajustado
		//  (ou apagado) sem tocar no sol.
		//
		//  Mesmo padrao de auto-cura do PPVOLUME_ONE_PATH_V1: completar em
		//  silencio o que falta e LOGAR, em vez de exigir que ele apague e
		//  recrie a entidade perdendo tudo o que ja ajustou.
		//
		//  Os campos legados do DirectionalLight NAO sao zerados: eles seguem
		//  sendo salvos, entao um projeto reaberto numa build anterior a esta
		//  continua funcionando. Ver o comentario neles em directional_light.hpp.
		// ═════════════════════════════════════════════════════════════════
		{
			auto& registry = scene.GetRegistry();

			bool hasSkyLight = false;
			for (auto e : registry.view<SkyLightComponent>()) { (void)e; hasSkyLight = true; break; }

			// ── SKY_OWNS_SKY_V1 — SEGUNDO CASO DE MIGRACAO ───────────────
			//
			// Cena salva ENTRE o SKY_LIGHT_V1 e agora ja tem um SkyLight, mas
			// sem os campos de ceu — eles ainda estavam no DirectionalLight.
			// Sem este caso, o `if (!hasSkyLight)` abaixo nao rodaria, os
			// campos ficariam nos defaults da struct e o ceu procedural de
			// quem ja o usava simplesmente se desligaria no proximo load.
			if (hasSkyLight && s_SkyNeedsSkyFieldsMigration)
			{
				auto& registry2 = scene.GetRegistry();
				DirectionalLight* legacy = nullptr;
				for (auto e : registry2.view<LightComponent>())
				{
					auto& lc = registry2.get<LightComponent>(e);
					if (lc.Data) { legacy = lc.Data.get(); break; }
				}

				if (legacy)
				{
					for (auto e : registry2.view<SkyLightComponent>())
					{
						auto& sc = registry2.get<SkyLightComponent>(e);
						if (!sc.Data) continue;
						sc.Data->ProceduralSky = legacy->ProceduralSky;
						sc.Data->Turbidity = legacy->Turbidity;
						sc.Data->CloudCoverage = legacy->CloudCoverage;
						sc.Data->CloudSpeed = legacy->CloudSpeed;
						sc.Data->CloudColor = legacy->CloudColor;
						sc.Data->NightColor = legacy->NightColor;
						sc.Data->TimeOfDayEnabled = legacy->TimeOfDayEnabled;
						sc.Data->Hour = legacy->Hour;
						sc.Data->DaySpeed = legacy->DaySpeed;
						sc.Data->SunLatitude = legacy->SunLatitude;
						break;
					}
					AXE_CORE_INFO("SKY_OWNS_SKY_V1: configuracao do ceu migrada da luz "
						"direcional para o Sky Light (ceu procedural {}).",
						legacy->ProceduralSky ? "LIGADO" : "desligado");
				}
			}

			if (!hasSkyLight)
			{
				// Semente: os valores legados da PRIMEIRA luz direcional. Sem
				// luz nenhuma, os defaults da struct — cena vazia nao deveria
				// nascer com ambiente herdado de lugar nenhum.
				SkyLight seed;
				bool fromLegacy = false;
				for (auto e : registry.view<LightComponent>())
				{
					auto& lc = registry.get<LightComponent>(e);
					if (!lc.Data) continue;
					seed.Intensity = lc.Data->IBLIntensity;
					seed.ShadowFactor = lc.Data->AmbientShadowFactor;
					seed.ConstantAmbient = lc.Data->AmbientStrength;

					// SKY_OWNS_SKY_V1 — o ceu vem junto, pelo mesmo motivo:
					// abrir a cena tem que dar a MESMA imagem de antes.
					seed.ProceduralSky = lc.Data->ProceduralSky;
					seed.Turbidity = lc.Data->Turbidity;
					seed.CloudCoverage = lc.Data->CloudCoverage;
					seed.CloudSpeed = lc.Data->CloudSpeed;
					seed.CloudColor = lc.Data->CloudColor;
					seed.NightColor = lc.Data->NightColor;
					seed.TimeOfDayEnabled = lc.Data->TimeOfDayEnabled;
					seed.Hour = lc.Data->Hour;
					seed.DaySpeed = lc.Data->DaySpeed;
					seed.SunLatitude = lc.Data->SunLatitude;

					fromLegacy = true;
					break;
				}

				entt::entity e = scene.CreateSkyLight();
				auto& sc = registry.get<SkyLightComponent>(e);
				if (sc.Data) *sc.Data = seed;

				AXE_CORE_INFO("SKY_LIGHT_V1: cena sem Sky Light — criado {} "
					"(Intensity {:.2f}, ShadowFactor {:.2f}, ConstantAmbient {:.2f}). "
					"O ambiente agora e desta entidade, nao mais da luz direcional.",
					fromLegacy ? "a partir dos valores da luz direcional" : "com os padroes",
					seed.Intensity, seed.ShadowFactor, seed.ConstantAmbient);
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

			// Mesmo motivo do Serialize: o snapshot de Play tambem nao pode
			// carregar preview de autoria — ele volta no Stop.
			if (registry.all_of<EditorTransientComponent>(entity)) continue;

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

				ApplySocketAttachmentTarget(scene, entity, components, idMap);   // SC43
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

				ApplySocketAttachmentTarget(scene, entity, components, idMap);   // SC43
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