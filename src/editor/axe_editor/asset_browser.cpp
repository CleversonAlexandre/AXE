#include "asset_browser.hpp"
#include <imgui.h>

namespace axe
{
	void AssetBrowser::Draw()
	{
		if (!ImGui::Begin("Asset Browser"))
		{
			ImGui::End();
			return;
		}

		ImGui::Text("No assets in Asset Browser");
		ImGui::Separator();
		
		ImGui::BulletText("Entity 1");
		ImGui::BulletText("Entity 2");
		ImGui::BulletText("Entity 3");

		ImGui::End();
	}
}