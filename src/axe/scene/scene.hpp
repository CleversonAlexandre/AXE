#pragma once
#include "axe/core/types.hpp"
#include <vector>
#include "scene_objects.hpp"

namespace axe
{
	class AXE_API Scene
	{
	public:
		SceneObject& CreateObject(const std::string& name = "SceneObject");		

		
		const std::vector<SceneObject>& GetObjects() const { return m_Objects; }
		SceneObject* FindObjectByID(std::uint32_t id);
		const SceneObject* FindObjectByID(std::uint32_t id) const;

		std::vector<SceneObject>& GetObjects() { return m_Objects; }
		std::vector<SceneObject>& GetObject() { return m_Objects; }

		
	private:
		std::vector<SceneObject> m_Objects;
		std::uint32_t m_NextObjectID = 1;
	};
}