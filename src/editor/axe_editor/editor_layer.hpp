#pragma once

#include "axe/layers/layer.hpp"
#include "editor_ui.hpp"

#include "GLFW/glfw3.h"
#include <iostream>

#include "axe/graphics/renderer/viewport_renderer.hpp"
#include "axe/graphics/framebuffer.hpp"
#include "axe/scene//scene.hpp"
#include "axe/mesh/mesh_factory.hpp"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/glm.hpp>

#include "axe/graphics/editor_camera.hpp"

#include "editor_context.hpp"


#include "axe/mesh/mesh_loader.hpp"
#include "axe/mesh/primitive_uuid.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/project/project_manager.hpp"
#include <filesystem>

#include "axe/scene/scene_serializer.hpp"
#include "editor_icon_library.hpp"


#include "axe/graphics/game_camera.hpp"
#include "axe/events/key_event.hpp"
#include "axe/input/key_codes.hpp"

#include "material_editor_window.hpp"

#include "node_graph/material_graph.hpp"
#include "axe/material/material_compiler.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

#include "material_thumbnail_renderer.hpp"
namespace axe
{

	MaterialThumbnailRenderer m_ThumbnailRenderer;

	// EditorLayer contém toda a UI do editor
	// É uma layer normal — fica abaixo do ImGuiLayer

	class EditorLayer : public Layer
	{
		enum class EditorState { Edit, Play, Pause };
		// membros privados — adiciona:
		EditorState              m_EditorState = EditorState::Edit;
		GameCamera               m_GameCamera;
		std::string              m_SceneSnapshot; // JSON da cena antes do Play
		//float                    m_DeltaTime = 0.0f;

		class EditorCamera;



	public:
		EditorLayer()
			: Layer("EditorLayer"), m_EditorUI(std::make_unique<EditorUI>())
		{

		}

