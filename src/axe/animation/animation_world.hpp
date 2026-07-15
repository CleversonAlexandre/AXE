#pragma once
#include "axe/core/types.hpp"
#include "axe/animation/pose.hpp"

namespace axe
{
	class Scene;

	// Avança o tempo de todos os SkeletalMeshComponent da cena e recalcula
	// as palettes de bone.
	//
	// Mesmo contrato do PhysicsWorld e do ParticleWorld — chamado uma vez
	// por frame no EditorLayer::OnUpdate, ANTES do render. Ordem importa: o
	// SceneCollector lê a palette que este sistema acabou de escrever.
	//
	// Roda 100% na CPU e não toca em GPU: é só matemática (AnimationSampler).
	// Quem sobe pra GPU é o SkinningPass, dentro do SceneRenderer.
	class AXE_API AnimationWorld
	{
	public:
		// inPlay = false: o tempo NÃO avança, mas a palette continua sendo
		// calculada. É o que permite o editor mostrar o personagem na pose
		// certa (bind pose, ou o frame onde você parou o scrub) em vez de
		// uma mesh colapsada na origem.
		void OnUpdate(Scene& scene, float deltaTime, bool inPlay);

	private:
		// Buffer de pose reusado entre personagens.
		//
		// Um vector<BoneTransform> alocado por personagem, por frame, seria
		// morte por mil cortes — e o profiler apontaria pro malloc, não pra
		// animação. Como a avaliação é sequencial, um buffer só basta.
		Pose m_ScratchPose;
	};

} // namespace axe
