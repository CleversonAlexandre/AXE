#pragma once
#include "axe/asset/asset_database.hpp"
#include "axe/graphics/texture.hpp"
#include "../editor_icon_library.hpp"
#include <functional>
#include <string>
#include <vector>
#include <imgui.h>

namespace axe
{
	class AssetPicker
	{
	public:
		using OnSelectCallback = std::function<void(const AssetRecord&)>;

		// Desenha o slot completo
		// label     — label do campo (ex: "Material", "Albedo")
		// uuid      — UUID atual do asset selecionado (pode ser vazio)
		// filter    — tipos aceitos (ex: {AssetType::Material})
		// onSelect  — callback ao selecionar um asset

		static bool Draw(const char* label,
			std::string& uuid,
			const std::vector<AssetType>& filter,
			OnSelectCallback onSelect);

		
		static std::unordered_map<std::string, std::shared_ptr<Texture2D>> s_TextureCache;

	private:
		static void DrawSearchModel(const char* label,
			std::string& uuid,  
			const std::vector<AssetType>& filter,
			OnSelectCallback onSelect);

		static char s_SearchBuffer[256];
		static bool s_ModalOpen;
		static std::string s_ActiveLabel; //qual picker abriu o modal
		static bool s_ClearRequested;
	};
}//namespace axe