		void OnAttach() override
		{
			AXE_EDITOR_INFO("EditorLayer attached");

			// 1. Cria a cena
			m_Scene = std::make_unique<Scene>();
			m_Context.ActiveScene = m_Scene.get();
			m_Context.SelectedEntity = entt::null;
			m_EditorUI->SetContext(&m_Context);



			// 2. Carrega cena padrão se existir, senão cria cena vazia
			//if (ProjectManager::Get().HasStartScene())
			//{
			//	SceneSerializer::Deserialize(
			//		ProjectManager::Get().GetStartScenePath().string(), *m_Scene);
			//}
			//else
			//{
			//	// Tenta carregar cena padrão do engine
			//	std::string defaultScene = "resources/default_scene/main.axescene";
			//	AXE_EDITOR_INFO("Procurando cena padrão em: {}",
			//		std::filesystem::absolute(defaultScene).string());

			//	if (std::filesystem::exists(defaultScene))
			//	{
			//		AXE_EDITOR_INFO("Carregando cena padrão do engine.");
			//		SceneSerializer::Deserialize(defaultScene, *m_Scene);
			//	}
			//	else
			//	{
			//		// Cena realmente vazia
			//		AXE_EDITOR_INFO("Arquivo não encontrado: {}", defaultScene);
			//		m_Scene->CreateLight("Directional Light");
			//		AXE_EDITOR_INFO("Cena vazia criada.");
			//	}
			//}

			// 3. Registra callback do AssetBrowser
			m_EditorUI->GetAssetBrowser()->SetFileDropCallback(
				[this](const std::string& filepath)
				{
					std::string uuid = AssetDatabase::Get().Register(filepath);

					LoadedAsset asset = MeshLoader::Load(filepath);
					if (!asset.MeshData)
					{
						AXE_CORE_ERROR("EditorLayer: falha ao carregar '{}'", filepath);
						return;
					}

					std::string name = std::filesystem::path(filepath).stem().string();
					auto& registry = m_Scene->GetRegistry();
					entt::entity entity = m_Scene->CreateEntity(name);

					auto& mc = registry.emplace<MeshComponent>(entity);
					mc.Data = asset.MeshData;
					mc.AssetUUID = uuid;

					if (asset.MaterialData)
						registry.emplace<MaterialComponent>(entity, asset.MaterialData);

					m_Context.Select(entity);

					if (ProjectManager::Get().HasProject())
						AssetDatabase::Get().Save(ProjectManager::Get().GetCurrent().RootPath);

					AXE_CORE_INFO("EditorLayer: '{}' importado. UUID: {}", name, uuid);
				}
			);

			m_EditorUI->GetAssetBrowser()->SetAssetOpenCallback([this](const AssetRecord& record)
				{
					if (record.Type == AssetType::Material)
					{
						auto matAsset = MaterialAsset::LoadFromFile(record.FilePath);
						if (matAsset)
							m_EditorUI->m_MaterialEditorWindow.OpenMaterial(matAsset);
						m_EditorUI->m_MaterialEditorWindow.OpenMaterial(matAsset);
					}
				});



			auto instantiate = [this](const std::string& uuid)
				{
					const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
					if (!record) return;

					auto& registry = m_Scene->GetRegistry(); // ← move para cá

					// Material — aplica no mesh selecionado
					if (record->Type == AssetType::Material)
					{


						if (!m_Context.HasSelection()) return;

						entt::entity selected = m_Context.SelectedEntity;
						if (!registry.valid(selected)) return;

						if (!registry.all_of<MeshComponent>(selected)) return;




						auto matAsset = MaterialAsset::LoadFromFile(record->FilePath);
						if (!matAsset) return;

						auto material = matAsset->GetMaterial();

						// Usa o mesmo callback do SceneSerializer para recompilar
						// (aplica forward + geometry shader + texturas direto no material)
						if (SceneSerializer::GetMaterialRecompileCallback())
							SceneSerializer::GetMaterialRecompileCallback()(uuid, material.get());

						// Transfere texturas do grafo
						auto graphPath = record->FilePath;
						graphPath.replace_extension(".axegraph");
						if (std::filesystem::exists(graphPath))
						{
							std::ifstream file(graphPath);
							try
							{
								nlohmann::json j = nlohmann::json::parse(file);
								MaterialGraph graph;
								graph.Deserialize(j);

								int slot = 0;
								for (auto& node : graph.GetNodes())
								{
									if (node->Name != "Texture Sample") continue;
									if (!node->Value.TextureVal) { ++slot; continue; }

									bool isConnected = false;
									for (auto& output : node->Outputs)
										for (auto& link : graph.GetLinks())
											if (link.StartPin == output.ID)
												isConnected = true;

									if (isConnected && slot == 0)
									{
										material->AlbedoMap = node->Value.TextureVal;
										material->AlbedoUUID = node->Value.TextureUUID;
									}
									++slot;
								}
							}
							catch (...) {}
						}

						auto matComp = MaterialComponent{ material };
						matComp.MaterialAssetUUID = uuid;

						if (registry.all_of<MaterialComponent>(selected))
							registry.get<MaterialComponent>(selected) = matComp;
						else
							registry.emplace<MaterialComponent>(selected, matComp);


						AXE_EDITOR_INFO("Material '{}' aplicado.", record->Name);
						return;
					}

					// Mesh primitivo
					if (MeshFactory::IsPrimitive(uuid))
					{
						auto mesh = MeshFactory::CreateByUUID(uuid);
						if (!mesh) return;

						auto entity = m_Scene->CreateEntity(record->Name);
						auto& mc = registry.emplace<MeshComponent>(entity);
						mc.Data = mesh;
						mc.AssetUUID = uuid;
						m_Context.Select(entity);
						return;
					}

					// Mesh de arquivo
					LoadedAsset asset = MeshLoader::Load(record->FilePath.string());
					if (!asset.MeshData) return;

					auto entity = m_Scene->CreateEntity(record->Name);
					auto& mc = registry.emplace<MeshComponent>(entity);
					mc.Data = asset.MeshData;
					mc.AssetUUID = uuid;

					if (asset.MaterialData)
						registry.emplace<MaterialComponent>(entity, asset.MaterialData);

					m_Context.Select(entity);
				};

			m_EditorUI->GetAssetBrowser()->SetInstantiateCallback(instantiate);
			//m_EditorUI->GetViewport()->SetAssetDropCallback(instantiate);
			m_EditorUI->GetViewport()->SetAssetDropCallback(
				[this](const std::string& uuid, float mouseX, float mouseY)
				{
					const AssetRecord* record = AssetDatabase::Get().GetByUUID(uuid);
					if (!record) return;

					auto& registry = m_Scene->GetRegistry();

					// Se for material, usa picking para encontrar o mesh sob o cursor
					if (record->Type == AssetType::Material)
					{
						uint32_t pickID = m_ViewportRenderer->PickObject(mouseX, mouseY);
						if (pickID != 0)
						{
							entt::entity picked = (entt::entity)pickID;
							if (registry.valid(picked))
								m_Context.Select(picked);
						}
					}

					// Chama o instantiate diretamente — sem referência ao lambda local
					if (MeshFactory::IsPrimitive(uuid))
					{
						auto mesh = MeshFactory::CreateByUUID(uuid);
						if (!mesh) return;
						auto entity = m_Scene->CreateEntity(record->Name);
						auto& mc = registry.emplace<MeshComponent>(entity);
						mc.Data = mesh;
						mc.AssetUUID = uuid;
						m_Context.Select(entity);
						return;
					}

					if (record->Type == AssetType::Material)
					{
						if (!m_Context.HasSelection()) return;
						entt::entity selected = m_Context.SelectedEntity;
						if (!registry.valid(selected)) return;
						if (!registry.all_of<MeshComponent>(selected)) return;

						auto matAsset = MaterialAsset::LoadFromFile(record->FilePath);
						if (!matAsset) return;

						auto material = matAsset->GetMaterial();

						// Recompila forward + geometry shader + texturas
						if (SceneSerializer::GetMaterialRecompileCallback())
							SceneSerializer::GetMaterialRecompileCallback()(uuid, material.get());

						auto matComp = MaterialComponent{ material };
						matComp.MaterialAssetUUID = uuid;

						if (registry.all_of<MaterialComponent>(selected))
							registry.get<MaterialComponent>(selected) = matComp;
						else
							registry.emplace<MaterialComponent>(selected, matComp);
						return;
					}

					// Mesh de arquivo
					LoadedAsset asset = MeshLoader::Load(record->FilePath.string());

					if (!asset.MeshData) return;
					auto entity = m_Scene->CreateEntity(record->Name);
					auto& mc = registry.emplace<MeshComponent>(entity);
					mc.Data = asset.MeshData;
					mc.AssetUUID = uuid;
					if (asset.MaterialData)
						registry.emplace<MaterialComponent>(entity, asset.MaterialData);
					m_Context.Select(entity);
				});

			// 4. Inicializa o viewport
			ViewportWindow* viewport = m_EditorUI->GetViewport();
			if (viewport)
			{
				viewport->Initialize();
				viewport->SetGuizmoCallback([this](const glm::vec2& min, const glm::vec2& max)
					{
						m_ViewportRenderer->DrawGuizmo(min, max);
					});
			}
			else
			{
				AXE_EDITOR_ERROR("ViewportWindow is null during OnAttach!");
			}


			viewport->SetPlayStateCallback([this]() -> int
				{
					if (m_EditorState == EditorState::Play)  return 1;
					if (m_EditorState == EditorState::Pause) return 2;
					return 0;
				});

			viewport->SetPlayActionCallback([this](int action)
				{
					if (action == 0) // Play
					{
						if (m_EditorState == EditorState::Edit)
						{
							EnterPlay();
						}
						else if (m_EditorState == EditorState::Pause)
						{
							m_Context.ClearSelection();
							ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
							EditorApp::Get().GetWindow().CaptureCursor(true);
							m_GameCamera.MouseCaptured = true;
							m_GameCamera.m_FirstMouse = true;
							m_ViewportRenderer->SetGameCamera(&m_GameCamera);
							m_EditorState = EditorState::Play;
							AXE_EDITOR_INFO("Modo Play retomado.");
						}
					}
					else if (action == 1) EnterPause();
					else if (action == 2) EnterEdit();
				});

			// 5. Inicializa o renderer
			m_ViewportRenderer = std::make_unique<axe::ViewportRenderer>();
			m_ViewportRenderer->Initialize();
			viewport->SetViewportRenderer(m_ViewportRenderer.get());
			m_ViewportRenderer->SetScene(m_Scene.get());
			m_ViewportRenderer->SetSelectedEntity(&m_Context.SelectedEntity);
			m_ViewportRenderer->SetEnvironment(&m_Environment);

			m_EditorUI->SetViewportRenderer(m_ViewportRenderer.get());

			m_ThumbnailRenderer.Initialize();
			m_EditorUI->GetAssetBrowser()->SetThumbnailRenderer(&m_ThumbnailRenderer);
			m_EditorUI->m_MaterialEditorWindow.SetThumbnailRenderer(&m_ThumbnailRenderer);

			SceneSerializer::SetMaterialRecompileCallback(
				[](const std::string& assetUUID, Material* material)
				{
					if (!material) return;

					const AssetRecord* record = AssetDatabase::Get().GetByUUID(assetUUID);
					if (!record) return;

					auto graphPath = record->FilePath;
					graphPath.replace_extension(".axegraph");
					if (!std::filesystem::exists(graphPath)) return;

					std::ifstream file(graphPath);
					try
					{
						nlohmann::json j = nlohmann::json::parse(file);
						MaterialGraph graph;
						graph.Deserialize(j);

						auto result = MaterialCompiler::Compile(&graph);
						if (!result.Success) return;

						// Forward shader
						auto forwardShader = Shader::Create(result.VertexShader, result.FragmentShader);
						if (forwardShader)
							material->SetShader(forwardShader);

						// Geometry shader (deferred G-Buffer)
						if (!result.GeometryFragShader.empty())
						{
							auto geometryShader = Shader::Create(result.VertexShader, result.GeometryFragShader);
							if (geometryShader)
								material->SetGeometryShader(geometryShader);
						}

						// Texturas
						material->SamplerTextures = result.SamplerTextures;
						if (result.AlbedoTexture)
							material->AlbedoMap = result.AlbedoTexture;
						if (result.NormalTexture)
							material->NormalMap = result.NormalTexture;
					}
					catch (...) {}
				});

			m_Environment.LoadHDRI("resources/quarry_04_puresky_2k.hdr");
			EditorIconLibrary::Get().Load("resources");

			m_EditorUI->m_MaterialEditorWindow.Initialize();


		}

