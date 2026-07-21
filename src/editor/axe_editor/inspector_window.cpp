// AXE build tag: inspector_window collapsable-volume-sections v2
#include "inspector_window.hpp"
#include "file_dialog.hpp"
#include "axe/animation/skeletal_mesh_loader.hpp"
#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/animation/anim_graph_asset.hpp"
#include "axe/utils/glm_config.hpp"
#include <imgui.h>
#include <cstdint>
#include <algorithm>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/component_wise.hpp>

#include "axe/asset/asset_database.hpp"
#include "axe/graphics/texture.hpp"
#include "axe/log/log.hpp"
#include "axe/material/material_asset.hpp"
#include "axe/scene/scene_serializer.hpp"
#include "axe/script/script_component.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "axe/project/project_manager.hpp"

#include "asset/asset_picker.hpp"
#include "editor/axe_editor/material/material_compiler.hpp"
#include "editor/axe_editor/material/material_editor_window.hpp"


namespace axe
{
	static std::unique_ptr<MaterialGraph> s_CachedGraph;
	static std::string s_CachedGraphUUID;
	static bool s_GraphCacheDirty = false;

	// Slot de "Light Material" — compartilhado entre PointLight e
	// DirectionalLight (mesmos nomes de campo: LightMaterialUUID/Shader/
	// Samplers), pra não duplicar a mesma lógica duas vezes. Compila via
	// MaterialCompiler::CompileLightFunctionFromFile (domínio Light
	// Function: só o pin Emissive importa) — ver comentário em PointLight.
	template<typename TLight>
	static void DrawLightMaterialSlot(TLight& light)
	{
		ImGui::Separator();
		ImGui::TextDisabled("Light Material (grafo controla a cor da luz)");

		bool changed = AssetPicker::Draw("Light Material", light.LightMaterialUUID,
			{ AssetType::Material },
			[&](const AssetRecord& record)
			{
				std::shared_ptr<Shader> shader;
				std::map<std::string, std::shared_ptr<Texture2D>> samplers;
				if (MaterialCompiler::CompileLightFunctionFromFile(record.FilePath, shader, samplers))
				{
					light.LightMaterialShader = shader;
					light.LightMaterialSamplers = samplers;
					light.LightMaterialUUID = record.UUID;
				}
			});

		if (changed && light.LightMaterialUUID.empty())
		{
			// Limpo via botão "X" do próprio AssetPicker (não passa pelo
			// callback onSelect, que só dispara numa seleção nova)
			light.LightMaterialShader = nullptr;
			light.LightMaterialSamplers.clear();
		}

		if (light.LightMaterialShader)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Recompilar"))
			{
				const AssetRecord* record = AssetDatabase::Get().GetByUUID(light.LightMaterialUUID);
				if (record)
				{
					std::shared_ptr<Shader> shader;
					std::map<std::string, std::shared_ptr<Texture2D>> samplers;
					if (MaterialCompiler::CompileLightFunctionFromFile(record->FilePath, shader, samplers))
					{
						light.LightMaterialShader = shader;
						light.LightMaterialSamplers = samplers;
					}
				}
			}
			ImGui::TextDisabled("Multiplica a Cor da luz (cinza = brilho, cor = tingimento)");
		}
	}

	static bool DrawComponentHeader(const char* label, entt::entity entity, int componentIdx, bool* outRemove)
	{
		*outRemove = false;
		ImGui::Separator();
		ImGui::Spacing();

		float available = ImGui::GetContentRegionAvail().x;

		// ID único: inclui entity e componente no label para evitar conflito
		std::string headerId = std::string(label) + "##hdr_" +
			std::to_string((uint32_t)entity) + "_" + std::to_string(componentIdx);
		std::string btnId = "x##x_" +
			std::to_string((uint32_t)entity) + "_" + std::to_string(componentIdx);

		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.25f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.28f, 0.32f, 1.0f));
		bool open = ImGui::CollapsingHeader(headerId.c_str(),
			ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);
		ImGui::PopStyleColor(2);

		ImGui::SameLine(available - 18.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.7f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
		if (ImGui::SmallButton(btnId.c_str()))
			*outRemove = true;
		ImGui::PopStyleColor(3);

		return open;
	}

	static void InvalidateGraphCache() { s_CachedGraphUUID = ""; }

	static void SaveCachedGraph(const std::string& assetUUID,
		entt::registry* registry = nullptr,
		entt::entity entity = entt::null)
	{
		if (!s_CachedGraph) return;
		const AssetRecord* record = AssetDatabase::Get().GetByUUID(assetUUID);
		if (!record) return;

		auto graphPath = record->FilePath;
		graphPath.replace_extension(".axegraph");

		nlohmann::json originalJ;
		{
			std::ifstream fileIn(graphPath);
			if (fileIn.is_open())
				try { originalJ = nlohmann::json::parse(fileIn); }
			catch (...) {}
		}

		nlohmann::json newJ = s_CachedGraph->Serialize();

		if (originalJ.contains("nodes") && newJ.contains("nodes"))
		{
			auto& origNodes = originalJ["nodes"];
			auto& newNodes = newJ["nodes"];
			for (int i = 0; i < (int)newNodes.size() && i < (int)origNodes.size(); i++)
				if (origNodes[i].contains("pos_x"))
				{
					newNodes[i]["pos_x"] = origNodes[i]["pos_x"];
					newNodes[i]["pos_y"] = origNodes[i]["pos_y"];
				}
		}

		std::ofstream fileOut(graphPath);
		if (fileOut.is_open())
			fileOut << newJ.dump(4);

		auto result = MaterialCompiler::Compile(s_CachedGraph.get());
		if (result.Success)
		{
			try
			{
				auto shader = Shader::Create(result.VertexShader, result.FragmentShader);
				if (shader && registry && registry->valid(entity))
				{
					auto* mc = registry->try_get<MaterialComponent>(entity);
					if (mc && mc->Data)
						mc->Data->SetShader(shader);
				}
			}
			catch (...) {}
		}
		MaterialEditorWindow::MarkNeedsReload();
	}

	static std::string FindOutputPinLabel(MaterialGraph* graph, Node* sourceNode)
	{
		for (auto& outPin : sourceNode->Outputs)
			for (auto& link : graph->GetLinks())
			{
				if (link.StartPin != outPin.ID) continue;
				for (auto& destNode : graph->GetNodes())
					for (auto& inPin : destNode->Inputs)
					{
						if (inPin.ID != link.EndPin) continue;
						if (destNode->Name == "Material Output") return inPin.Name;
						std::string label = FindOutputPinLabel(graph, destNode.get());
						if (!label.empty()) return label;
					}
			}
		return "";
	}

	void InspectorWindow::SetContext(EditorContext* context) { m_Context = context; }

	void InspectorWindow::Draw()
	{
		// Executa remoção pendente ANTES de qualquer ImGui — frame limpo
		if (m_PendingRemove != PendingRemove::None && m_Context)
		{
			auto& reg = m_Context->ActiveScene->GetRegistry();
			if (reg.valid(m_PendingRemoveEntity))
			{
				switch (m_PendingRemove)
				{
				case PendingRemove::Rigidbody:           reg.remove<RigidbodyComponent>(m_PendingRemoveEntity);           break;
				case PendingRemove::Collider:            reg.remove<ColliderComponent>(m_PendingRemoveEntity);            break;
				case PendingRemove::CharacterController: reg.remove<CharacterControllerComponent>(m_PendingRemoveEntity); break;
				case PendingRemove::Script:              reg.remove<ScriptComponent>(m_PendingRemoveEntity);              break;
				default: break;
				}
			}
			m_PendingRemove = PendingRemove::None;
			m_PendingRemoveEntity = entt::null;
		}

		if (!ImGui::Begin("Inspector")) { ImGui::End(); return; }

		if (!m_Context || !m_Context->HasSelection())
		{
			ImGui::TextDisabled("Nenhum objeto selecionado.");
			ImGui::End();
			return;
		}

		auto& registry = m_Context->ActiveScene->GetRegistry();
		entt::entity entity = m_Context->SelectedEntity;

		if (auto* name = registry.try_get<NameComponent>(entity))
		{
			char nameBuffer[256];
			std::memset(nameBuffer, 0, sizeof(nameBuffer));
			std::strncpy(nameBuffer, name->Name.c_str(), sizeof(nameBuffer) - 1);
			if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
				name->Name = nameBuffer;
		}

		ImGui::Separator();

		if (auto* transform = registry.try_get<TransformComponent>(entity))
			DrawTransform(transform->Data);

		if (auto* light = registry.try_get<LightComponent>(entity))
		{
			if (light->Data) DrawLight(*light->Data);
		}
		else if (registry.any_of<PostProcessComponent, InteriorVolumeComponent,
			ProbeVolumeComponent, ReflectionProbeComponent>(entity))
		{
			// Entity de "volumes de ambiente" — pode carregar os 4
			// componentes JUNTOS (via + Adicionar Componente), cada um na
			// sua seção colapsável. Um só Transform serve os quatro: a
			// caixa (Escala) é compartilhada — perfeito pro caso "uma
			// sala". Quando o grid de GI precisar ser maior que a sala ou
			// a captura de reflexo precisar de posição própria, crie
			// entities separadas — os dois fluxos coexistem.
			if (auto* pp = registry.try_get<PostProcessComponent>(entity))
				if (ImGui::CollapsingHeader("Post Process", ImGuiTreeNodeFlags_DefaultOpen))
				{
					// PushID por seção: as quatro repetem labels ("Ativo",
					// "Intensidade", "Bake"...) e o ImGui identifica o
					// widget pelo label — sem escopo de ID, só o PRIMEIRO
					// de cada nome responde ao clique (e o "Bake" da
					// Reflection dispararia o da Probe). O PushID torna
					// cada seção um namespace de IDs próprio.
					ImGui::PushID("sec_pp");
					DrawPostProcess(*pp);
					ImGui::PopID();
				}
			if (auto* iv = registry.try_get<InteriorVolumeComponent>(entity))
				if (ImGui::CollapsingHeader("Interior Volume"))
				{
					ImGui::PushID("sec_interior");
					DrawInteriorVolume(*iv);
					ImGui::PopID();
				}
			if (auto* pv = registry.try_get<ProbeVolumeComponent>(entity))
				if (ImGui::CollapsingHeader("Probe Volume (GI)"))
				{
					ImGui::PushID("sec_probe");
					DrawProbeVolume(*pv);
					ImGui::PopID();
				}
			if (auto* rp = registry.try_get<ReflectionProbeComponent>(entity))
				if (ImGui::CollapsingHeader("Reflection Probe"))
				{
					ImGui::PushID("sec_refl");
					DrawReflectionProbe(*rp);
					ImGui::PopID();
				}
		}
		else if (auto* ec = registry.try_get<EnvironmentComponent>(entity))
			DrawEnvironment(*ec);
		else if (auto* sa = registry.try_get<SpringArmComponent>(entity))
		{
			bool removeArm = false;
			bool openArm = DrawComponentHeader("Spring Arm", entity, 4, &removeArm);
			if (removeArm) { registry.remove<SpringArmComponent>(entity); return; }
			if (openArm) DrawSpringArm(*sa);
		}
		else if (auto* cam = registry.try_get<CameraComponent>(entity))
			DrawCamera(*cam);
		else if (auto* folder = registry.try_get<FolderComponent>(entity))
			DrawFolder(*folder);
		else if (registry.any_of<PointLightComponent>(entity)) {}
		else if (registry.any_of<ParticleSystemComponent>(entity))
		{
			// Entity de partícula pura — não exibe slot de Material aqui;
			// o Material do mesh (se existir) aparece só se houver MeshComponent
			// junto. Evita mostrar um slot inútil e confuso.
		}
		else if (!registry.any_of<MaterialComponent>(entity))
		{
			// Sem material ainda: mostra o slot vazio para poder atribuir um.
			ImGui::Separator();
			DrawMaterial(entity);
		}

		// ── Material: FORA da cadeia else-if ─────────────────────────────
		//
		// O painel vivia como mais um "else if" de uma cadeia que comeca em
		// SpringArm/Camera/Folder. Bastava a entidade ter um SpringArm (todo
		// personagem com camera tem) para o primeiro ramo vencer e o Material
		// NUNCA ser desenhado — o componente existia (o menu Adicionar dizia
		// "ja adicionado") mas ficava invisivel e ineditavel no Inspector.
		//
		// Material e ortogonal aos outros componentes: quem tem, mostra.
		if (registry.any_of<MaterialComponent>(entity))
			DrawMaterial(entity);

		if (auto* plc = registry.try_get<PointLightComponent>(entity))
			if (plc->Data)
			{
				glm::vec3 rotation = glm::vec3(0.0f);
				if (auto* tc = registry.try_get<TransformComponent>(entity))
					rotation = tc->Data.Rotation;
				DrawPointLight(*plc->Data, rotation);
			}

		// Física — podem coexistir com outros componentes
		if (registry.any_of<RigidbodyComponent>(entity))
			DrawRigidbody(entity, registry);
		if (registry.any_of<ColliderComponent>(entity))
			DrawCollider(entity, registry);
		if (registry.any_of<CharacterControllerComponent>(entity))
			DrawCharacterController(entity, registry);

		// Script
		if (registry.any_of<ScriptComponent>(entity))
		{
			auto& sc = registry.get<ScriptComponent>(entity);
			bool remove = false;
			bool open = DrawComponentHeader("Script", entity, 3, &remove);
			if (remove) { m_PendingRemove = PendingRemove::Script; m_PendingRemoveEntity = entity; }
			else if (open)
			{
				ImGui::Text("Script: %s", sc.ScriptName.c_str());
				ImGui::Spacing();

				float btnW = ImGui::GetContentRegionAvail().x;
				if (ImGui::Button("  Editar Script  ", ImVec2(btnW, 0)))
				{
					if (m_OnOpenScript)
						m_OnOpenScript(entity, &sc, &registry);
				}

				if (sc.IsCompiled)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
					ImGui::TextUnformatted("  Script compilado");
					ImGui::PopStyleColor();
				}
			}
		}

		// Particle System
		DrawParticleSystem(entity);

		// ─────────────────────────────────────────────────────────────────
		// Skeletal Mesh
		// ─────────────────────────────────────────────────────────────────
		if (auto* sk = registry.try_get<SkeletalMeshComponent>(entity))
		{
			if (ImGui::CollapsingHeader("Skeletal Mesh", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const Skeleton* skeleton = sk->GetSkeleton();

				// ── ENTIDADE ORFA ────────────────────────────────────────
				//
				// Sem Asset, este personagem existe SO EM MEMORIA: ele
				// funciona no editor, anima, tudo certo — e some pra sempre
				// ao reabrir a cena, porque nao ha .axeskel pra recarregar.
				//
				// Antes, nada na tela dizia isso. O usuario salvava, confiava,
				// e perdia o trabalho no proximo boot. Agora o estado quebrado
				// e VISIVEL.
				if (!sk->Asset)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
					ImGui::TextWrapped("SEM ASSET — este personagem NAO sera salvo na cena.");
					ImGui::PopStyleColor();

					ImGui::TextWrapped("Arraste o .axeskel do Asset Browser para a cena "
						"e apague esta entidade.");

					ImGui::Spacing();
				}
				else
				{
					ImGui::TextDisabled("Asset: %s", sk->Asset->GetName().c_str());
				}

				ImGui::Text("Ossos: %d", skeleton ? (int)skeleton->GetBoneCount() : 0);

				ImGui::Checkbox("Mostrar esqueleto", &sk->ShowSkeleton);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip(
						"Desenha os ossos como linhas.\n\n"
						"Ossos certos + malha explodida = bug no skinning.\n"
						"Ossos ja tortos = bug no import/hierarquia.");
				}

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				// ── AnimGraph ────────────────────────────────────────────
				//
				// Com um grafo atribuido, o clipe/blend space abaixo sao
				// IGNORADOS: quem manda passa a ser a state machine, e o
				// gameplay so escreve parametros. Dizer isso na tela evita a
				// confusao de "troquei o clipe e nao mudou nada".
				ImGui::TextDisabled("AnimGraph");

				if (sk->GraphAsset)
				{
					ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", sk->GraphAsset->GetName().c_str());
					ImGui::TextWrapped("O AnimGraph controla a animacao deste personagem.");

					if (ImGui::Button("Remover AnimGraph", ImVec2(-1, 0)))
					{
						sk->GraphAsset.reset();
						sk->GraphAssetUUID.clear();
						sk->GraphInstance.SetAsset(nullptr);   // solta a copia dos nos
					}
				}
				else
				{
					ImGui::TextDisabled("(nenhum) — arraste um .axeanim aqui");
				}

				// Alvo de drop: aceita o mesmo payload do Asset Browser.
				ImGui::InvisibleButton("##animgraph_drop", ImVec2(-1, 24));

				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET_UUID"))
					{
						const std::string uuid = (const char*)pl->Data;
						const AssetRecord* rec = AssetDatabase::Get().GetByUUID(uuid);

						// Pela EXTENSAO, e nao por rec->Type.
						//
						// rec->Type vem do .axemeta gravado — e um .axeanim
						// registrado por um build antigo (antes de AssetType::
						// AnimGraph existir) tem Type "Unknown" ou "Mesh" ali.
						// Chavear por Type faria o drop no slot de AnimGraph
						// FALHAR silenciosamente, e cair no alvo de mesh abaixo,
						// que grava o UUID num MeshComponent. A extensao do
						// arquivo no disco e a verdade.
						const bool isAnimGraph = rec &&
							rec->FilePath.extension() == ".axeanim";

						if (isAnimGraph && sk->Asset)
						{
							auto ga = AnimGraphAsset::LoadFromFile(rec->FilePath);

							// Resolve CONTRA O ESQUELETO DESTA ENTIDADE.
							//
							// O .axeanim guarda os clipes por NOME. Resolver
							// contra o personagem certo e o que religa os
							// nomes aos clipes de verdade — e o que permite
							// reusar o mesmo grafo em varios personagens que
							// compartilhem a nomenclatura.
							if (ga && ga->Resolve(*sk->Asset))
							{
								sk->GraphAsset = ga;
								sk->GraphAssetUUID = uuid;

								// Clona o grafo pra ESTA entidade. Sem isto o
								// personagem so comecaria a animar no proximo
								// frame do AnimationWorld — e o preview do
								// editor mostraria bind pose ate la.
								sk->GraphInstance.SetAsset(ga);
							}
						}
					}
					ImGui::EndDragDropTarget();
				}

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				// ── Animações ────────────────────────────────────────────
				ImGui::TextDisabled("Animacoes (%d)", (int)sk->Clips.size());

				const bool hasAsset = (sk->Asset != nullptr);

				if (!hasAsset)
					ImGui::BeginDisabled();

				if (ImGui::Button("Importar animacao...", ImVec2(-1, 0)))
				{
					const auto path = FileDialog::Open(
						"Animacao\0*.fbx;*.gltf;*.glb;*.dae\0Todos\0*.*\0",
						"Importar clipe de animacao");

					if (!path.empty() && sk->Asset)
					{
						// Escreve no ASSET, nao no componente.
						//
						// E essa diferenca que faz a animacao PERSISTIR: o
						// .axeskel guarda a lista de arquivos, entao ao
						// reabrir o projeto (ou arrastar o mesmo personagem de
						// novo) os clipes ja vem juntos. Se so mexessemos no
						// componente, tudo se perderia ao fechar o editor.
						const int added = sk->Asset->AddAnimation(path);

						if (added > 0)
						{
							sk->Asset->Save();

							// Recopia do asset — ele e a fonte de verdade.
							sk->Clips = sk->Asset->GetClips();

							if (sk->CurrentClip < 0 && !sk->Clips.empty())
								sk->CurrentClip = 0;
						}
					}
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip(hasAsset
						? "Aceita FBX so com as curvas (Mixamo: 'Without Skin').\nFica salvo no .axeskel."
						: "Sem asset .axeskel. Importe o personagem pelo Asset Browser\n"
						"(botao direito no FBX > Importar como Skeletal Mesh).");
				}

				if (!hasAsset)
					ImGui::EndDisabled();

				// ── Entradas importadas, com remocao ─────────────────────
				//
				// Cada linha = um ARQUIVO registrado no .axeskel. O "x"
				// desimporta: a entrada sai, os clipes daquele arquivo somem
				// do combo, e os sufixos anti-colisao reassentam. E a
				// ferramenta de limpeza pras duplicatas acumuladas.
				if (hasAsset)
				{
					int removeEntry = -1;
					const auto& entries = sk->Asset->GetAnimations();

					for (std::size_t e = 0; e < entries.size(); ++e)
					{
						ImGui::PushID((int)e + 9000);

						if (ImGui::SmallButton("x"))
							removeEntry = (int)e;

						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("Remove esta entrada e os clipes dela.\nO arquivo de origem NAO e apagado.");

						ImGui::SameLine();

						const std::string& nm = entries[e].Name.empty()
							? entries[e].SourceFile.filename().string()
							: entries[e].Name;

						ImGui::TextUnformatted(nm.c_str());
						ImGui::SameLine();
						ImGui::TextDisabled("(%s)", entries[e].SourceFile.filename().string().c_str());

						ImGui::PopID();
					}

					if (removeEntry >= 0)
					{
						if (sk->Asset->RemoveAnimation((std::size_t)removeEntry))
						{
							sk->Asset->Save();

							// Recopia do asset — ele e a fonte de verdade.
							sk->Clips = sk->Asset->GetClips();

							if (sk->CurrentClip >= (int)sk->Clips.size())
								sk->CurrentClip = sk->Clips.empty() ? -1 : 0;
						}
					}
				}

				// Com AnimGraph no personagem, o player manual abaixo nao
				// tem funcao: a state machine decide o que toca, e dois
				// donos da mesma animacao so geram confusao ("mudei o
				// clipe e nada aconteceu"). O Importar acima CONTINUA
				// visivel — clipes novos entram no .axeskel e viram
				// opcoes pros nos do grafo.
				if (sk->GraphAsset)
				{
					ImGui::TextDisabled("Reproducao controlada pelo AnimGraph.");
				}
				else if (!sk->Clips.empty())
				{
					// Dropdown de clipe.
					const int count = (int)sk->Clips.size();
					const char* currentName =
						(sk->CurrentClip >= 0 && sk->CurrentClip < count && sk->Clips[sk->CurrentClip])
						? sk->Clips[sk->CurrentClip]->GetName().c_str()
						: "(bind pose)";

					if (ImGui::BeginCombo("Clipe", currentName))
					{
						if (ImGui::Selectable("(bind pose)", sk->CurrentClip < 0))
							sk->CurrentClip = -1;

						for (int i = 0; i < count; ++i)
						{
							if (!sk->Clips[i])
								continue;

							const bool selected = (sk->CurrentClip == i);
							if (ImGui::Selectable(sk->Clips[i]->GetName().c_str(), selected))
								sk->CurrentClip = i;

							if (selected)
								ImGui::SetItemDefaultFocus();
						}

						ImGui::EndCombo();
					}

					ImGui::SliderFloat("Blend (s)", &sk->BlendTime, 0.0f, 1.0f, "%.2f");

					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Duracao do crossfade ao trocar de clipe. 0 = corte seco.");

					// ── Controles de reproducao ──────────────────────────
					const bool playing = sk->Player.Playing;

					if (ImGui::Button(playing ? "Pausar" : "Tocar", ImVec2(90, 0)))
						sk->Player.Playing = !playing;

					ImGui::SameLine();

					if (ImGui::Button("Reiniciar", ImVec2(90, 0)))
						sk->Player.SetTime(0.0f);

					ImGui::SliderFloat("Velocidade", &sk->Player.PlayRate, -2.0f, 2.0f, "%.2fx");

					// Scrub. O AnimationWorld reavalia a pose mesmo fora do
					// Play, entao arrastar isto mexe o personagem no editor —
					// que e exatamente o que voce quer pra conferir um import.
					if (sk->CurrentClip >= 0 && sk->CurrentClip < count && sk->Clips[sk->CurrentClip])
					{
						float t = sk->Player.GetTime();
						const float dur = sk->Clips[sk->CurrentClip]->GetDuration();

						if (ImGui::SliderFloat("Tempo", &t, 0.0f, dur > 0.0f ? dur : 1.0f, "%.2fs"))
							sk->Player.SetTime(t);
					}
				}
				else
				{
					ImGui::TextDisabled("Nenhum clipe. Renderizando em bind pose (T-pose).");
				}
			}
		}

		// Botão Add Component
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		float btnWidth = ImGui::GetContentRegionAvail().x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btnWidth * 0.15f);

		ImGui::PushID((uint32_t)entity);
		if (ImGui::Button("+ Adicionar Componente", ImVec2(btnWidth * 0.7f, 0)))
			ImGui::OpenPopup("add_comp");

		if (ImGui::BeginPopup("add_comp"))
		{
			// Os volumes de ambiente (Post Process, Interior, Probe, Refl)
			// NÃO aparecem aqui: eles vêm juntos ao criar um "Post Process
			// Volume" pelo menu Criar do Hierarchy — checkbox "Ativo"
			// controla cada um. Assim a entity de ambiente é sempre uma só.
			ImGui::TextDisabled("Fisica");
			ImGui::Separator();

			bool hasRb = registry.any_of<RigidbodyComponent>(entity);
			bool hasCol = registry.any_of<ColliderComponent>(entity);
			bool hasCc = registry.any_of<CharacterControllerComponent>(entity);

			if (hasRb)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
				ImGui::TextUnformatted("  Rigidbody (ja adicionado)");
				ImGui::PopStyleColor();
			}
			else if (ImGui::MenuItem("  Rigidbody"))
			{
				registry.emplace<RigidbodyComponent>(entity);
				ImGui::CloseCurrentPopup();
			}

			if (hasCol)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
				ImGui::TextUnformatted("  Collider (ja adicionado)");
				ImGui::PopStyleColor();
			}
			else if (ImGui::MenuItem("  Collider"))
			{
				auto& col = registry.emplace<ColliderComponent>(entity);

				// Detecta shape pelo nome do mesh
				if (auto* mc = registry.try_get<MeshComponent>(entity))
				{
					std::string uuid = mc->AssetUUID;
					std::string name = "";
					if (auto* nc = registry.try_get<NameComponent>(entity))
						name = nc->Name;

					// Converte para lowercase para comparação
					std::string lower = name;
					std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

					if (lower.find("sphere") != std::string::npos ||
						lower.find("ball") != std::string::npos ||
						lower.find("esfera") != std::string::npos)
					{
						col.Shape = ColliderShape::Sphere;
						// Usa o maior eixo do scale como raio
						if (auto* tc = registry.try_get<TransformComponent>(entity))
							col.Radius = glm::compMax(tc->Data.Scale) * 0.5f;
						else
							col.Radius = 1.0f;
					}
					else if (lower.find("capsule") != std::string::npos ||
						lower.find("capsula") != std::string::npos)
					{
						col.Shape = ColliderShape::Capsule;
						col.Height = 1.8f;
						col.CapsuleRadius = 0.3f;
					}
					// else: Box por padrão
				}
				ImGui::CloseCurrentPopup();
			}

			if (hasCc)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
				ImGui::TextUnformatted("  Character Controller (ja adicionado)");
				ImGui::PopStyleColor();
			}
			else if (ImGui::MenuItem("  Character Controller"))
			{
				registry.emplace<CharacterControllerComponent>(entity);
				ImGui::CloseCurrentPopup();
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Renderizacao");
			ImGui::Separator();

			if (registry.any_of<MaterialComponent>(entity))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
				ImGui::TextUnformatted("  Material (ja adicionado)");
				ImGui::PopStyleColor();
			}
			else if (ImGui::MenuItem("  Material"))
			{
				registry.emplace<MaterialComponent>(entity);
				ImGui::CloseCurrentPopup();
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Script");
			ImGui::Separator();

			if (registry.any_of<ScriptComponent>(entity))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
				ImGui::TextUnformatted("  Script (ja adicionado)");
				ImGui::PopStyleColor();
			}
			else if (ImGui::MenuItem("  Script"))
			{
				auto& sc = registry.emplace<ScriptComponent>(entity);
				if (auto* nc = registry.try_get<NameComponent>(entity))
					sc.ScriptName = nc->Name + "Script";
				ImGui::CloseCurrentPopup();
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Efeitos");
			ImGui::Separator();

			if (registry.any_of<ParticleSystemComponent>(entity))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
				ImGui::TextUnformatted("  Particle System (ja adicionado)");
				ImGui::PopStyleColor();
			}
			else if (ImGui::MenuItem("  Particle System"))
			{
				registry.emplace<ParticleSystemComponent>(entity);
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
		ImGui::PopID();

		ImGui::End();
	}

	void InspectorWindow::DrawSpringArm(SpringArmComponent& sa)
	{
		ImGui::DragFloat("Comprimento", &sa.Length, 0.1f, 0.5f, 50.0f, "%.1f m");
		ImGui::DragFloat("Altura", &sa.HeightOffset, 0.1f, 0.0f, 20.0f, "%.1f m");
		ImGui::DragFloat3("Socket Offset", &sa.SocketOffset.x, 0.05f, -10.f, 10.f, "%.2f");
		ImGui::DragFloat("Suavização", &sa.LagSpeed, 0.1f, 0.5f, 30.0f, "%.1f");
		ImGui::Checkbox("Lag de câmera", &sa.EnableCameraLag);
		ImGui::Checkbox("Mouse rotaciona", &sa.MouseRotates);
	}

	void InspectorWindow::DrawCamera(CameraComponent& cam)
	{
		ImGui::Separator();
		ImGui::Text("Câmera");
		ImGui::Checkbox("Câmera Principal", &cam.IsPrimary);
		ImGui::DragFloat("FOV", &cam.Fov, 0.5f, 10.0f, 170.0f);
		ImGui::DragFloat("Near Clip", &cam.NearClip, 0.01f, 0.001f, 10.0f);
		ImGui::DragFloat("Far Clip", &cam.FarClip, 1.0f, 10.0f, 10000.0f);
		ImGui::Separator();
		ImGui::TextDisabled("Controles (modo Play)");
		ImGui::DragFloat("Velocidade", &cam.MoveSpeed, 0.1f, 0.1f, 100.0f);
		ImGui::DragFloat("Sensibilidade", &cam.Sensitivity, 0.01f, 0.01f, 5.0f);
	}

	void InspectorWindow::DrawFolder(FolderComponent& folder)
	{
		ImGui::Separator();
		ImGui::Text("Pasta");
		float col[4] = { folder.Color.x, folder.Color.y, folder.Color.z, folder.Color.w };
		if (ImGui::ColorEdit4("Cor", col))
			folder.Color = { col[0], col[1], col[2], col[3] };
		ImGui::TextDisabled("Arraste objetos para dentro desta pasta na hierarchy.");
	}

	void InspectorWindow::DrawEnvironment(EnvironmentComponent& ec)
	{
		ImGui::Separator();
		ImGui::Text("Environment");
		ImGui::TextDisabled("HDRI:");
		ImGui::SameLine();
		std::string shortPath = ec.HDRIPath.empty() ? "Nenhum" :
			std::filesystem::path(ec.HDRIPath).filename().string();
		ImGui::Text("%s", shortPath.c_str());
		if (ImGui::Button("Carregar HDRI..."))
		{
			auto path = FileDialog::Open("HDR Image\0*.hdr;*.exr\0All Files\0*.*\0", "Selecionar HDRI", "hdr");
			if (!path.empty()) ec.HDRIPath = path.string();
		}
		ImGui::Separator();
		ImGui::DragFloat("Rotação Skybox", &ec.SkyboxRotation, 1.0f, -360.0f, 360.0f);
		ImGui::TextDisabled("Céu Procedural e Time of Day estão");
		ImGui::TextDisabled("no componente Directional Light.");
	}

	void InspectorWindow::DrawPointLight(PointLight& light, const glm::vec3& rotationEuler)
	{
		ImGui::Separator();
		ImGui::Text("Point Light");
		ImGui::TextDisabled("Posicione pelo Transform");
		ImGui::ColorEdit3("Cor", glm::value_ptr(light.Color));
		ImGui::DragFloat("Intensidade", &light.Intensity, 0.1f, 0.0f, 100.0f);
		ImGui::DragFloat("Radius", &light.Radius, 0.1f, 0.1f, 200.0f);

		ImGui::Separator();
		ImGui::Checkbox("Cast Shadows", &light.CastShadows);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Sombra omnidirecional (6 renders de\n"
				"profundidade por frame). Maximo de 4 luzes\n"
				"sombreadas simultaneas — as mais proximas da\n"
				"camera vencem. Use nas luzes que importam.");
		if (light.CastShadows)
		{
			ImGui::DragFloat("Shadow Bias (m)", &light.ShadowBias, 0.005f, 0.0f, 1.0f);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Aumente se aparecer acne (listras);\n"
					"diminua se a sombra 'descolar' do objeto.");
		}
		ImGui::Spacing();

		ImGui::Checkbox("Spot Light (cone)", &light.IsSpot);
		if (light.IsSpot)
		{
			ImGui::TextDisabled("Direção segue a Rotation do Transform —");
			ImGui::TextDisabled("rotacione o objeto pra apontar o cone.");
			glm::vec3 dir = ComputeSpotDirection(rotationEuler); // valor real, ao vivo
			ImGui::BeginDisabled();
			ImGui::DragFloat3("Direção (calculada)", glm::value_ptr(dir), 0.01f);
			ImGui::EndDisabled();
			ImGui::DragFloat("Ângulo Interno", &light.InnerConeAngle, 0.5f, 0.0f, light.OuterConeAngle);
			ImGui::DragFloat("Ângulo Externo", &light.OuterConeAngle, 0.5f, light.InnerConeAngle, 89.0f);

			ImGui::Spacing();
			ImGui::TextDisabled("Cookie (textura projetada através do cone)");
			DrawTextureSlot("Cookie", light.CookieTexture, light.CookieTextureUUID);
		}

		DrawLightMaterialSlot(light);
	}

	void InspectorWindow::DrawLight(DirectionalLight& light)
	{
		ImGui::Separator();
		ImGui::Text("Luz Direcional");
		ImGui::DragFloat3("Direção", glm::value_ptr(light.Direction), 0.01f, -1.0f, 1.0f);
		ImGui::ColorEdit3("Cor", glm::value_ptr(light.Color));
		ImGui::DragFloat("Intensidade", &light.Intensity, 0.01f, 0.0f, 10.0f);
		ImGui::Separator();
		ImGui::TextDisabled("Luz Indireta");
		ImGui::DragFloat("IBL Intensity", &light.IBLIntensity, 0.01f, 0.0f, 5.0f);
		ImGui::DragFloat("Ambient Flat", &light.AmbientStrength, 0.01f, 0.0f, 1.0f);
		ImGui::SliderFloat("Ambient nas Sombras", &light.AmbientShadowFactor, 0.0f, 1.0f);
		ImGui::TextDisabled("  0=interior escuro  1=ambient livre (exterior)");

		ImGui::Separator();
		ImGui::TextDisabled("Sombras");
		ImGui::Checkbox("Cast Shadows", &light.CastShadows);
		if (light.CastShadows)
		{
			ImGui::DragFloat("Shadow Distance", &light.ShadowDistance, 1.0f, 2.0f, 200.0f);
			ImGui::TextDisabled("  Menor = mais preciso (indoor: 8-15, outdoor: 50-100)");
			ImGui::DragFloat("Shadow Bias", &light.ShadowBias, 0.0001f, 0.0001f, 0.05f);
		}

		ImGui::Separator();
		ImGui::TextDisabled("Cookie (textura projetada, com tiling)");
		DrawTextureSlot("Cookie", light.CookieTexture, light.CookieTextureUUID);
		if (light.CookieTexture)
			ImGui::DragFloat("Escala (tamanho do tile)", &light.CookieScale, 0.1f, 0.1f, 200.0f);

		DrawLightMaterialSlot(light);

		// ── Céu Procedural ────────────────────────────────────────────────────
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Céu Procedural");
		ImGui::Checkbox("Ativar Céu Procedural##procsky", &light.ProceduralSky);
		if (light.ProceduralSky)
		{
			ImGui::TextDisabled("  O sol segue a direção desta luz.");
			ImGui::DragFloat("Turbidez", &light.Turbidity, 0.1f, 1.0f, 10.0f);
			ImGui::TextDisabled("  1=limpo  10=poluido/nublado");
			ImGui::DragFloat("Cobertura Nuvens", &light.CloudCoverage, 0.02f, 0.0f, 1.0f);
			ImGui::DragFloat("Velocidade Nuvens", &light.CloudSpeed, 0.001f, 0.0f, 0.2f);
			ImGui::ColorEdit3("Cor Nuvens", &light.CloudColor.x);
			ImGui::ColorEdit3("Cor Noite", &light.NightColor.x);

			// ── Time of Day ─────────────────────────────────────────────────
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Text("Ciclo Dia/Noite");
			ImGui::Checkbox("Ativar Time of Day##tod", &light.TimeOfDayEnabled);
			if (light.TimeOfDayEnabled)
			{
				ImGui::SliderFloat("Hora##tod", &light.Hour, 0.0f, 24.0f);
				ImGui::DragFloat("Velocidade##tod", &light.DaySpeed, 1.0f, 0.1f, 3600.0f);
				ImGui::TextDisabled("  1=tempo real  60=1min/seg  3600=1h/seg");
				ImGui::DragFloat("Latitude##tod", &light.SunLatitude, 1.0f, -90.f, 90.f);
				int h = (int)light.Hour;
				int m = (int)((light.Hour - h) * 60.0f);
				ImGui::Text("  Hora atual: %02d:%02d", h, m);
				ImGui::TextDisabled("  Direcao, Cor e Intensidade sao");
				ImGui::TextDisabled("  automaticas com Time of Day ativo.");
			}
		}
	}

	void InspectorWindow::DrawTransform(Transform& t)
	{
		ImGui::Text("Transform");
		bool changed = false;
		if (ImGui::DragFloat3("Position", &t.Position.x, 0.1f)) changed = true;
		glm::vec3 rotDeg = glm::degrees(t.Rotation);
		if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotDeg), 0.5f))
		{
			t.Rotation = glm::radians(rotDeg);
			changed = true;
		}
		glm::vec3 scaleCopy = t.Scale;
		if (ImGui::DragFloat3("Scale", &scaleCopy.x, 0.05f))
		{
			t.Scale.x = std::max(scaleCopy.x, 0.001f);
			t.Scale.y = std::max(scaleCopy.y, 0.001f);
			t.Scale.z = std::max(scaleCopy.z, 0.001f);
			changed = true;
		}
		if (changed) { t.UseWorldMatrix = false; t.WorldMatrix = t.GetMatrix(); }
	}

	void InspectorWindow::DrawTextureSlot(const char* label,
		std::shared_ptr<Texture2D>& tex, std::string& uuid)
	{
		ImGui::PushID(label);
		ImVec2 size(48, 48);
		if (tex && tex->IsLoaded())
			ImGui::Image((ImTextureID)(uintptr_t)tex->GetRendererID(), size, ImVec2(0, 1), ImVec2(1, 0));
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
			ImGui::Button("##empty", size);
			ImGui::PopStyleColor();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_UUID"))
			{
				std::string droppedUUID = (const char*)payload->Data;
				const AssetRecord* record = AssetDatabase::Get().GetByUUID(droppedUUID);
				if (record && record->Type == AssetType::Texture)
				{
					tex = Texture2D::Create(record->FilePath.string());
					uuid = droppedUUID;
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::Text("%s", label);
		if (tex && tex->IsLoaded())
		{
			ImGui::TextDisabled("%dx%d", tex->GetWidth(), tex->GetHeight());
			if (ImGui::SmallButton("X")) { tex = nullptr; uuid = ""; }
		}
		else ImGui::TextDisabled("Nenhuma");
		ImGui::EndGroup();
		ImGui::PopID();
		ImGui::Spacing();
	}

	void InspectorWindow::DrawMaterial(entt::entity entity)
	{
		auto& registry = m_Context->ActiveScene->GetRegistry();
		auto* mc = registry.try_get<MaterialComponent>(entity);

		ImGui::Separator();
		ImGui::Text("Material");
		ImGui::Spacing();

		std::string uuid = mc ? mc->MaterialAssetUUID : "";

		if (AssetPicker::Draw("Material", uuid, { AssetType::Material },
			[&](const AssetRecord& record)
			{
				auto matAsset = MaterialAsset::LoadFromFile(record.FilePath);
				if (!matAsset) return;
				auto material = matAsset->GetMaterial();
				if (SceneSerializer::GetMaterialRecompileCallback())
					SceneSerializer::GetMaterialRecompileCallback()(record.UUID, material.get());
				if (!mc)
				{
					auto& comp = registry.emplace<MaterialComponent>(entity, material);
					comp.MaterialAssetUUID = record.UUID;
					mc = registry.try_get<MaterialComponent>(entity);
				}
				else
				{
					mc->Data = material;
					mc->MaterialAssetUUID = record.UUID;
				}
			}))
		{
			if (uuid.empty() && mc) { registry.remove<MaterialComponent>(entity); mc = nullptr; }
			else if (!uuid.empty() && mc) mc->MaterialAssetUUID = uuid;
		}

		ImGui::Spacing();
		if (mc && mc->Data)
		{
			if (!mc->MaterialAssetUUID.empty())
				DrawMaterialGraphParams(mc->MaterialAssetUUID, registry, entity);
			else
				DrawMaterialParams(*mc->Data);
		}
	}

	// Particle System — referência a um ParticleSystemAsset (.axepart),
	// mesmo modelo do Material: o componente só guarda o UUID + um cache
	// runtime (Data); os parâmetros vivem no asset, editados no Particle
	// Editor (duplo clique no asset browser, ou botão "Editar" abaixo).
	void InspectorWindow::DrawParticleSystem(entt::entity entity)
	{
		auto& registry = m_Context->ActiveScene->GetRegistry();
		auto* ps = registry.try_get<ParticleSystemComponent>(entity);
		if (!ps) return;

		ImGui::Separator();
		if (!ImGui::CollapsingHeader("Particle System", ImGuiTreeNodeFlags_DefaultOpen)) return;

		std::string uuid = ps->ParticleAssetUUID;

		if (AssetPicker::Draw("System", uuid, { AssetType::ParticleSystem },
			[&](const AssetRecord& record)
			{
				auto asset = ParticleSystemAsset::LoadFromFile(record.FilePath);
				if (!asset) return;
				ps->Data = asset;
				ps->ParticleAssetUUID = record.UUID;
				ps->EmitterRuntimes.assign(asset->Emitters.size(), ParticleEmitterRuntime{});
			}))
		{
			if (uuid.empty()) { ps->Data = nullptr; ps->ParticleAssetUUID.clear(); }
			else ps->ParticleAssetUUID = uuid;
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("New##particle_new"))
		{
			if (ProjectManager::Get().HasProject())
			{
				auto dir = ProjectManager::Get().GetCurrent().AssetsPath / "Particles";
				auto path = dir / "NewParticleSystem.axepart";
				int i = 1;
				while (std::filesystem::exists(path))
					path = dir / ("NewParticleSystem_" + std::to_string(i++) + ".axepart");

				auto asset = ParticleSystemAsset::Create(path.stem().string());
				asset->Save(path);
				auto newUuid = AssetDatabase::Get().Register(path.string());
				AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

				ps->Data = asset;
				ps->ParticleAssetUUID = newUuid;
				ps->EmitterRuntimes.assign(asset->Emitters.size(), ParticleEmitterRuntime{});
			}
		}

		ImGui::Checkbox("Playing", &ps->Playing);

		if (ps->Data && m_OnOpenParticleSystem)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Edit##particle_edit"))
				m_OnOpenParticleSystem(ps->Data);
		}

		if (ps->Data)
		{
			int alive = 0, total = 0;
			for (auto& rt : ps->EmitterRuntimes)
			{
				for (auto& p : rt.Particles) if (p.Alive) ++alive;
				total += (int)rt.Particles.size();
			}
			ImGui::TextDisabled("Alive: %d / %d (Max defined in asset)", alive, total);
		}
		else
		{
			ImGui::TextDisabled("No Particle System assigned.");
		}
	}

	void InspectorWindow::DrawMaterialParams(Material& mat)
	{
		bool usePBR = mat.UsePBR;
		if (ImGui::Checkbox("PBR", &usePBR)) mat.UsePBR = usePBR;
		ImGui::Separator();
		if (!mat.UsePBR)
		{
			ImGui::ColorEdit4("Cor", glm::value_ptr(mat.Color));
			ImGui::DragFloat("Specular", &mat.SpecularStrength, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Shininess", &mat.Shininess, 1.0f, 1.0f, 256.0f);
			return;
		}
		ImGui::DragFloat("Metallic", &mat.Metallic, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Roughness", &mat.Roughness, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("AO", &mat.AO, 0.01f, 0.0f, 1.0f);
		ImGui::Separator();
		ImGui::Text("Texturas:");
		ImGui::Spacing();
		AssetPicker::Draw("Albedo", mat.AlbedoUUID, { AssetType::Texture }, [&](const AssetRecord& r) { mat.AlbedoMap = Texture2D::Create(r.FilePath.string()); mat.AlbedoUUID = r.UUID; });
		AssetPicker::Draw("Normal", mat.NormalUUID, { AssetType::Texture }, [&](const AssetRecord& r) { mat.NormalMap = Texture2D::Create(r.FilePath.string()); mat.NormalUUID = r.UUID; });
		AssetPicker::Draw("Roughness", mat.RoughnessUUID, { AssetType::Texture }, [&](const AssetRecord& r) { mat.RoughnessMap = Texture2D::Create(r.FilePath.string()); mat.RoughnessUUID = r.UUID; });
		AssetPicker::Draw("Metallic", mat.MetallicUUID, { AssetType::Texture }, [&](const AssetRecord& r) { mat.MetallicMap = Texture2D::Create(r.FilePath.string()); mat.MetallicUUID = r.UUID; });
		AssetPicker::Draw("AO", mat.AOUUID, { AssetType::Texture }, [&](const AssetRecord& r) { mat.AOMap = Texture2D::Create(r.FilePath.string()); mat.AOUUID = r.UUID; });
	}

	void InspectorWindow::DrawMaterialGraphParams(const std::string& assetUUID,
		entt::registry& registry, entt::entity entity)
	{
		if (s_GraphCacheDirty) { s_CachedGraphUUID = ""; s_GraphCacheDirty = false; }
		if (assetUUID.empty()) return;

		if (s_CachedGraphUUID != assetUUID)
		{
			s_CachedGraphUUID = assetUUID;
			s_CachedGraph.reset();
			const AssetRecord* record = AssetDatabase::Get().GetByUUID(assetUUID);
			if (!record) return;
			auto graphPath = record->FilePath;
			graphPath.replace_extension(".axegraph");
			if (!std::filesystem::exists(graphPath)) return;
			std::ifstream file(graphPath);
			try
			{
				nlohmann::json j = nlohmann::json::parse(file);
				s_CachedGraph = std::make_unique<MaterialGraph>();
				s_CachedGraph->Deserialize(j);
				s_CachedGraph->BuildNodes();
			}
			catch (...) { s_CachedGraph.reset(); return; }
		}

		if (!s_CachedGraph || s_CachedGraph->GetNodes().empty()) return;

		Node* outputNode = nullptr;
		for (auto& node : s_CachedGraph->GetNodes())
			if (node->Name == "Material Output") { outputNode = node.get(); break; }
		if (!outputNode) return;

		ImGui::Separator();
		ImGui::Text("Parâmetros:");
		ImGui::Spacing();

		bool anyEditable = false;
		for (auto& node : s_CachedGraph->GetNodes())
		{
			if (node->Name == "Material Output" || node->Type == NodeType::Comment) continue;
			ImGui::PushID(node->ID.Get());

			if (node->Name == "Float" && node->IsConstant)
			{
				std::string pinLabel = FindOutputPinLabel(s_CachedGraph.get(), node.get());
				if (!pinLabel.empty())
				{
					anyEditable = true;
					ImGui::TextDisabled("%s [Float]", pinLabel.c_str());
					ImGui::SetNextItemWidth(-1);
					if (ImGui::DragFloat(("##f" + std::to_string(node->ID.Get())).c_str(), &node->Value.FloatVal, 0.01f, 0.0f, 1.0f))
						SaveCachedGraph(assetUUID, &registry, entity);
				}
			}
			else if (node->Name == "Color" && node->IsConstant)
			{
				std::string pinLabel = FindOutputPinLabel(s_CachedGraph.get(), node.get());
				if (!pinLabel.empty())
				{
					anyEditable = true;
					ImGui::TextDisabled("%s [Color]", pinLabel.c_str());
					ImGui::SetNextItemWidth(-1);
					if (ImGui::ColorEdit4(("##c" + std::to_string(node->ID.Get())).c_str(), &node->Value.Vec4Val.x))
						SaveCachedGraph(assetUUID, &registry, entity);
				}
			}
			else if (node->Name == "Texture Sample")
			{
				std::string pinLabel = FindOutputPinLabel(s_CachedGraph.get(), node.get());
				if (!pinLabel.empty())
				{
					anyEditable = true;
					ImGui::TextDisabled("%s [Texture]", pinLabel.c_str());
					AssetPicker::Draw((pinLabel + "_" + std::to_string(node->ID.Get())).c_str(),
						node->Value.TextureUUID, { AssetType::Texture },
						[&](const AssetRecord& record)
						{
							node->Value.TextureVal = Texture2D::Create(record.FilePath.string());
							node->Value.TextureUUID = record.UUID;
							SaveCachedGraph(assetUUID, &registry, entity);
						});
				}
			}
			ImGui::PopID();
		}

		if (!anyEditable)
		{
			ImGui::TextDisabled("Nenhum parâmetro editável.");
			ImGui::TextDisabled("Adicione nodes Float, Color ou Texture Sample no graph.");
		}
	}

	void InspectorWindow::MarkGraphCacheDirty() { s_GraphCacheDirty = true; }

	void InspectorWindow::DrawReflectionProbe(ReflectionProbeComponent& rp)
	{
		ImGui::TextDisabled("Cubemap local pro reflexo especular.");
		ImGui::TextDisabled("POSICAO = ponto de captura (centro da sala);");
		ImGui::TextDisabled("ESCALA = caixa de influencia (paredes).");
		ImGui::Spacing();

		ImGui::Checkbox("Ativo", &rp.Settings.Enabled);

		const char* resNames[] = { "64", "128", "256" };
		int resIdx = rp.Settings.Resolution >= 256 ? 2
			: rp.Settings.Resolution >= 128 ? 1 : 0;
		if (ImGui::Combo("Resolucao", &resIdx, resNames, 3))
			rp.Settings.Resolution = resIdx == 2 ? 256 : resIdx == 1 ? 128 : 64;

		ImGui::DragFloat("Intensidade", &rp.Settings.Intensity, 0.01f, 0.0f, 4.0f);
		ImGui::DragFloat("Feather (m)", &rp.Settings.Feather, 0.05f, 0.01f, 10.0f);
		ImGui::DragFloat("Bake Far Clip (m)", &rp.Settings.BakeFarClip, 1.0f, 10.0f, 1000.0f);

		ImGui::Checkbox("Box Projection", &rp.Settings.BoxProjection);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Ancora o reflexo nas paredes da caixa (parallax\n"
				"correction) em vez de 'infinitamente longe' — essencial\n"
				"pra reflexo de interior parecer correto.");

		ImGui::Spacing();
		if (ImGui::Button("Bake", ImVec2(-1, 0)))
			rp.BakeRequested = true;
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Captura + prefilter (rapido). Recapturado\n"
				"automaticamente ao abrir a cena.");

		if (rp.BakeRequested)
			ImGui::TextDisabled("Captura agendada para o proximo frame...");
		else if (rp.Capture && rp.Capture->IsValid())
			ImGui::TextDisabled("Captura atual: %dpx", rp.Settings.Resolution);
		else
			ImGui::TextDisabled("Nunca capturado.");
	}

	void InspectorWindow::DrawProbeVolume(ProbeVolumeComponent& pv)
	{
		ImGui::TextDisabled("Grid de light probes bakeadas (SH L1).");
		ImGui::TextDisabled("Interiores escurecem sozinhos; exteriores");
		ImGui::TextDisabled("ganham 1 bounce do sol. ESCALA = tamanho (m).");
		ImGui::Spacing();

		ImGui::Checkbox("Ativo", &pv.Settings.Enabled);

		ImGui::SliderInt3("Probes X/Y/Z", &pv.Settings.Resolution.x, 2, 16);
		int total = pv.Settings.Resolution.x * pv.Settings.Resolution.y * pv.Settings.Resolution.z;
		ImGui::TextDisabled("Total: %d probes (%d renders no bake)", total, total * 6);

		ImGui::DragFloat("Intensidade", &pv.Settings.Intensity, 0.01f, 0.0f, 4.0f);
		ImGui::DragFloat("Feather (m)", &pv.Settings.Feather, 0.05f, 0.01f, 10.0f);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Transicao na borda da caixa de volta\npro IBL global do ceu.");
		ImGui::DragFloat("Bake Far Clip (m)", &pv.Settings.BakeFarClip, 1.0f, 10.0f, 1000.0f);

		ImGui::SliderInt("Bounces", &pv.Settings.Bounces, 1, 3);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("1 = sol -> parede -> probe.\n"
				"2 = sol -> parede -> chao -> probe (luz que 'dobra\n"
				"a esquina' e preenche cantos — GI de verdade).\n"
				"Tempo de bake escala linear com os bounces.");
		ImGui::Checkbox("Mostrar Probes (gizmo)", &pv.Settings.ShowProbes);

		ImGui::Checkbox("Auto Bake ao Abrir Cena", &pv.Settings.AutoBakeOnLoad);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("O grid e salvo em .axeprobes junto com a cena e\n"
				"recarregado no load — a cena abre com o GI pronto.\n"
				"Este rebake automatico so age quando o arquivo nao\n"
				"existe ou nao bate com as Settings. Play/Stop nunca\n"
				"rebakeia — o grid sobrevive ao snapshot.");

		ImGui::Checkbox("Ocluir Sol (Occlusion Probes)", &pv.Settings.OccludeSunlight);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Usa a visibilidade do ceu das probes (geometrica,\n"
				"independente da hora) pra bloquear a luz DIRETA do\n"
				"sol em interiores — sem depender de shadow map.\n"
				"Sala fechada = sol zero em qualquer horario.");

		ImGui::Spacing();
		if (ImGui::Button("Bake", ImVec2(-1, 0)))
			pv.BakeRequested = true;
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Renderiza a cena de cada probe (6 faces) e\n"
				"projeta em SH. Operacao bloqueante de editor.\n"
				"Rebakear apos mover geometria, luzes ou o volume.");

		if (pv.BakeRequested)
			ImGui::TextDisabled("Bake agendado para o proximo frame...");
		else if (pv.Grid && pv.Grid->IsValid())
			ImGui::TextDisabled("Bake atual: %dx%dx%d",
				pv.Grid->Resolution.x, pv.Grid->Resolution.y, pv.Grid->Resolution.z);
		else
			ImGui::TextDisabled("Nunca bakeado.");
	}

	void InspectorWindow::DrawInteriorVolume(InteriorVolumeComponent& iv)
	{
		ImGui::TextDisabled("Bloqueia sol + ambient/IBL dentro da caixa.");
		ImGui::TextDisabled("A ESCALA do Transform define o tamanho (m).");
		ImGui::TextDisabled("Point Lights e particulas nao sao afetadas.");
		ImGui::Spacing();

		ImGui::Checkbox("Ativo", &iv.Data.Enabled);

		ImGui::SliderFloat("Intensidade", &iv.Data.Intensity, 0.0f, 1.0f);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("0 = sem efeito, 1 = bloqueio total da luz externa");

		ImGui::DragFloat("Suavizacao da Borda (m)", &iv.Data.BlendDistance, 0.05f, 0.01f, 10.0f);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("A luz externa 'vaza' suavemente por esta distancia\n"
				"para dentro da caixa. Posicione a face da caixa no vao\n"
				"da porta/janela pra transicao ficar natural.");

		ImGui::Spacing();
		ImGui::Checkbox("Bloquear Luz Direta (Sol)", &iv.Data.AffectDirect);
		ImGui::Checkbox("Bloquear Ambient / IBL", &iv.Data.AffectAmbient);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Desmarque a Luz Direta pra deixar o sol entrar\n"
				"pela janela (via shadow map) matando so o ambient\n"
				"que vaza pelas paredes.");
	}

	void InspectorWindow::DrawPostProcess(PostProcessComponent& pp)
	{
		// (título vem do CollapsingHeader da seção)
		ImGui::Checkbox("Global", &pp.IsGlobal);

		ImGui::Separator();
		ImGui::Text("Tone Mapping");
		ImGui::DragFloat("Exposure", &pp.Settings.Exposure, 0.01f, 0.1f, 10.0f);
		const char* toneModes[] = { "Reinhard", "ACES" };
		ImGui::Combo("Tone Map", &pp.Settings.ToneMapMode, toneModes, 2);

		ImGui::Separator();
		ImGui::Text("Bloom");
		ImGui::Checkbox("Bloom Ativo", &pp.Settings.BloomEnabled);
		if (pp.Settings.BloomEnabled)
		{
			ImGui::DragFloat("Threshold", &pp.Settings.BloomThreshold, 0.01f, 0.0f, 3.0f);
			ImGui::DragFloat("Intensidade", &pp.Settings.BloomIntensity, 0.01f, 0.0f, 3.0f);
			ImGui::SliderInt("Blur Passes", &pp.Settings.BloomBlurPasses, 1, 10);
		}

		ImGui::Separator();
		ImGui::Text("SSAO");
		ImGui::Checkbox("SSAO Ativo", &pp.SSAO.Enabled);
		if (pp.SSAO.Enabled)
		{
			ImGui::DragFloat("Radius", &pp.SSAO.Radius, 0.01f, 0.01f, 2.0f);
			ImGui::DragFloat("Bias", &pp.SSAO.Bias, 0.001f, 0.0f, 0.1f);
			ImGui::DragFloat("Power", &pp.SSAO.Power, 0.1f, 0.5f, 8.0f);
			ImGui::SliderInt("Kernel Size", &pp.SSAO.KernelSize, 8, 64);
			ImGui::Checkbox("Debug (mostra oclusão)", &pp.SSAO.Debug);
		}

		// ── TAA ──────────────────────────────────────────────────────────────
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("TAA (Temporal Anti-Aliasing)");
		ImGui::Checkbox("TAA Ativo", &pp.Settings.TAA.Enabled);
		if (pp.Settings.TAA.Enabled)
		{
			ImGui::Separator();
			ImGui::TextDisabled("Base");
			ImGui::DragFloat("Blend Factor", &pp.Settings.TAA.BlendFactor, 0.005f, 0.02f, 0.5f);
			ImGui::TextDisabled("  0.05=suave/ghost  0.15=rapido/ruido");

			ImGui::Separator();
			ImGui::TextDisabled("Emissivos / Animados");
			ImGui::DragFloat("Lum Min", &pp.Settings.TAA.EmissiveLumMin, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Lum Max", &pp.Settings.TAA.EmissiveLumMax, 0.01f, 0.1f, 3.0f);
			ImGui::DragFloat("Blend Max Emissivo", &pp.Settings.TAA.EmissiveBlendMax, 0.01f, 0.1f, 1.0f);
			ImGui::DragFloat("Sensib. Temporal", &pp.Settings.TAA.TemporalSensitivity, 0.1f, 0.0f, 10.0f);
			ImGui::TextDisabled("  Lum Min/Max: range de luminancia para detectar emissivo");
			ImGui::TextDisabled("  Blend Max: o quanto emissivos ignoram o historico");
			ImGui::TextDisabled("  Sensib. Temporal: reatividade a mudancas de cor");

			ImGui::Separator();
			ImGui::Checkbox("Sharpening", &pp.Settings.TAA.Sharpen);
			if (pp.Settings.TAA.Sharpen)
				ImGui::DragFloat("Sharpen Amount", &pp.Settings.TAA.SharpenAmount, 0.05f, 0.f, 1.f);
		}

		// ── SSR — Screen Space Reflections ─────────────────────────────────────
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("SSR (Screen Space Reflections)");
		ImGui::Checkbox("SSR Ativo", &pp.Settings.SSR.Enabled);
		if (pp.Settings.SSR.Enabled)
		{
			ImGui::DragFloat("Max Distance", &pp.Settings.SSR.MaxDistance, 0.5f, 1.f, 100.f);
			ImGui::SliderInt("Max Steps", &pp.Settings.SSR.MaxSteps, 8, 128);
			ImGui::SliderInt("Binary Refine", &pp.Settings.SSR.BinaryRefine, 0, 10);
			ImGui::DragFloat("Thickness", &pp.Settings.SSR.Thickness, 0.05f, 0.05f, 5.f);
			ImGui::DragFloat("Max Roughness", &pp.Settings.SSR.MaxRoughness, 0.02f, 0.f, 1.f);
			ImGui::DragFloat("Intensity", &pp.Settings.SSR.Intensity, 0.05f, 0.f, 2.f);
			ImGui::DragFloat("Edge Fade", &pp.Settings.SSR.EdgeFade, 0.01f, 0.f, 0.5f);
			ImGui::TextDisabled("  Superficies lisas (baixa roughness) refletem a cena.");
		}

		// ── Volumetric Fog ────────────────────────────────────────────────────
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Volumetric Fog");
		ImGui::Checkbox("Fog Ativo", &pp.Settings.Fog.Enabled);
		if (pp.Settings.Fog.Enabled)
		{
			ImGui::ColorEdit3("Cor do Fog", &pp.Settings.Fog.FogColor.x);
			ImGui::DragFloat("Densidade", &pp.Settings.Fog.Density, 0.001f, 0.0f, 1.0f);
			ImGui::DragFloat("Scatter Strength", &pp.Settings.Fog.ScatterStrength, 0.01f, 0.0f, 2.0f);
			ImGui::DragFloat("Ambient Strength", &pp.Settings.Fog.AmbientStrength, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Fog Start", &pp.Settings.Fog.FogStart, 0.5f, 0.0f, 50.0f);
			ImGui::DragFloat("Fog End", &pp.Settings.Fog.FogEnd, 1.0f, 1.0f, 500.0f);
			ImGui::DragFloat("Height Base", &pp.Settings.Fog.HeightBase, 0.1f, -20.0f, 20.0f);
			ImGui::DragFloat("Height Falloff", &pp.Settings.Fog.HeightFalloff, 0.01f, 0.0f, 2.0f);
			ImGui::SliderInt("Ray Steps", &pp.Settings.Fog.Steps, 4, 32);
			ImGui::DragFloat("Jitter", &pp.Settings.Fog.StepJitter, 0.01f, 0.0f, 1.0f);
			ImGui::TextDisabled("Ponto de luz ilumina o volume (scatter).");
		}
	}

	// ==================== Física ====================

		// Helper — header de componente com botão X para remover
	void InspectorWindow::DrawRigidbody(entt::entity entity, entt::registry& registry)
	{
		auto* rb = registry.try_get<RigidbodyComponent>(entity);
		if (!rb) return;

		bool remove = false;
		bool open = DrawComponentHeader("Rigidbody", entity, 0, &remove);
		if (remove) { m_PendingRemove = PendingRemove::Rigidbody; m_PendingRemoveEntity = entity; return; }
		if (!open) return;

		const char* types[] = { "Static", "Dynamic", "Kinematic" };
		int typeIdx = (int)rb->Type;
		if (ImGui::Combo("Tipo", &typeIdx, types, 3))
			rb->Type = (BodyType)typeIdx;

		if (rb->Type != BodyType::Static)
		{
			ImGui::DragFloat("Massa", &rb->Mass, 0.1f, 0.01f, 1000.0f);
			ImGui::DragFloat("Friccao", &rb->Friction, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Restitution", &rb->Restitution, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Linear Damp", &rb->LinearDamping, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Angular Damp", &rb->AngularDamping, 0.01f, 0.0f, 1.0f);
			ImGui::Checkbox("Usar Gravidade", &rb->UseGravity);
			ImGui::Spacing();
			ImGui::TextDisabled("Travar Rotacao:");
			ImGui::SameLine(); ImGui::Checkbox("X##rx", &rb->LockRotX);
			ImGui::SameLine(); ImGui::Checkbox("Y##ry", &rb->LockRotY);
			ImGui::SameLine(); ImGui::Checkbox("Z##rz", &rb->LockRotZ);
		}
	}

	void InspectorWindow::DrawCollider(entt::entity entity, entt::registry& registry)
	{
		auto* col = registry.try_get<ColliderComponent>(entity);
		if (!col) return;

		bool remove = false;
		bool open = DrawComponentHeader("Collider", entity, 1, &remove);
		if (remove) { m_PendingRemove = PendingRemove::Collider; m_PendingRemoveEntity = entity; return; }
		if (!open) return;

		const char* shapes[] = { "Box", "Sphere", "Capsule", "Mesh (Static)", "Convex Hull" };
		int shapeIdx = (int)col->Shape;
		if (ImGui::Combo("Forma", &shapeIdx, shapes, 5))
			col->Shape = (ColliderShape)shapeIdx;

		// Dica de uso
		auto* rb = registry.try_get<RigidbodyComponent>(entity);
		if (rb && rb->Type != BodyType::Static &&
			(col->Shape == ColliderShape::Box ||
				col->Shape == ColliderShape::Sphere ||
				col->Shape == ColliderShape::Capsule ||
				col->Shape == ColliderShape::ConvexHull))
		{
			// ok — shape compatível com dynamic
		}
		else if (col->Shape == ColliderShape::Mesh && rb && rb->Type != BodyType::Static)
		{
			// já mostrado abaixo
		}
		else if (rb && rb->Type != BodyType::Static && col->Shape == ColliderShape::Box)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
			ImGui::TextDisabled("Dica: use Convex Hull para formato exato dinamico");
			ImGui::PopStyleColor();
		}

		ImGui::Checkbox("Is Trigger", &col->IsTrigger);
		ImGui::Checkbox("Debug Wireframe", &col->ShowDebug);
		ImGui::DragFloat3("Offset", &col->Offset.x, 0.01f);

		switch (col->Shape)
		{
		case ColliderShape::Box:
			ImGui::DragFloat3("Half Extent", &col->HalfExtent.x, 0.01f, 0.01f, 100.0f);
			break;
		case ColliderShape::Sphere:
			ImGui::DragFloat("Raio", &col->Radius, 0.01f, 0.01f, 100.0f);
			break;
		case ColliderShape::Capsule:
			ImGui::DragFloat("Altura", &col->Height, 0.01f, 0.1f, 10.0f);
			ImGui::DragFloat("Raio Capsule", &col->CapsuleRadius, 0.01f, 0.01f, 5.0f);
			break;
		case ColliderShape::Mesh:
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.1f, 1.0f));
			ImGui::TextWrapped("Mesh exato do objeto. Use apenas com Rigidbody Static.");
			ImGui::PopStyleColor();
			// Avisa se Rigidbody for Dynamic
			if (auto* rb = registry.try_get<RigidbodyComponent>(entity))
				if (rb->Type != BodyType::Static)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
					ImGui::TextWrapped("AVISO: Mesh Collider requer Rigidbody Static!");
					ImGui::PopStyleColor();
				}
			break;
		case ColliderShape::ConvexHull:
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.1f, 1.0f));
			ImGui::TextWrapped("Convex Hull do mesh. Funciona com Dynamic e Kinematic.");
			ImGui::PopStyleColor();
			break;
		}
	}

	void InspectorWindow::DrawCharacterController(entt::entity entity, entt::registry& registry)
	{
		auto* cc = registry.try_get<CharacterControllerComponent>(entity);
		if (!cc) return;

		bool remove = false;
		bool open = DrawComponentHeader("Character Controller", entity, 2, &remove);
		if (remove) { m_PendingRemove = PendingRemove::CharacterController; m_PendingRemoveEntity = entity; return; }
		if (!open) return;

		ImGui::DragFloat("Altura", &cc->Height, 0.01f, 0.5f, 5.0f);
		ImGui::DragFloat("Raio", &cc->Radius, 0.01f, 0.1f, 2.0f);
		ImGui::DragFloat("Max Slope", &cc->MaxSlopeAngle, 0.5f, 0.0f, 89.0f);
		ImGui::DragFloat("Step Height", &cc->StepHeight, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Max Speed", &cc->MaxSpeed, 0.1f, 0.0f, 50.0f);
		ImGui::DragFloat("Jump Force", &cc->JumpForce, 0.1f, 0.0f, 50.0f);

		ImGui::DragFloat3("Capsule Offset", &cc->CapsuleOffset.x, 0.01f);

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Move a capsula sem mover o personagem.\nEm metros; Y ja parte de meia altura.");

		ImGui::Checkbox("Debug Wireframe", &cc->ShowDebug);

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Desenha a capsula usada pela fisica.\nA base dela fica nos PES do personagem.");

		ImGui::Checkbox("Orient Rotation To Movement", &cc->OrientRotationToMovement);

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("O personagem gira para a direcao em que anda.\n"
				"A animacao de caminhar pra frente passa a servir\npara todas as direcoes.");

		if (cc->OrientRotationToMovement)
			ImGui::DragFloat("Rotation Rate", &cc->RotationRate, 10.0f, 0.0f, 2000.0f, "%.0f deg/s");

		if (cc->IsCreated)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("Grounded: %s", cc->IsGrounded ? "Sim" : "Nao");
		}
	}

} // namespace axe