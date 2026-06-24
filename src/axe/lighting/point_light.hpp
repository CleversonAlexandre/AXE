#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <string>

namespace axe
{
	class Texture2D;

	struct AXE_API PointLight
	{
		glm::vec3 Position{ 0.0f, 1.0f, 0.0f };
		glm::vec3 Color{ 1.0f, 1.0f, 1.0f };

		float Intensity = 5.0f;
		float Radius = 10.0f; //Raio de influência (Atenuação fisica)

		// Animação de intensidade ao longo do tempo (flicker/pulsação) —
		// Intensity continua sendo o valor BASE editado no Inspector; o
		// valor realmente usado na hora de renderizar oscila em torno dele
		// usando seno: Intensity + sin(tempo * AnimSpeed) * AnimAmplitude.
		// Ver SceneCollector::Collect.
		bool  Animated = false;
		float AnimSpeed = 2.0f;
		float AnimAmplitude = 0.3f;

		// Spot Light — quando IsSpot=true, a luz só ilumina dentro de um
		// cone (em vez de todas as direções). InnerConeAngle é onde a luz
		// está em intensidade máxima; entre Inner e Outer ela esmaece
		// suavemente até zero — igual ao Spot Light da Unreal. Ambos os
		// ângulos são o MEIO-ângulo do cone, em graus.
		//
		// Direction NÃO é editado diretamente pelo usuário — é sempre
		// recalculado a partir da rotação do Transform do objeto (ver
		// ComputeSpotDirection abaixo). Rotacionar o objeto "aponta" o
		// cone, igual a girar uma lanterna de verdade. O campo aqui só
		// existe pra carregar o valor já calculado até o shader.
		bool      IsSpot = false;
		glm::vec3 Direction{ 0.0f, -1.0f, 0.0f };
		float     InnerConeAngle = 25.0f;
		float     OuterConeAngle = 35.0f;

		// Cookie — textura projetada através do cone (vitral colorido,
		// padrão de luz, etc.). Só tem efeito quando IsSpot=true. Limite de
		// 4 cookies de Point Light simultâneas na cena (ver lighting pass);
		// alem desse limite a luz funciona normal, só sem o padrão.
		std::shared_ptr<Texture2D> CookieTexture;
		std::string CookieTextureUUID;
	};

	// Direção "pra baixo" local (0,-1,0), rotacionada pelo Transform do
	// objeto — usa glm::quat (mesma conversão exata do Transform::GetMatrix
	// "canônico" do engine, sem gimbal lock), pra garantir que o cone do
	// Spot Light sempre bata com a orientação visual do objeto na cena.
	inline glm::vec3 ComputeSpotDirection(const glm::vec3& rotationEuler)
	{
		glm::quat q(rotationEuler);
		return glm::normalize(q * glm::vec3(0.0f, -1.0f, 0.0f));
	}
}//namespace axe