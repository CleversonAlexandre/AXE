#pragma once
#include "axe/core/types.hpp"

#include "axe/scene/components.hpp"
#include "axe/physics/physics_components.hpp"
#include "axe/script/script_component.hpp"
#include "axe/particles/particle_system_component.hpp"

#include <entt/entt.hpp>

namespace axe
{
	class Scene;

	// Lista de tipos usada pelo clone. Não é um container — só um pacote de
	// tipos que o compilador expande.
	template <typename...>
	struct ComponentGroup {};

	// ═════════════════════════════════════════════════════════════════════
	//  A LISTA. Componente novo? Adicione o nome aqui. Só aqui.
	//
	//  E se você esquecer: o SceneSnapshot detecta e GRITA no console na
	//  primeira vez que você der Play, dizendo o nome do componente que
	//  ficou de fora. Nunca mais perder trabalho em silêncio.
	// ═════════════════════════════════════════════════════════════════════
	using AllComponents = ComponentGroup <
		NameComponent,
		TransformComponent,
		RelationshipComponent,
		FolderComponent,

		MeshComponent,
		SkeletalMeshComponent,
		MaterialComponent,

		LightComponent,
		PointLightComponent,
		PostProcessComponent,
		InteriorVolumeComponent,
		ReflectionProbeComponent,
		ProbeVolumeComponent,
		EnvironmentComponent,

		CameraComponent,
		SpringArmComponent,

		RigidbodyComponent,
		ColliderComponent,
		CharacterControllerComponent,
		TriggerComponent,

		ScriptComponent,
		ParticleSystemComponent
	> ;

	// Snapshot de Play/Stop.
	//
	// ── POR QUE NÃO É SERIALIZAÇÃO ───────────────────────────────────────
	//
	// Antes, o snapshot era `SceneSerializer::SerializeToString()` — a cena
	// ia pra JSON e voltava. O problema não é performance: é que o snapshot
	// passava a HERDAR TODO BURACO DO SERIALIZER. Um campo que o serializer
	// não conhecesse morria no round-trip — e o dado perdido não era o do
	// Play, era o da EDIÇÃO, feito antes.
	//
	// Isso criava o imposto: "implementou algo novo? atualize o serializer,
	// ou o Play/Stop come o seu trabalho."
	//
	// Mas as duas coisas querem coisas diferentes:
	//
	//   SceneSerializer  → gravar em DISCO. Precisa de formato, versão,
	//                      portabilidade entre máquinas e entre builds.
	//
	//   SceneSnapshot    → "me devolva exatamente o que eu tinha há 5
	//                      segundos". Mesmo processo, mesma memória, mesmo
	//                      binário. Não precisa de formato NENHUM.
	//
	// Então isto aqui é um CLONE DE MEMÓRIA do entt::registry. Consequências:
	//
	//   - shared_ptr é copiado como shared_ptr. O ProbeGrid, a
	//     ReflectionCapture, o SkinnedMesh, o AnimGraph — tudo atravessa
	//     intacto, sem hack de preservação e sem rebake de 20 segundos.
	//   - Estado que não tem representação em JSON (o tempo de um clipe, a
	//     pose de um personagem, o runtime de um emissor) atravessa também.
	//   - Os IDs das entidades são PRESERVADOS, então RelationshipComponent
	//     e a seleção do editor continuam válidos.
	//
	// O custo continua sendo uma lista de tipos — C++ não tem reflexão. Mas
	// é UMA LINHA por componente, contra `save` + `load` campo a campo.
	class AXE_API SceneSnapshot
	{
	public:
		// Copia a cena inteira. Chamado no Play.
		//
		// Também verifica a cobertura: se o registry contiver algum
		// componente que NÃO está em AllComponents, loga um erro com o nome
		// dele. É a rede de segurança que substitui "lembrar de atualizar o
		// serializer".
		//
		// Recebe Scene& (nao const) DE PROPOSITO. No EnTT, a sobrecarga const
		// de registry.storage<T>() devolve um PONTEIRO anulavel, enquanto a
		// nao-const devolve uma REFERENCIA. Iterar o ponteiro num range-based
		// for nao compila ("no callable 'begin' function ... *").
		//
		// Pegar o registry nao-const evita a sobrecarga const inteira — e e a
		// mesma API que o SceneSerializer ja usa e que sabemos que compila.
		void Capture(Scene& scene);

		// Devolve a cena ao estado do Capture. Chamado no Stop.
		//
		// Note que a Scene NÃO é recriada — só o registry dela é reposto.
		// Ponteiros para a Scene (viewport, contexto do editor) continuam
		// válidos, e não é preciso reconectar nada.
		void Restore(Scene& scene);

		bool IsEmpty() const { return m_Empty; }
		void Clear();

	private:
		entt::registry m_Registry;
		bool m_Empty = true;
	};

} // namespace axe