		void OnDetach() override
		{

			m_EditorUI.reset();

		}

		void OnUpdate(float deltaTime) override
		{


			// Carrega a cena no primeiro frame (após OpenGL estar pronto)
			if (!m_SceneLoaded)
			{
				m_SceneLoaded = true;

				if (ProjectManager::Get().HasStartScene())
				{
					SceneSerializer::Deserialize(
						ProjectManager::Get().GetStartScenePath().string(), *m_Scene);
				}
				else
				{
					std::string defaultScene = "resources/default_scene/main.axescene";

					if (std::filesystem::exists(defaultScene))
					{

						SceneSerializer::Deserialize(defaultScene, *m_Scene);
					}
					else
					{

						m_Scene->CreateLight("Directional Light");

						auto ppEntity = m_Scene->CreateEntity("Post Process Volume");
						auto& registry = m_Scene->GetRegistry();
						registry.emplace<PostProcessComponent>(ppEntity);

					}
				}
			}

			m_DeltaTime = deltaTime;

			m_FPSAccumulator += 1.0f / deltaTime;
			m_FPSSamples++;
			if (m_FPSSamples >= 60)
			{
				m_FPS = m_FPSAccumulator / m_FPSSamples;
				m_FPSAccumulator = 0.0f;
				m_FPSSamples = 0;
			}

			// PROTEÇÃO
			if (m_EditorUI && m_EditorUI->GetAssetBrowser())
			{
				m_EditorUI->GetAssetBrowser()->Update();
			}
			else
			{
				AXE_CORE_ERROR("AssetBrowser é nullptr!");
			}




			if (m_EditorState == EditorState::Play)
			{
				m_GameCamera.OnUpdate(deltaTime, &EditorApp::Get().GetWindow());

				bool escNow = EditorApp::Get().GetWindow().IsKeyDown((int)Key::Escape);
				if (escNow && !m_EscWasPressed)
					EnterPause();
				m_EscWasPressed = escNow;
			}
			else
			{
				m_EscWasPressed = false;
			}



		}

