#include "asset_picker.hpp"
#include "axe/log/log.hpp"
#include <imgui.h>

namespace axe
{
    char AssetPicker::s_SearchBuffer[256] = {};
    bool AssetPicker::s_ModalOpen = false;
    std::string AssetPicker::s_ActiveLabel;
    bool AssetPicker::s_ClearRequested = false;

    std::unordered_map<std::string, std::shared_ptr<Texture2D>> AssetPicker::s_TextureCache;

    bool AssetPicker::Draw(const char* label,
        std::string& uuid,
        const std::vector<AssetType>& filter,
        OnSelectCallback onSelect)
    {
        bool changed = false;
        auto& icons = EditorIconLibrary::Get();

        ImGui::PushID(label);

        // Asset atual
        std::string assetName = "Nenhum";
        const AssetRecord* current = uuid.empty()
            ? nullptr : AssetDatabase::Get().GetByUUID(uuid);
        if (current) assetName = current->Name;

        // Ícone baseado no tipo
        std::shared_ptr<Texture2D> icon;
        if (current)
        {
            switch (current->Type)
            {
            case AssetType::Material: icon = icons.GetMaterial(); break;
            case AssetType::Texture:  icon = icons.GetTexture();  break;
            case AssetType::Scene:    icon = icons.GetScene();    break;
            case AssetType::Script:   icon = icons.GetScript();   break;
            case AssetType::Audio:    icon = icons.GetAudio();    break;
            default:                  icon = icons.GetMesh();     break;
            }
        }
        else
        {
            // Sem asset — ícone baseado no filtro
            if (!filter.empty())
            {
                switch (filter[0])
                {
                case AssetType::Material: icon = icons.GetMaterial(); break;
                case AssetType::Texture:  icon = icons.GetTexture();  break;
                case AssetType::Scene:    icon = icons.GetScene();    break;
                default:                  icon = icons.GetMesh();     break;
                }
            }
        }


        std::shared_ptr<Texture2D> texturePreview;
        if (current && current->Type == AssetType::Texture)
        {
            // Usa a textura real como preview
            if (s_TextureCache.find(uuid) == s_TextureCache.end())
                s_TextureCache[uuid] = Texture2D::Create(current->FilePath.string());

            auto& cached = s_TextureCache[uuid];
            if (cached && cached->IsLoaded())
                icon = cached; // usa a textura real como ícone
        }


        ImVec2 thumbSize(32.0f, 32.0f);
        float  startY = ImGui::GetCursorPosY();

        // Thumbnail
        ImGui::InvisibleButton("##thumb", thumbSize);
        ImVec2 thumbMin = ImGui::GetItemRectMin();
        ImVec2 thumbMax = ImGui::GetItemRectMax();

        if (icon && icon->IsLoaded())
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(uintptr_t)icon->GetRendererID(),
                thumbMin, thumbMax, ImVec2(0, 1), ImVec2(1, 0));
        else
            ImGui::GetWindowDrawList()->AddRectFilled(
                thumbMin, thumbMax, IM_COL32(40, 40, 40, 255), 4.0f);

