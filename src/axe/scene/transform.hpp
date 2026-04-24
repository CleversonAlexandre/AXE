#pragma once

#include "axe/utils/glm_config.hpp"

namespace axe
{
	struct Transform
	{
		glm::vec3 Position{ 0.0f, 0.0f, 0.0f }; 
		glm::vec3 Rotation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f, 1.0f, 1.0f };

		glm::mat4 WorldMatrix{ 1.0f };
		bool UseWorldMatrix = false;
		
		glm::mat4 GetMatrix() const
		{
			//glm::mat4 rotation = glm::mat4(1.0f);
			if (UseWorldMatrix)
				return WorldMatrix;

			glm::mat4 t = glm::translate(glm::mat4(1.0f), Position);
			glm::mat4 r = glm::mat4_cast(glm::quat(Rotation)); // quat evita gimbal lock
			glm::mat4 s = glm::scale(glm::mat4(1.0f), Scale);
			return t * r * s;

			//rotation = glm::rotate(rotation, Rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
			////rotation = glm::rotate(rotation, Rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
			//glm::mat4 r = glm::mat4_cast(glm::quat(Rotation));
			//rotation = glm::rotate(rotation, Rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));

			//glm::mat4 tranlation = glm::translate(glm::mat4(1.0f), Position);
			//glm::mat4 scale = glm::scale(glm::mat4(1.0f), Scale);

			//return tranlation * rotation * scale;
		}
	};
}