		void DrawPlayToolbar()
		{
			ImGui::SetNextWindowPos(
				ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 30.0f),
				ImGuiCond_Always, ImVec2(0.5f, 0.0f)
			);
			ImGui::SetNextWindowSize(ImVec2(160, 40), ImGuiCond_Always);
			ImGui::SetNextWindowBgAlpha(0.85f);

			ImGuiWindowFlags flags =
				ImGuiWindowFlags_NoDecoration |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoDocking |
				ImGuiWindowFlags_NoSavedSettings;

			ImGui::Begin("##toolbar", nullptr, flags);

			bool isEdit = m_EditorState == EditorState::Edit;
			bool isPlay = m_EditorState == EditorState::Play;
			bool isPause = m_EditorState == EditorState::Pause;

			// Botão Play
			if (isPlay)
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));

			if (ImGui::Button("▶ Play", ImVec2(50, 26)))
			{
				if (isEdit)        EnterPlay();
				else if (isPause) { m_GameCamera.MouseCaptured = true; m_EditorState = EditorState::Play; }
			}

			if (isPlay) ImGui::PopStyleColor();

			ImGui::SameLine();

			// Botão Pause
			if (isPause)
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.6f, 0.0f, 1.0f));

			if (ImGui::Button("⏸", ImVec2(26, 26)) && isPlay)
				EnterPause();

			if (isPause) ImGui::PopStyleColor();

			ImGui::SameLine();

			// Botão Stop
			if (ImGui::Button("⏹", ImVec2(26, 26)) && !isEdit)
				EnterEdit();

			ImGui::End();
		}


		void OnRender() override

		{

			m_ThumbnailRenderer.RenderPending();

			// Atualiza câmera antes de renderizar
			if (m_EditorState == EditorState::Play)
				m_ViewportRenderer->SetGameCamera(&m_GameCamera);
			else
				m_ViewportRenderer->SetGameCamera(nullptr);

			if (m_EditorUI)
			{
				ViewportWindow* viewport = m_EditorUI->GetViewport();
				if (viewport && viewport->IsInitialized())
				{
					auto framebuffer = viewport->GetFramebuffer();
					if (framebuffer && viewport->GetWidth() > 0 && viewport->GetHeight() > 0)
					{
						m_ViewportRenderer->RenderToFramebuffer(
							*framebuffer,
							viewport->GetWidth(),
							viewport->GetHeight(),
							EditorApp::Get().GetWindow().GetTime()
						);
					}
				}
			}

			if (m_EditorUI)
			{
				ViewportWindow* viewport = m_EditorUI->GetViewport();
				if (viewport)
				{
					// Gizmo só no modo Edit
					if (m_EditorState == EditorState::Edit || m_EditorState == EditorState::Pause)
					{
						m_ViewportRenderer->DrawGuizmo(
							viewport->GetBoundsMin(),
							viewport->GetBoundsMax());
					}
				}
			}
			if (m_EditorUI->m_MaterialEditorWindow.IsOpen())
				m_EditorUI->m_MaterialEditorWindow.RenderPreview();


			m_EditorUI->Draw();



			if (m_EditorState == EditorState::Edit || m_EditorState == EditorState::Pause)
			{
				ViewportWindow* viewport = m_EditorUI->GetViewport();
				if (viewport)
					m_ViewportRenderer->DrawGuizmo(
						viewport->GetBoundsMin(),
						viewport->GetBoundsMax());
			}

			// Input de câmera só no modo Edit
			if (m_EditorState == EditorState::Edit || m_EditorState == EditorState::Pause)
				HandleViewportCameraInput();



			HandleSceneInput();

			std::string title = "AXE Engine — " + std::to_string((int)m_FPS) + " FPS";
			EditorApp::Get().GetWindow().SetTitle(title);
		}

		void SaveScene()
		{
			if (!ProjectManager::Get().HasProject()) return;

			auto scenePath = ProjectManager::Get().GetCurrent().AssetsPath
				/ "Scenes" / "main.axescene";

			SceneSerializer::Serialize(*m_Scene, scenePath);

			// Define como cena padrão automaticamente
			auto& project = ProjectManager::Get().GetCurrent();
			project.StartScene = "Assets/Scenes/main.axescene";
			ProjectManager::Get().SaveProject();

			AXE_EDITOR_INFO("Cena salva e definida como padrão.");
		}
		void LoadScene()
		{
			if (!ProjectManager::Get().HasProject()) return;

			auto scenePath = ProjectManager::Get().GetCurrent().AssetsPath
				/ "Scenes" / "main.axescene";

			if (!std::filesystem::exists(scenePath))
			{
				AXE_CORE_WARN("EditorLayer: cena não encontrada em '{}'", scenePath.string());
				return;
			}

			// Limpa a cena atual
			m_Scene = std::make_unique<Scene>();
			m_Context.ActiveScene = m_Scene.get();
			m_Context.SelectedEntity = entt::null;

			// Atualiza ponteiros do renderer
			m_ViewportRenderer->SetScene(m_Scene.get());
			m_ViewportRenderer->SetSelectedEntity(&m_Context.SelectedEntity);

			// Carrega a cena
			SceneSerializer::Deserialize(scenePath, *m_Scene);
		}
		void HandleSceneInput()
		{
			ImGuiIO& io = ImGui::GetIO();

			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
			{
				// Se o material editor estiver focado, salva o grafo
				if (m_EditorUI->m_MaterialEditorWindow.IsOpen() &&
					m_EditorUI->m_MaterialEditorWindow.IsFocused())
					m_EditorUI->m_MaterialEditorWindow.SaveGraph();
				else
					SaveScene(); // caso contrário salva a cena
			}


			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
				LoadScene();


		}


		void OnEvent(Event& e) override
		{
			EventDispatcher dispatcher(e);

			dispatcher.Dispatch<FileDropEvent>([this](FileDropEvent& event)
				{
					m_EditorUI->GetAssetBrowser()->OnFileDrop(event.GetPath());
					return true;
				});

			// Escape no modo Play → pausa
			dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& event)
				{
					if (m_EditorState == EditorState::Play &&
						event.GetKeyCode() == GLFW_KEY_ESCAPE)
					{
						EnterPause();
						return true;
					}
					return false;
				});
		}


		void HandleViewportCameraInput()
		{
			if (!m_EditorUI) return;

			ViewportWindow* viewport = m_EditorUI->GetViewport();
			if (!viewport || !viewport->IsHovered()) return;

			ImGuiIO& io = ImGui::GetIO();

			// Teclas de perspectiva e gizmo
			if (ImGui::IsKeyPressed(ImGuiKey_P))
			{
				m_ViewportRenderer->m_Camera->isPerspective = !m_ViewportRenderer->m_Camera->isPerspective;
				if (!m_ViewportRenderer->m_Camera->isPerspective)
					m_ViewportRenderer->m_Camera->viewHeight = m_ViewportRenderer->m_Camera->viewWidth * io.DisplaySize.y / io.DisplaySize.x;
				AXE_EDITOR_INFO("Perspective mode: {}", m_ViewportRenderer->m_Camera->isPerspective);
			}

			if (viewport->IsFocused())
			{
				if (ImGui::IsKeyPressed(ImGuiKey_R)) m_ViewportRenderer->m_GuizmoOperation = ImGuizmo::ROTATE;
				if (ImGui::IsKeyPressed(ImGuiKey_S)) m_ViewportRenderer->m_GuizmoOperation = ImGuizmo::SCALE;
				if (ImGui::IsKeyPressed(ImGuiKey_T)) m_ViewportRenderer->m_GuizmoOperation = ImGuizmo::TRANSLATE;
			}

			// Picking — ANTES do bloco Alt
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
				!io.KeyAlt && !ImGuizmo::IsOver())
			{
				ImVec2 boundsMin = { viewport->GetBoundsMin().x, viewport->GetBoundsMin().y };
				ImVec2 mousePos = ImGui::GetMousePos();

				float localX = mousePos.x - boundsMin.x;
				float localY = mousePos.y - boundsMin.y;

				if (localX >= 0 && localY >= 0 &&
					localX < viewport->GetWidth() &&
					localY < viewport->GetHeight())
				{
					std::uint32_t pickID = m_ViewportRenderer->PickObject(localX, localY);

					if (pickID == 0)
						m_Context.ClearSelection();
					else
					{
						entt::entity picked = (entt::entity)pickID;
						if (m_Scene->GetRegistry().valid(picked))
							m_Context.Select(picked);
						else
							m_Context.ClearSelection();
					}
				}
			}

			// Câmera orbital — só com Alt
			const bool alt = io.KeyAlt;
			if (!alt) return;

			glm::vec2 delta = viewport->GetMouseDelta();
			delta *= 0.003f;

			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				m_ViewportRenderer->OnMouseRotate(delta);
			else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
				m_ViewportRenderer->OnMousePan(delta);
			else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
				m_ViewportRenderer->OnMouseZoom(delta.y * 10.0f);

			if (io.MouseWheel != 0.0f)
				m_ViewportRenderer->OnMouseZoom(io.MouseWheel);
		}

		void EnterPlay()
		{
			if (m_EditorState != EditorState::Edit) return;

			m_SceneSnapshot = SceneSerializer::SerializeToString(*m_Scene);

			auto& editorCam = m_ViewportRenderer->m_Camera;
			m_GameCamera.Reset(
				editorCam->GetPosition(),
				glm::degrees(editorCam->GetYaw()),
				glm::degrees(editorCam->GetPitch())
			);
			m_GameCamera.MouseCaptured = true;
			m_GameCamera.m_FirstMouse = true;

			m_Context.ClearSelection();
			m_EditorState = EditorState::Play;

			// Primeiro diz ao ImGui para não tocar no cursor
			ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

			// Depois captura via DLL correta
			EditorApp::Get().GetWindow().CaptureCursor(true);

			AXE_EDITOR_INFO("Modo Play iniciado.");
		}

		void EnterPause()
		{
			if (m_EditorState != EditorState::Play) return;

			m_GameCamera.MouseCaptured = false;
			m_EditorState = EditorState::Pause;

			// Garante que volta para EditorCamera
			m_ViewportRenderer->SetGameCamera(nullptr);  // ← está aqui?

			ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
			EditorApp::Get().GetWindow().CaptureCursor(false);

			AXE_EDITOR_INFO("Modo Pause — editor ativo.");
		}

		void EnterEdit()
		{
			if (m_EditorState == EditorState::Edit) return;

			GLFWwindow* window = (GLFWwindow*)EditorApp::Get().GetWindow().GetNativeWindow();
			if (window)
				glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

			// Restaura snapshot da cena
			if (!m_SceneSnapshot.empty())
			{
				m_Scene = std::make_unique<Scene>();
				m_Context.ActiveScene = m_Scene.get();
				m_Context.SelectedEntity = entt::null;
				m_ViewportRenderer->SetScene(m_Scene.get());
				m_ViewportRenderer->SetSelectedEntity(&m_Context.SelectedEntity);

				SceneSerializer::DeserializeFromString(m_SceneSnapshot, *m_Scene);
				m_SceneSnapshot.clear();
			}

			m_GameCamera.MouseCaptured = false;
			m_EditorState = EditorState::Edit;

			ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
			AXE_EDITOR_INFO("Modo Edit restaurado.");
		}






	private:
		std::unique_ptr<Scene> m_Scene;
		bool m_SceneLoaded = false;

		std::unique_ptr<axe::ViewportRenderer> m_ViewportRenderer;
		std::unique_ptr<EditorUI> m_EditorUI;

		EditorContext m_Context;


		float m_DeltaTime = 0.0f;
		float m_FPS = 0.0f;
		float m_FPSAccumulator = 0.0f;
		int m_FPSSamples = 0;

		bool m_EscWasPressed = false;

		SceneEnvironment m_Environment;

	};
}