#include "scene.hpp"
#include "axe/log/log.hpp"
namespace axe
{
	SceneObject& Scene::CreateObject(const std::string& name)
	{

		m_Objects.emplace_back();
		SceneObject& object = m_Objects.back();
		object.ID = m_NextObjectID++;
		object.Name = name;
		return object;
	}
	SceneObject* Scene::FindObjectByID(std::uint32_t id)
	{
		for (auto& object : m_Objects)
		{
			if (object.ID == id)
			{
				//AXE_CORE_INFO("Object {}", id);
				return &object;
			}
		}

		return nullptr;
	}

	const SceneObject* Scene::FindObjectByID(std::uint32_t id) const
	{
		for (const auto& object : m_Objects)
		{
			if (object.ID == id)
				return &object;
		}
		return nullptr;
	}
}