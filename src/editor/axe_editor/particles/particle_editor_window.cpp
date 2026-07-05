#include "editor/axe_editor/particles/particle_editor_window.hpp"
#include "axe/particles/particle_system_component.hpp"
#include "axe/scene/components.hpp"
#include "axe/asset/asset_database.hpp"
#include "axe/log/log.hpp"
#include "editor/axe_editor/material/material_compiler.hpp"
#include "editor/axe_editor/asset/asset_picker.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <unordered_map>

namespace axe
{
    // ── DragVec3Labeled ───────────────────────────────────────────────────────
    static bool DragVec3Labeled(const char* id, float* v, float speed,
        const char* fmt = "%.3f")
    {
        static const ImVec4 cols[3] = {
            { 0.95f, 0.35f, 0.35f, 1 }, // X — vermelho
            { 0.45f, 0.85f, 0.35f, 1 }, // Y — verde
            { 0.35f, 0.55f, 0.95f, 1 }, // Z — azul
        };
        static const char* labels[3] = { "X", "Y", "Z" };

        bool changed = false;
        float avail = ImGui::GetContentRegionAvail().x;
        float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        float fw = (avail - spacing * 2.f) / 3.f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImGui::PushID(id);
        for (int a = 0; a < 3; ++a)
        {
            ImGui::PushID(a);
            ImGui::SetNextItemWidth(fw);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                ImVec2(16.f, ImGui::GetStyle().FramePadding.y));
            if (ImGui::DragFloat("##v", &v[a], speed, 0.f, 0.f, fmt)) changed = true;
            ImGui::PopStyleVar();
            ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
            dl->AddText(ImVec2(r0.x + 6.f, (r0.y + r1.y) * .5f - ImGui::GetFontSize() * .5f),
                ImGui::ColorConvertFloat4ToU32(cols[a]), labels[a]);
            ImGui::PopID();
            if (a < 2) ImGui::SameLine(0, spacing);
        }
        ImGui::PopID();
        return changed;
    }

    // ── DrawSizeCurveWidget ───────────────────────────────────────────────────
    // Canvas interativo pra editar a curva Size over Lifetime.
    // - Clique esquerdo em espaço vazio = adiciona keyframe
    // - Arrasta ponto = move (X = tempo, Y = tamanho)
    // - Clique direito em ponto = remove (mínimo 2 pontos)
    static bool DrawSizeCurveWidget(const char* id,
        std::vector<SizeKey>& keys, float h = 90.f)
    {
        if (keys.empty()) return false;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float  w = ImGui::GetContentRegionAvail().x;
        ImVec2 canvasEnd(p.x + w, p.y + h);

        // Fundo
        dl->AddRectFilled(p, canvasEnd, IM_COL32(22, 22, 22, 255), 4.f);
        dl->AddRect(p, canvasEnd, IM_COL32(70, 70, 70, 255), 4.f);

        // Grid horizontal leve
        for (int g = 1; g < 4; ++g)
        {
            float gy = p.y + h * g / 4.f;
            dl->AddLine({ p.x, gy }, { canvasEnd.x, gy }, IM_COL32(45, 45, 45, 255));
        }
        // Grid vertical (0.25, 0.5, 0.75)
        for (int g = 1; g < 4; ++g)
        {
            float gx = p.x + w * g / 4.f;
            dl->AddLine({ gx, p.y }, { gx, canvasEnd.y }, IM_COL32(45, 45, 45, 255));
        }

        float maxVal = 0.05f;
        for (auto& k : keys) maxVal = std::max(maxVal, k.Size);
        maxVal *= 1.1f; // margem no topo

        // Avalia curva em t
        auto evalAt = [&](float t) -> float
            {
                if (keys.size() == 1) return keys[0].Size;
                for (int i = 0; i < (int)keys.size() - 1; ++i)
                {
                    if (t <= keys[i + 1].Time)
                    {
                        float span = keys[i + 1].Time - keys[i].Time;
                        float local = (span < 1e-5f) ? 0.f : (t - keys[i].Time) / span;
                        return glm::mix(keys[i].Size, keys[i + 1].Size, local);
                    }
                }
                return keys.back().Size;
            };

        // Converte t,v → pixel
        auto toScreen = [&](float t, float v) -> ImVec2
            {
                return ImVec2(p.x + t * w,
                    p.y + h - (v / maxVal) * h * 0.9f - h * 0.05f);
            };

        // Desenha a curva (80 samples)
        ImVec2 prev = toScreen(0.f, evalAt(0.f));
        for (int s = 1; s <= 80; ++s)
        {
            float t = s / 80.f;
            ImVec2 cur = toScreen(t, evalAt(t));
            dl->AddLine(prev, cur, IM_COL32(255, 210, 60, 220), 2.f);
            prev = cur;
        }

        // Interação
        ImGui::InvisibleButton(id, ImVec2(w, h));
        ImVec2 mouse = ImGui::GetMousePos();

        static std::unordered_map<ImGuiID, int> s_SizeDrag;
        ImGuiID wid = ImGui::GetID(id);
        int& dragging = s_SizeDrag[wid];

        bool modified = false;
        int toDelete = -1;

        // Detecta ponto mais próximo
        auto nearestPoint = [&](float radius) -> int
            {
                for (int i = 0; i < (int)keys.size(); ++i)
                {
                    ImVec2 sp = toScreen(keys[i].Time, keys[i].Size);
                    if (std::abs(mouse.x - sp.x) < radius && std::abs(mouse.y - sp.y) < radius)
                        return i;
                }
                return -1;
            };

        if (ImGui::IsItemHovered())
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                int hit = nearestPoint(9.f);
                if (hit >= 0) dragging = hit;
                else
                {
                    float t = std::clamp((mouse.x - p.x) / w, 0.f, 1.f);
                    keys.push_back({ t, evalAt(t) });
                    std::sort(keys.begin(), keys.end(),
                        [](const SizeKey& a, const SizeKey& b) { return a.Time < b.Time; });
                    modified = true;
                }
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                int hit = nearestPoint(9.f);
                if (hit >= 0 && keys.size() > 2) toDelete = hit;
            }
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging = -1;

        if (dragging >= 0 && dragging < (int)keys.size() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f))
        {
            keys[dragging].Time = std::clamp((mouse.x - p.x) / w, 0.f, 1.f);
            keys[dragging].Size = std::max(0.f, (1.f - (mouse.y - p.y) / h) * maxVal);
            std::sort(keys.begin(), keys.end(),
                [](const SizeKey& a, const SizeKey& b) { return a.Time < b.Time; });
            modified = true;
        }

        if (toDelete >= 0) { keys.erase(keys.begin() + toDelete); modified = true; }

        // Desenha pontos de controle
        for (int i = 0; i < (int)keys.size(); ++i)
        {
            ImVec2 sp = toScreen(keys[i].Time, keys[i].Size);
            bool isDrag = (dragging == i);
            dl->AddCircleFilled(sp, isDrag ? 6.f : 5.f,
                isDrag ? IM_COL32(255, 255, 80, 255) : IM_COL32(255, 210, 60, 255));
            dl->AddCircle(sp, isDrag ? 6.f : 5.f, IM_COL32(0, 0, 0, 200), 12, 1.5f);
        }

        ImGui::TextDisabled("Click = add  |  Drag = move  |  Right-click = remove");
        return modified;
    }

    // ── DrawColorCurveWidget ──────────────────────────────────────────────────
    // Gradiente visual + pontos de controle arrastáveis (só eixo X = tempo).
    // Ponto selecionado mostra color picker inline.
    static bool DrawColorCurveWidget(const char* id,
        std::vector<ColorKey>& keys, float h = 50.f)
    {
        if (keys.empty()) return false;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float  w = ImGui::GetContentRegionAvail().x;
        ImVec2 canvasEnd(p.x + w, p.y + h);

        // Gradiente interpolado pixel a pixel (amostras a cada 4px)
        auto evalColor = [&](float t) -> glm::vec4
            {
                if (keys.size() == 1) return keys[0].Color;
                for (int i = 0; i < (int)keys.size() - 1; ++i)
                {
                    if (t <= keys[i + 1].Time)
                    {
                        float span = keys[i + 1].Time - keys[i].Time;
                        float local = (span < 1e-5f) ? 0.f : (t - keys[i].Time) / span;
                        return glm::mix(keys[i].Color, keys[i + 1].Color, local);
                    }
                }
                return keys.back().Color;
            };

        // Fundo xadrez pra indicar alpha
        dl->AddRectFilled(p, canvasEnd, IM_COL32(80, 80, 80, 255), 4.f);

        // Gradiente em faixas de 4px
        int steps = (int)(w / 4.f) + 1;
        for (int s = 0; s < steps; ++s)
        {
            float t0 = (float)s / steps;
            float t1 = (float)(s + 1) / steps;
            glm::vec4 c = evalColor(glm::mix(t0, t1, 0.5f));
            ImU32 col = IM_COL32(
                (int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255), (int)(c.a * 255));
            dl->AddRectFilled(
                ImVec2(p.x + t0 * w, p.y),
                ImVec2(p.x + t1 * w + 1.f, p.y + h), col);
        }

        dl->AddRect(p, canvasEnd, IM_COL32(70, 70, 70, 255), 4.f);

        ImGui::InvisibleButton(id, ImVec2(w, h));
        ImVec2 mouse = ImGui::GetMousePos();

        static std::unordered_map<ImGuiID, int> s_ColSel;
        static std::unordered_map<ImGuiID, int> s_ColDrag;
        ImGuiID wid = ImGui::GetID(id);
        int& selected = s_ColSel[wid];
        int& dragging = s_ColDrag[wid];

        bool modified = false;
        int toDelete = -1;

        auto nearestPoint = [&](float radius) -> int
            {
                for (int i = 0; i < (int)keys.size(); ++i)
                {
                    float px = p.x + keys[i].Time * w;
                    if (std::abs(mouse.x - px) < radius) return i;
                }
                return -1;
            };

        if (ImGui::IsItemHovered())
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                int hit = nearestPoint(10.f);
                if (hit >= 0) { selected = hit; dragging = hit; }
                else
                {
                    float t = std::clamp((mouse.x - p.x) / w, 0.f, 1.f);
                    glm::vec4 c = evalColor(t);
                    keys.push_back({ t, c });
                    std::sort(keys.begin(), keys.end(),
                        [](const ColorKey& a, const ColorKey& b) { return a.Time < b.Time; });
                    modified = true;
                }
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                int hit = nearestPoint(10.f);
                if (hit >= 0 && keys.size() > 2) toDelete = hit;
            }
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging = -1;

        if (dragging >= 0 && dragging < (int)keys.size() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f))
        {
            keys[dragging].Time = std::clamp((mouse.x - p.x) / w, 0.f, 1.f);
            std::sort(keys.begin(), keys.end(),
                [](const ColorKey& a, const ColorKey& b) { return a.Time < b.Time; });
            modified = true;
        }

        if (toDelete >= 0) { keys.erase(keys.begin() + toDelete); modified = true; if (selected >= toDelete) selected = std::max(0, selected - 1); }

        // Pontos de controle (triângulos no topo do gradiente)
        for (int i = 0; i < (int)keys.size(); ++i)
        {
            float px = p.x + keys[i].Time * w;
            bool  sel = (selected == i);
            ImU32 col = IM_COL32(
                (int)(keys[i].Color.r * 255),
                (int)(keys[i].Color.g * 255),
                (int)(keys[i].Color.b * 255), 255);
            // Triangulo apontando pra baixo (marcador)
            float ts = sel ? 7.f : 5.f;
            dl->AddTriangleFilled(
                { px - ts, p.y - 1.f }, { px + ts, p.y - 1.f }, { px, p.y + ts * 1.4f }, col);
            dl->AddTriangle(
                { px - ts, p.y - 1.f }, { px + ts, p.y - 1.f }, { px, p.y + ts * 1.4f },
                sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 200), 1.5f);
        }

        // Color picker pra ponto selecionado
        if (selected >= 0 && selected < (int)keys.size())
        {
            ImGui::Spacing();
            std::string label = "Key " + std::to_string(selected) +
                "  (t=" + std::to_string(int(keys[selected].Time * 100)) + "%)##cpick";
            if (ImGui::ColorEdit4(label.c_str(), &keys[selected].Color.x,
                ImGuiColorEditFlags_AlphaBar))
                modified = true;
        }

        ImGui::TextDisabled("Click = add  |  Drag = move  |  Right-click = remove");
        return modified;
    }

    // ─────────────────────────────────────────────────────────────────────────

    void ParticleEditorWindow::Initialize() { InitializePreview(); }

    void ParticleEditorWindow::InitializePreview()
    {
        FramebufferSpecification spec;
        spec.Width = 512; spec.Height = 512;
        spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH32F };
        m_PreviewFramebuffer = Framebuffer::Create(spec);

        m_PreviewRenderer = std::make_unique<ViewportRenderer>();
        m_PreviewRenderer->Initialize();
        m_PreviewRenderer->SetPickingEnabled(false);
        m_PreviewRenderer->SetPreviewMode(true);
        if (m_PreviewRenderer->GetSceneRenderer())
        {
            m_PreviewRenderer->GetSceneRenderer()->SetDeferredEnabled(false);
            m_PreviewRenderer->GetSceneRenderer()->SetDeferredSupported(false);
        }

        m_PreviewScene = std::make_unique<Scene>();
        m_PreviewEntity = m_PreviewScene->CreateEntity("PreviewEmitter");

        auto& reg = m_PreviewScene->GetRegistry();
        auto le = m_PreviewScene->CreateEntity("PreviewLight");
        auto& lc = reg.emplace<LightComponent>(le);
        lc.Data = std::make_shared<DirectionalLight>();
        lc.Data->Direction = glm::vec3(0.f, -1.f, -1.f);
        lc.Data->Color = glm::vec3(1.f);
        lc.Data->Intensity = 1.5f;
        lc.Data->AmbientStrength = 0.4f;

        m_PreviewRenderer->SetScene(m_PreviewScene.get());
    }

    void ParticleEditorWindow::SyncPreviewComponent()
    {
        if (!m_PreviewScene || !m_Asset) return;
        auto& reg = m_PreviewScene->GetRegistry();
        if (!reg.valid(m_PreviewEntity)) return;
        auto& ps = reg.get_or_emplace<ParticleSystemComponent>(m_PreviewEntity);
        ps.Data = m_Asset;
        ps.ParticleAssetUUID.clear();
        ps.Playing = true;
        ps.EmitterRuntimes.resize(m_Asset->Emitters.size());
        for (auto& rt : ps.EmitterRuntimes)
        {
            rt.Particles.clear();
            rt.EmissionAccumulator = 0.0f;
            rt.SpawnAngle = 0.0f;
        }
    }

    void ParticleEditorWindow::OpenAsset(std::shared_ptr<ParticleSystemAsset> asset)
    {
        if (!asset) return;
        m_Asset = asset;
        m_Open = true;
        m_SelectedEmitter = 0;
        if (!m_PreviewScene) InitializePreview();

        // Recompila os shaders de material de partícula de cada emitter.
        // O shader é cache runtime (não serializado) — ao abrir o asset no
        // editor o ponteiro está null. O SceneSerializer faz isso via callback
        // ao carregar cena; aqui precisamos fazer manualmente.
        for (auto& emitter : m_Asset->Emitters)
        {
            if (emitter.ParticleMaterialUUID.empty()) continue;
            const AssetRecord* record = AssetDatabase::Get().GetByUUID(emitter.ParticleMaterialUUID);
            if (!record) continue;
            MaterialCompiler::CompileParticleFunctionFromFile(
                record->FilePath,
                emitter.ParticleMaterialShader,
                emitter.ParticleMaterialSamplers);
        }

        SyncPreviewComponent();
    }

    void ParticleEditorWindow::Restart()
    {
        if (!m_PreviewScene) return;
        auto& reg = m_PreviewScene->GetRegistry();
        if (!reg.valid(m_PreviewEntity)) return;
        if (auto* ps = reg.try_get<ParticleSystemComponent>(m_PreviewEntity))
            for (auto& rt : ps->EmitterRuntimes)
            {
                for (auto& p : rt.Particles) p.Alive = false;
                rt.EmissionAccumulator = 0.f;
                rt.SpawnAngle = 0.f;
            }
    }

    void ParticleEditorWindow::UpdatePreview(float dt)
    {
        if (!m_Open || !m_PreviewScene) return;
        m_PreviewParticleWorld.OnUpdate(*m_PreviewScene, dt);
    }

    void ParticleEditorWindow::RenderPreview()
    {
        if (!m_PreviewRenderer || !m_PreviewFramebuffer || !m_PreviewScene) return;
        if (auto* sr = m_PreviewRenderer->GetSceneRenderer())
        {
            sr->SetDeferredEnabled(false);
            sr->SetDeferredSupported(false);
        }
        uint32_t w = (uint32_t)m_PreviewSize.x, h = (uint32_t)m_PreviewSize.y;
        if (w == 0 || h == 0) { w = 512; h = 512; }
        m_PreviewRenderer->SetScene(m_PreviewScene.get());
        m_PreviewRenderer->RenderToFramebuffer(*m_PreviewFramebuffer, w, h, 0.f);
    }

    void ParticleEditorWindow::Save()
    {
        if (!m_Asset) return;
        if (!m_Asset->GetFilePath().empty())
            m_Asset->Save(m_Asset->GetFilePath());

        if (m_Context && m_Context->ActiveScene)
        {
            const AssetRecord* record = AssetDatabase::Get().GetByPath(m_Asset->GetFilePath());
            if (record)
            {
                auto& reg = m_Context->ActiveScene->GetRegistry();
                int count = 0;
                for (auto entity : reg.view<ParticleSystemComponent>())
                {
                    auto& ps = reg.get<ParticleSystemComponent>(entity);
                    if (ps.ParticleAssetUUID != record->UUID) continue;
                    auto copy = std::make_shared<ParticleSystemAsset>(*m_Asset);
                    ps.Data = copy;
                    ps.EmitterRuntimes.resize(copy->Emitters.size());
                    ++count;
                }
                if (count > 0)
                    AXE_CORE_INFO("ParticleEditorWindow: aplicado em {} entidade(s).", count);
            }
        }
    }

    void ParticleEditorWindow::Draw()
    {
        if (!m_Open) return;
        m_IsAnyWindowFocused = false;

        ImGui::SetNextWindowSize(ImVec2(1200, 720), ImGuiCond_FirstUseEver);
        std::string title = "Particle Editor";
        if (m_Asset) title += " — " + m_Asset->GetName();
        title += "###ParticleEditorWindow";

        if (!ImGui::Begin(title.c_str(), &m_Open))
        {
            ImGui::End();
            return;
        }

        // Toolbar
        if (ImGui::Button("Save"))    Save();
        ImGui::SameLine();
        if (ImGui::Button("Restart")) Restart();
        ImGui::Separator();

        ImVec2 avail = ImGui::GetContentRegionAvail();

        // ── DockSpace — mesmo padrão do MaterialEditorWindow ─────────────
        ImGuiID dsID = ImGui::GetID("ParticleEditorDockSpace");
        ImGui::DockSpace(dsID, ImVec2(0.f, 0.f), ImGuiDockNodeFlags_None);

        static bool s_LayoutDone = false;
        if (!s_LayoutDone)
        {
            s_LayoutDone = true;
            const char* flagFile = "particle_editor_layout_v1.flag";
            if (!std::filesystem::exists(flagFile))
            {
                std::ofstream(flagFile) << "v1";

                ImGui::DockBuilderRemoveNode(dsID);
                ImGui::DockBuilderAddNode(dsID, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dsID,
                    (avail.x > 0 && avail.y > 0) ? avail : ImVec2(1200, 680));

                // [Emitter List | Params | Preview]
                ImGuiID dockList, dockRight;
                ImGui::DockBuilderSplitNode(dsID, ImGuiDir_Left, 0.18f, &dockList, &dockRight);

                ImGuiID dockParams, dockPreview;
                ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Left, 0.32f, &dockParams, &dockPreview);

                ImGui::DockBuilderDockWindow("Emitters##PS", dockList);
                ImGui::DockBuilderDockWindow("Details##PS", dockParams);
                ImGui::DockBuilderDockWindow("Preview##PS", dockPreview);
                ImGui::DockBuilderFinish(dsID);
            }
        }

        // ── Sub-janelas docáveis ──────────────────────────────────────────
        DrawEmitterListWindow();
        DrawEmitterDetailsWindow();
        DrawPreviewWindow();

        m_IsAnyWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        ImGui::End();

        if (!m_Open)
        {
            if (m_PreviewScene && m_PreviewScene->GetRegistry().valid(m_PreviewEntity))
                if (auto* ps = m_PreviewScene->GetRegistry().try_get<ParticleSystemComponent>(m_PreviewEntity))
                    ps->Data = nullptr;
            m_Asset = nullptr;
        }
    }

    void ParticleEditorWindow::DrawEmitterListWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        if (ImGui::Begin("Emitters##PS"))
        {
            if (!m_Asset) { ImGui::End(); ImGui::PopStyleVar(); return; }

            ImGui::TextDisabled("Emitters");
            ImGui::Separator();

            for (int i = 0; i < (int)m_Asset->Emitters.size(); ++i)
            {
                auto& e = m_Asset->Emitters[i];
                ImGui::PushID(i);
                if (ImGui::Checkbox("##en", &e.Enabled))
                    SyncPreviewComponent();
                ImGui::SameLine();
                bool sel = (m_SelectedEmitter == i);
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.55f, 0.85f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.6f, 0.9f, 0.7f));
                if (ImGui::Selectable(e.Name.c_str(), sel))
                    m_SelectedEmitter = i;
                ImGui::PopStyleColor(2);
                ImGui::PopID();
            }

            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button("+ Add", ImVec2(-1, 0)))
            {
                m_Asset->Emitters.emplace_back();
                m_Asset->Emitters.back().Name = "Emitter " + std::to_string(m_Asset->Emitters.size());
                m_SelectedEmitter = (int)m_Asset->Emitters.size() - 1;
                SyncPreviewComponent();
            }
            if (m_Asset->Emitters.size() > 1 &&
                ImGui::Button("Remove", ImVec2(-1, 0)))
            {
                m_Asset->Emitters.erase(m_Asset->Emitters.begin() + m_SelectedEmitter);
                m_SelectedEmitter = std::max(0, m_SelectedEmitter - 1);
                SyncPreviewComponent();
            }

            ImGui::Spacing();
            if (m_PreviewScene)
            {
                auto& reg = m_PreviewScene->GetRegistry();
                if (reg.valid(m_PreviewEntity))
                    if (auto* ps = reg.try_get<ParticleSystemComponent>(m_PreviewEntity))
                    {
                        int alive = 0, total = 0;
                        for (auto& rt : ps->EmitterRuntimes)
                        {
                            for (auto& p : rt.Particles) if (p.Alive) ++alive;
                            total += (int)rt.Particles.size();
                        }
                        ImGui::TextDisabled("Alive: %d/%d", alive, total);
                    }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void ParticleEditorWindow::DrawEmitterDetailsWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        if (ImGui::Begin("Details##PS"))
        {
            if (!m_Asset || m_Asset->Emitters.empty())
            {
                ImGui::TextDisabled("No emitters.");
                ImGui::End(); ImGui::PopStyleVar(); return;
            }
            if (m_SelectedEmitter >= (int)m_Asset->Emitters.size())
                m_SelectedEmitter = 0;

            ParticleEmitterDef& e = m_Asset->Emitters[m_SelectedEmitter];

            // ── Cabeçalho do emitter ──────────────────────────────────────
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s", e.Name.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##ename", buf, sizeof(buf))) e.Name = buf;

            ImGui::Spacing();

            // Helper: estilo de header colorido
            auto pushHeaderStyle = [](ImVec4 col)
                {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(col.x * .7f, col.y * .7f, col.z * .7f, .5f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(col.x * .8f, col.y * .8f, col.z * .8f, .7f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(col.x, col.y, col.z, .9f));
                };
            auto popHeaderStyle = []() { ImGui::PopStyleColor(3); };

            // ── 1. EMISSION ───────────────────────────────────────────────
            pushHeaderStyle({ 0.35f, 0.65f, 0.95f, 1 });
            bool openEmission = ImGui::CollapsingHeader("  Emission", ImGuiTreeNodeFlags_DefaultOpen);
            popHeaderStyle();
            if (openEmission)
            {
                ImGui::Indent(8);
                ImGui::Checkbox("Enabled", &e.Enabled);
                ImGui::DragFloat("Rate", &e.EmissionRate, 1.f, 0.f, 5000.f);
                ImGui::DragInt("Max Particles", &e.MaxParticles, 10.f, 1, 100000);
                ImGui::DragFloat("Lifetime", &e.Lifetime, 0.05f, 0.05f, 60.f);
                ImGui::DragFloat("Life Var", &e.LifetimeVariation, 0.01f, 0.f, 1.f);
                ImGui::DragFloat("Duration (s)", &e.Duration, 0.1f, -1.0f, 9999.0f);
                ImGui::SameLine(); ImGui::TextDisabled(e.Duration < 0.f ? "(inf)" : "");
                ImGui::DragFloat("Warmup (s)", &e.WarmupTime, 0.1f, 0.0f, 30.0f);
                ImGui::Checkbox("Auto Destroy", &e.AutoDestroy);
                ImGui::Unindent(8);
            }

            // ── 2. SPAWN SHAPE ────────────────────────────────────────────
            pushHeaderStyle({ 0.55f, 0.85f, 0.45f, 1 });
            bool openShape = ImGui::CollapsingHeader("  Spawn Shape", ImGuiTreeNodeFlags_DefaultOpen);
            popHeaderStyle();
            if (openShape)
            {
                ImGui::Indent(8);
                const char* shapes[] = { "Point","Sphere","Ring","Cylinder","Helix","Cone" };
                ImGui::Combo("Shape", &e.SpawnShape, shapes, 6);

                ImGui::Text("Origin Offset");
                DragVec3Labeled("##off", &e.SpawnOffset.x, 0.1f);

                if (e.SpawnShape != 0)
                {
                    if (e.SpawnShape == 5)
                    {
                        ImGui::DragFloat("Cone Angle", &e.SpawnConeAngle, 0.5f, 1.f, 89.f);
                        ImGui::TextDisabled("Half-angle in degrees.");
                    }
                    else
                        ImGui::DragFloat("Radius", &e.SpawnRadius, 0.05f, 0.f, 100.f);
                    if (e.SpawnShape == 1) ImGui::Checkbox("Surface Only", &e.SpawnOnSurface);
                    if (e.SpawnShape == 3 || e.SpawnShape == 4)
                        ImGui::DragFloat("Height", &e.SpawnHeight, 0.1f, 0.01f, 100.f);
                    if (e.SpawnShape == 4)
                    {
                        ImGui::DragFloat("Helix Turns", &e.SpawnHelixTurns, 0.1f, 0.5f, 20.f);
                        ImGui::TextDisabled("Velocity flows along axis.");
                    }
                    else if (e.SpawnShape != 5)
                        ImGui::Checkbox("Velocity Follows Shape", &e.VelocityFollowsShape);
                }
                ImGui::Unindent(8);
            }

            // ── 3. MOVEMENT ───────────────────────────────────────────────
            pushHeaderStyle({ 0.95f, 0.75f, 0.35f, 1 });
            bool openMovement = ImGui::CollapsingHeader("  Movement", ImGuiTreeNodeFlags_DefaultOpen);
            popHeaderStyle();
            if (openMovement)
            {
                ImGui::Indent(8);
                ImGui::Text("Start Velocity"); ImGui::SameLine(); ImGui::TextDisabled("(m/s)");
                DragVec3Labeled("##sv", &e.StartVelocity.x, 0.1f);
                ImGui::Text("Velocity Variation");
                DragVec3Labeled("##vv", &e.VelocityVariation.x, 0.1f);
                ImGui::Text("Gravity");
                DragVec3Labeled("##grav", &e.Gravity.x, 0.1f);
                ImGui::DragFloat("Velocity Limit", &e.VelocityLimit, 0.1f, 0.f, 200.f);
                ImGui::SameLine(); ImGui::TextDisabled(e.VelocityLimit <= 0.f ? "(off)" : "");
                ImGui::Unindent(8);
            }

            // ── 4. APPEARANCE ─────────────────────────────────────────────
            pushHeaderStyle({ 0.95f, 0.45f, 0.75f, 1 });
            bool openAppear = ImGui::CollapsingHeader("  Appearance", ImGuiTreeNodeFlags_DefaultOpen);
            popHeaderStyle();
            if (openAppear)
            {
                ImGui::Indent(8);

                // Color
                ImGui::Text("Color over Lifetime");
                if (e.ColorCurve.empty())
                {
                    ImGui::ColorEdit4("Start##cs", &e.ColorStart.x);
                    ImGui::ColorEdit4("End##ce", &e.ColorEnd.x);
                    if (ImGui::SmallButton("+ Curve##col"))
                    {
                        e.ColorCurve.push_back({ 0.f,e.ColorStart }); e.ColorCurve.push_back({ 1.f,e.ColorEnd });
                    }
                }
                else
                {
                    DrawColorCurveWidget("##colcurve", e.ColorCurve);
                    if (ImGui::SmallButton("Clear##col")) e.ColorCurve.clear();
                }

                ImGui::Spacing();

                // Size
                ImGui::Text("Size over Lifetime");
                if (e.SizeCurve.empty())
                {
                    ImGui::DragFloat("Size Start", &e.SizeStart, 0.01f, 0.f, 100.f);
                    ImGui::DragFloat("Size End", &e.SizeEnd, 0.01f, 0.f, 100.f);
                    if (ImGui::SmallButton("+ Curve##sz"))
                    {
                        e.SizeCurve.push_back({ 0.f,e.SizeStart }); e.SizeCurve.push_back({ 1.f,e.SizeEnd });
                    }
                }
                else
                {
                    DrawSizeCurveWidget("##szcurve", e.SizeCurve);
                    if (ImGui::SmallButton("Clear##sz")) e.SizeCurve.clear();
                }

                ImGui::Spacing();
                ImGui::DragFloat("Size Variation", &e.SizeVariation, 0.01f, 0.f, 1.f);
                ImGui::DragFloat("Stretch Amount", &e.StretchAmount, 0.05f, 0.f, 20.f);
                ImGui::Checkbox("Ribbon / Trail", &e.IsRibbon);
                if (e.IsRibbon)
                    ImGui::TextDisabled("  Particles connected as continuous strip.");

                ImGui::Spacing();
                ImGui::Checkbox("Beam / Lightning", &e.IsBeam);
                if (e.IsBeam)
                {
                    ImGui::TextDisabled("  Procedural ribbon from origin to target.");
                    ImGui::Text("Target Offset");
                    DragVec3Labeled("##beamtgt", &e.BeamTargetOffset.x, 0.1f);
                    ImGui::DragInt("Points##beam", &e.BeamPoints, 1.f, 4, 128);
                    ImGui::DragFloat("Deviation##beam", &e.BeamDeviation, 0.01f, 0.f, 5.f);
                    ImGui::DragFloat("Flicker Speed##beam", &e.BeamFlickerSpeed, 0.5f, 0.f, 50.f);
                    ImGui::DragFloat("Width##beam", &e.BeamWidth, 0.002f, 0.001f, 2.f);
                    ImGui::TextDisabled("Color Start/End control color along the beam.");
                }
                const char* blends[] = { "Alpha","Additive" };
                ImGui::Combo("Blend Mode", &e.BlendMode, blends, 2);
                ImGui::Unindent(8);
            }

            // ── Particle Material ─────────────────────────────────────────
            ImGui::SeparatorText("  Material");
            pushHeaderStyle({ 0.75f, 0.55f, 0.95f, 1 });
            bool openMat = ImGui::CollapsingHeader("  Material", ImGuiTreeNodeFlags_DefaultOpen);
            popHeaderStyle();
            if (openMat)
            {
                ImGui::Indent(8);
                DrawMaterialSlot(e);
                ImGui::Spacing();
                ImGui::Checkbox("Flipbook##fb", &e.FlipbookEnabled);
                if (e.FlipbookEnabled)
                {
                    ImGui::DragInt("Columns", &e.FlipbookCols, 1.f, 1, 64);
                    ImGui::DragInt("Rows", &e.FlipbookRows, 1.f, 1, 64);
                    ImGui::DragFloat("Cycles/Life", &e.FlipbookCycles, 0.1f, 0.1f, 20.f);
                    ImGui::TextDisabled("Frames: %d", e.FlipbookCols * e.FlipbookRows);
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("Sub-emitter (spawns when particle dies)");
                AssetPicker::Draw("SubEmitter##sub", e.SubEmitterUUID,
                    { AssetType::ParticleSystem }, nullptr);
                if (!e.SubEmitterUUID.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X##subclr")) e.SubEmitterUUID.clear();
                    ImGui::TextDisabled("  One .axepart per dead particle.");
                }

                ImGui::Unindent(8);
            }

            // ── 5b. PARTICLE LIGHT ────────────────────────────────────────
            pushHeaderStyle({ 0.95f, 0.85f, 0.25f, 1 }); // amarelo-ouro
            bool openLight = ImGui::CollapsingHeader("  Particle Light");
            popHeaderStyle();
            if (openLight)
            {
                ImGui::Indent(8);
                ImGui::Checkbox("Enabled##light", &e.LightEnabled);
                if (e.LightEnabled)
                {
                    ImGui::ColorEdit3("Color##lc", &e.LightColor.x);
                    ImGui::DragFloat("Intensity##li", &e.LightIntensity, 0.1f, 0.f, 50.f);
                    ImGui::DragFloat("Radius##lr", &e.LightRadius, 0.2f, 0.f, 100.f);
                    ImGui::Checkbox("Scale by Particle Count", &e.LightScaleByParticles);
                    if (e.LightScaleByParticles)
                        ImGui::TextDisabled("  Intensity ∝ alive/max particles.");
                    ImGui::Spacing();
                    ImGui::Checkbox("Flicker", &e.LightFlicker);
                    if (e.LightFlicker)
                    {
                        ImGui::DragFloat("Flicker Speed##lfs", &e.LightFlickerSpeed, 0.5f, 0.1f, 50.f);
                        ImGui::DragFloat("Flicker Amount##lfa", &e.LightFlickerAmount, 0.01f, 0.f, 1.f);
                        ImGui::TextDisabled("  0=none  1=full off");
                    }
                }
                ImGui::Unindent(8);
            }

            // ── 6. FORCES (colapsado por padrão) ─────────────────────────
            pushHeaderStyle({ 0.95f, 0.55f, 0.35f, 1 });
            bool openForces = ImGui::CollapsingHeader("  Forces");
            popHeaderStyle();
            if (openForces)
            {
                ImGui::Indent(8);

                ImGui::Text("Rotation");
                ImGui::DragFloat("Rot Min", &e.RotationMin, 0.05f, -6.28f, 6.28f);
                ImGui::DragFloat("Rot Max", &e.RotationMax, 0.05f, -6.28f, 6.28f);
                ImGui::DragFloat("Speed Min", &e.RotationSpeedMin, 0.05f, -20.f, 20.f);
                ImGui::DragFloat("Speed Max", &e.RotationSpeedMax, 0.05f, -20.f, 20.f);

                ImGui::Spacing();
                ImGui::Text("Orbit");
                ImGui::DragFloat("Strength##orb", &e.OrbitStrength, 0.05f, -20.f, 20.f);
                if (e.OrbitStrength != 0.f)
                {
                    ImGui::Text("Axis"); DragVec3Labeled("##orbitaxis", &e.OrbitAxis.x, 0.05f);
                }

                ImGui::Spacing();
                ImGui::Text("Turbulence");
                ImGui::DragFloat("Strength##turb", &e.TurbulenceStrength, 0.05f, 0.f, 50.f);
                if (e.TurbulenceStrength > 0.f)
                {
                    ImGui::DragFloat("Frequency##turb", &e.TurbulenceFrequency, 0.05f, 0.01f, 20.f);
                    ImGui::DragFloat("Speed##turb", &e.TurbulenceSpeed, 0.05f, 0.f, 20.f);
                }

                ImGui::Spacing();
                ImGui::Checkbox("Local Space", &e.LocalSpace);
                ImGui::Unindent(8);
            }

            // ── 7. COLLISION (colapsado por padrão) ───────────────────────
            pushHeaderStyle({ 0.45f, 0.85f, 0.75f, 1 });
            bool openCol = ImGui::CollapsingHeader("  Collision");
            popHeaderStyle();
            if (openCol)
            {
                ImGui::Indent(8);
                ImGui::Checkbox("Enabled##col", &e.CollisionEnabled);
                if (e.CollisionEnabled)
                {
                    ImGui::DragFloat("Plane Y", &e.CollisionY, 0.1f, -999.f, 999.f);
                    ImGui::DragFloat("Bounciness", &e.CollisionBounciness, 0.01f, 0.f, 1.f);
                    ImGui::DragFloat("Friction", &e.CollisionFriction, 0.01f, 0.f, 1.f);
                }
                ImGui::Unindent(8);
            }

            // ── 8. BURST (colapsado por padrão) ──────────────────────────
            pushHeaderStyle({ 0.75f, 0.75f, 0.45f, 1 });
            bool openBurst = ImGui::CollapsingHeader("  Bursts");
            popHeaderStyle();
            if (openBurst)
            {
                ImGui::Indent(8);
                if (ImGui::BeginTable("BurstsTable", 5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
                {
                    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 52.f);
                    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 52.f);
                    ImGui::TableSetupColumn("Cycles", ImGuiTableColumnFlags_WidthFixed, 56.f);
                    ImGui::TableSetupColumn("Interval", ImGuiTableColumnFlags_WidthFixed, 60.f);
                    ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 24.f);
                    ImGui::TableHeadersRow();

                    int toDelete = -1;
                    for (int bi = 0; bi < (int)e.Bursts.size(); ++bi)
                    {
                        auto& b = e.Bursts[bi];
                        ImGui::PushID(bi);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::SetNextItemWidth(50.f);
                        ImGui::DragFloat("##t", &b.Time, 0.05f, 0.f, 999.f, "%.2f");
                        ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(50.f);
                        ImGui::DragInt("##c", &b.Count, 1.f, 1, 10000);
                        ImGui::TableSetColumnIndex(2); ImGui::SetNextItemWidth(54.f);
                        if (b.Cycles == -1) { ImGui::Text("  inf"); if (ImGui::IsItemClicked()) b.Cycles = 1; }
                        else { ImGui::DragInt("##cy", &b.Cycles, 1.f, -1, 999); if (b.Cycles == 0) b.Cycles = -1; }
                        ImGui::TableSetColumnIndex(3); ImGui::SetNextItemWidth(58.f);
                        ImGui::DragFloat("##iv", &b.Interval, 0.05f, 0.001f, 999.f, "%.2f");
                        ImGui::TableSetColumnIndex(4);
                        if (ImGui::SmallButton("X")) toDelete = bi;
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                    if (toDelete >= 0) e.Bursts.erase(e.Bursts.begin() + toDelete);
                }
                if (ImGui::Button("+ Add Burst")) e.Bursts.emplace_back();
                ImGui::Unindent(8);
            }

            // ── 9. LOD (colapsado por padrão) ─────────────────────────────
            pushHeaderStyle({ 0.55f, 0.55f, 0.55f, 1 });
            bool openLOD = ImGui::CollapsingHeader("  LOD");
            popHeaderStyle();
            if (openLOD)
            {
                ImGui::Indent(8);
                ImGui::Checkbox("LOD Enabled", &e.LODEnabled);
                if (e.LODEnabled)
                {
                    ImGui::DragFloat("Full Rate Dist", &e.LODDistanceFull, 1.f, 0.f, 500.f);
                    ImGui::DragFloat("Zero Rate Dist", &e.LODDistanceZero, 1.f, 0.f, 500.f);
                    ImGui::TextDisabled("Rate: 100%%→0%% between distances.");
                }
                ImGui::Unindent(8);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
    void ParticleEditorWindow::DrawPreviewWindow()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("Preview##PS"))
        {
            ImVec2 sz = ImGui::GetContentRegionAvail();
            uint32_t w = (uint32_t)sz.x, h = (uint32_t)sz.y;
            if (w > 0 && h > 0 &&
                (std::abs((float)w - m_PreviewSize.x) > 1.f ||
                    std::abs((float)h - m_PreviewSize.y) > 1.f))
            {
                m_PreviewSize = { (float)w, (float)h };
                m_PreviewFramebuffer->Resize(w, h);
                if (m_PreviewRenderer && m_PreviewRenderer->m_Camera)
                    m_PreviewRenderer->m_Camera->SetAspectRatio((float)w / (float)h);
            }
            m_PreviewHovered = ImGui::IsWindowHovered();
            m_PreviewFocused = ImGui::IsWindowFocused();
            if (m_PreviewFocused) m_IsAnyWindowFocused = true;

            ImTextureID tex = (ImTextureID)(uintptr_t)m_PreviewFramebuffer->GetColorAttachmentRendererID();
            if (tex != (ImTextureID)0)
                ImGui::Image(tex, sz, ImVec2(0, 1), ImVec2(1, 0));
            HandlePreviewInput();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void ParticleEditorWindow::DrawMaterialSlot(ParticleEmitterDef& emitter)
    {
        ImGui::TextDisabled("Drag a .axemat (domain: Particle) here.");
        if (AssetPicker::Draw("Material##part_mat", emitter.ParticleMaterialUUID,
            { AssetType::Material },
            [&](const AssetRecord& record)
            {
                if (MaterialCompiler::CompileParticleFunctionFromFile(
                    record.FilePath, emitter.ParticleMaterialShader, emitter.ParticleMaterialSamplers))
                {
                    emitter.ParticleMaterialUUID = record.UUID;
                    AXE_EDITOR_INFO("Particle material compiled: {}", record.Name);
                }
            }))
        {
            if (emitter.ParticleMaterialUUID.empty())
            {
                emitter.ParticleMaterialShader = nullptr;
                emitter.ParticleMaterialSamplers.clear();
            }
        }
        ImGui::TextDisabled(emitter.ParticleMaterialShader
            ? "  Custom shader active."
            : "  No material — using default radial falloff.");
    }

    void ParticleEditorWindow::HandlePreviewInput()
    {
        if (!m_PreviewHovered) return;
        ImGuiIO& io = ImGui::GetIO();
        static ImVec2 last = ImGui::GetMousePos();
        ImVec2 cur = ImGui::GetMousePos();
        glm::vec2 delta(cur.x - last.x, cur.y - last.y);
        last = cur;
        delta *= 0.003f;
        if (!io.KeyAlt) return;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_PreviewRenderer->OnMouseRotate(delta);
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            m_PreviewRenderer->OnMousePan(delta);
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            m_PreviewRenderer->OnMouseZoom(delta.y * 10.f);
        if (io.MouseWheel != 0.f)
            m_PreviewRenderer->OnMouseZoom(io.MouseWheel);
    }

} // namespace axe