        // Drag and drop no thumbnail
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("ASSET_UUID"))
            {
                std::string dropped = (const char*)payload->Data;
                const AssetRecord* rec = AssetDatabase::Get().GetByUUID(dropped);
                if (rec)
                {
                    bool ok = filter.empty();
                    for (auto t : filter) if (rec->Type == t) { ok = true; break; }
                    if (ok) { uuid = dropped; if (onSelect) onSelect(*rec); changed = true; }
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();

        // Coluna direita
        float rightStart = ImGui::GetCursorPosX();
        float totalRight = ImGui::GetContentRegionAvail().x;
        float btnW = 24.0f;
        float nameW = totalRight - btnW * 3 - 12.0f;

        ImGui::BeginGroup();

        // Linha 1: label
        ImGui::TextUnformatted(label);

        // Linha 2: nome + botões na mesma linha
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::Button(assetName.c_str(), ImVec2(nameW, 0));
        ImGui::PopStyleColor();

        // Drag and drop no botão de nome
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("ASSET_UUID"))
            {
                std::string dropped = (const char*)payload->Data;
                const AssetRecord* rec = AssetDatabase::Get().GetByUUID(dropped);
                if (rec)
                {
                    bool ok = filter.empty();
                    for (auto t : filter) if (rec->Type == t) { ok = true; break; }
                    if (ok) { uuid = dropped; if (onSelect) onSelect(*rec); changed = true; }
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();

        // Botão seta (ícone redo como seta)
        if (icons.GetRedo() && icons.GetRedo()->IsLoaded())
        {
            if (ImGui::ImageButton("##arrow",
                (ImTextureID)(uintptr_t)icons.GetRedo()->GetRendererID(),
                ImVec2(14, 14), ImVec2(0, 1), ImVec2(1, 0)))
            {
                // TODO: highlight no Asset Browser
            }
        }

        ImGui::SameLine();

        // Botão pasta
        if (icons.GetFolder() && icons.GetFolder()->IsLoaded())
        {
            if (ImGui::ImageButton("##folder",
                (ImTextureID)(uintptr_t)icons.GetFolder()->GetRendererID(),
                ImVec2(14, 14), ImVec2(0, 1), ImVec2(1, 0)))
            {
                s_ModalOpen = true;
                s_ActiveLabel = label;
                std::memset(s_SearchBuffer, 0, sizeof(s_SearchBuffer));
            }
        }

        ImGui::EndGroup();

        // Modal de busca
        if (s_ModalOpen && s_ActiveLabel == label)
            DrawSearchModel(label, uuid, filter, onSelect);

        // Verifica se usuário selecionou <Nenhum>
        if (s_ClearRequested && s_ActiveLabel == label)
        {
            uuid = "";
            changed = true;
            s_ClearRequested = false;
        }

        ImGui::PopID();
        return changed;
    }

    void AssetPicker::DrawSearchModel(const char* label,
        std::string& uuid,
        const std::vector<AssetType>& filter,
        OnSelectCallback onSelect)
    {
        ImGui::OpenPopup("##AssetPickerModal");

        ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
        if (ImGui::BeginPopupModal("##AssetPickerModal", &s_ModalOpen,
            ImGuiWindowFlags_NoTitleBar))
        {
            ImGui::Text("Selecionar Asset");
            ImGui::Separator();

            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##search", s_SearchBuffer, sizeof(s_SearchBuffer));
            ImGui::Separator();

            auto& icons = EditorIconLibrary::Get();
            std::string search = s_SearchBuffer;
            auto& all = AssetDatabase::Get().GetAll();

            ImGui::BeginChild("##results", ImVec2(0, -36), false);

            // ← Opção Nenhum no topo
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            if (ImGui::Selectable("<Nenhum>"))
            {
                // Chama onSelect com record vazio — sinaliza limpeza
                s_ClearRequested = true;
                s_ModalOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
            ImGui::Separator();

            for (auto& [assetUUID, record] : all)
            {
                // Filtra por tipo
                if (!filter.empty())
                {
                    bool typeOk = false;
                    for (auto t : filter)
                        if (record.Type == t) { typeOk = true; break; }
                    if (!typeOk) continue;
                }

                // Filtra por nome
                if (!search.empty())
                {
                    std::string nameLower = record.Name;
                    std::string searchLower = search;
                    std::transform(nameLower.begin(), nameLower.end(),
                        nameLower.begin(), ::tolower);
                    std::transform(searchLower.begin(), searchLower.end(),
                        searchLower.begin(), ::tolower);
                    if (nameLower.find(searchLower) == std::string::npos) continue;
                }

                // Preview visual — textura real ou ícone
                ImVec2 previewSize(32.0f, 32.0f);
                std::shared_ptr<Texture2D> preview;

                if (record.Type == AssetType::Texture)
                {
                    if (s_TextureCache.find(assetUUID) == s_TextureCache.end())
                    {
                        AXE_CORE_INFO("AssetPicker: carregando textura {} - {}",
                            record.Name, record.FilePath.string());
                        s_TextureCache[assetUUID] = Texture2D::Create(record.FilePath.string());
                    }
                    preview = s_TextureCache[assetUUID];
                }
                else if (record.Type == AssetType::Material)
                {
                    // Usa ícone de material por enquanto
                    // (thumbnail do material viria do MaterialThumbnailRenderer)
                    preview = icons.GetMaterial();
                }
                else
                {
                    preview = icons.GetMesh();
                }

                // Desenha preview + nome como selectable
                ImGui::PushID(uuid.c_str());

                // Preview
                if (preview && preview->IsLoaded())
                    ImGui::Image(
                        (ImTextureID)(uintptr_t)preview->GetRendererID(),
                        previewSize, ImVec2(0, 1), ImVec2(1, 0));
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                    ImGui::Button("##nopreview", previewSize);
                    ImGui::PopStyleColor();
                }

                ImGui::SameLine();

                // Nome alinhado verticalmente ao centro do preview
                float textY = ImGui::GetCursorPosY() +
                    (previewSize.y - ImGui::GetTextLineHeight()) * 0.5f;
                ImGui::SetCursorPosY(textY);

                if (ImGui::Selectable(record.Name.c_str(), false,
                    ImGuiSelectableFlags_None,
                    ImVec2(0, previewSize.y)))
                {
                    uuid = assetUUID; // ← usa assetUUID em vez de uuid
                    if (onSelect) onSelect(record);
                    s_ModalOpen = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::PopID();
                ImGui::Separator();
            }

            ImGui::EndChild();

            ImGui::Separator();
            if (ImGui::Button("Cancelar", ImVec2(-1, 0)))
            {
                s_ModalOpen = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

}//namespace axe