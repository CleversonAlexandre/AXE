#pragma once
#include "axe/utils/glm_config.hpp"
#include "axe/scene/transform.hpp"
#include "axe/scene/spline.hpp"
#include "axe/mesh/mesh.hpp"
#include "axe/animation/skinned_mesh.hpp"
#include "axe/animation/skeleton.hpp"
#include "axe/animation/animation_clip.hpp"
#include "axe/animation/animation_player.hpp"
#include "axe/animation/blend_space_1d.hpp"
#include "axe/animation/anim_graph.hpp"
#include "axe/animation/anim_graph_instance.hpp"
#include "axe/animation/skeletal_mesh_asset.hpp"
#include "axe/animation/anim_graph_asset.hpp"
#include "axe/material/material.hpp"
#include "axe/lighting/directional_light.hpp"
#include "axe/lighting/point_light.hpp"
#include "axe/lighting/interior_volume.hpp"
#include "axe/lighting/probe_volume.hpp"
#include "axe/lighting/reflection_probe.hpp"
#include "axe/physics/physics_components.hpp"
#include <memory>
#include <string>
#include <entt/entt.hpp>

#include "axe/graphics/renderer/post_process_pass.hpp"
#include "axe/graphics/renderer/ssao_pass.hpp"

namespace axe
{
	// Nome do objeto
	struct NameComponent
	{
		std::string Name = "Entity";
	};

	// Transform
	struct TransformComponent
	{
		Transform Data;
	};

	// Mesh
	struct MeshComponent
	{
		std::shared_ptr<Mesh> Data;
		std::string           AssetUUID;
	};

	// Skeletal Mesh — personagem animado.
	//
	// Convive com MaterialComponent normalmente: graças ao Skin Cache, o
	// material de um personagem é um material COMUM (o mesmo node graph que
	// você usa numa parede). Nenhuma variante skinned é necessária.
	//
	// Uma entidade tem MeshComponent OU SkeletalMeshComponent — nunca os
	// dois. O SceneCollector prioriza o skeletal se ambos existirem.
	struct SkeletalMeshComponent
	{
		// O ASSET e a fonte de verdade. Data/Clips abaixo sao so o conteudo
		// resolvido dele, copiado pra ca por conveniencia do runtime.
		//
		// Dez inimigos arrastados pra cena apontam pro MESMO
		// SkeletalMeshAsset — e portanto compartilham UMA malha na GPU.
		std::shared_ptr<SkeletalMeshAsset> Asset;

		std::shared_ptr<SkinnedMesh> Data;

		// UUID do .axeskel. E a UNICA coisa que a cena serializa — ao
		// recarregar, o asset e reencontrado no AssetDatabase e resolvido.
		std::string                  AssetUUID;

		// Clipes disponíveis. Vêm do próprio arquivo do personagem e/ou de
		// SkeletalMeshLoader::LoadClips() em arquivos separados
		// (idle.fbx, run.fbx...), religados por nome de bone.
		std::vector<std::shared_ptr<AnimationClip>> Clips;

		// ── Seleção da animação ──────────────────────────────────────────
		//
		// Três formas, em ordem de PRIORIDADE (a primeira setada vence):
		//
		//   Graph        — a state machine completa. É o modo "de produção":
		//                  o gameplay só escreve parâmetros (Speed,
		//                  IsGrounded, Attack) e o grafo decide tudo.
		//
		// As duas abaixo continuam existindo e são úteis: um NPC de fundo,
		// um prop animado ou um teste rápido não precisam de grafo nenhum.
		//
		//   CurrentClip  — um clipe por vez, com crossfade automático na
		//                  troca. Bom pra ações discretas (atacar, pular).
		//
		//   BlendSpace   — um contínuo dirigido por um parâmetro. Bom pra
		//                  locomoção: em vez de trocar de idle pra walk pra
		//                  run, você só varia a velocidade e a pose
		//                  acompanha, sem "pop" nenhum.

		// Asset .axeanim. E o que a cena serializa (so o UUID) e o que o
		// editor de nos abre. Se != nullptr, tem prioridade sobre BlendSpace
		// e CurrentClip: quem manda passa a ser o grafo.
		//
		// NAO ha mais um `shared_ptr<AnimGraph>` aqui. Havia, e era um erro: o
		// grafo do asset e um MOLDE. Quem guarda a copia viva deste personagem
		// e o GraphInstance — senao dois personagens com o mesmo .axeanim
		// compartilhariam o tempo da animacao e o estado da maquina.
		std::shared_ptr<AnimGraphAsset> GraphAsset;
		std::string                     GraphAssetUUID;

		// Runtime do grafo — a COPIA dos nos, o blackboard, e o pool de poses.
		// O Script Editor escreve em GraphInstance.Params.
		AnimGraphInstance GraphInstance;

		// Índice em Clips. -1 = nenhum -> bind pose (T-pose).
		int   CurrentClip = -1;

		// Duração do crossfade quando CurrentClip muda.
		float BlendTime = 0.2f;

		// Se != nullptr, ignora CurrentClip e usa o blend space.
		std::shared_ptr<BlendSpace1D> BlendSpace;

		// Posição no eixo do blend space (tipicamente a velocidade do pawn,
		// escrita pelo Script Editor a cada frame).
		float BlendParam = 0.0f;
		float BlendSpaceTime = 0.0f;

		// Runtime do crossfade/camadas. É ele que faz o trabalho de verdade.
		AnimationPlayer Player;

		// Última CurrentClip que o AnimationWorld realmente aplicou. Serve
		// pra detectar a MUDANÇA (e disparar o crossfade) em vez de
		// reiniciar o clipe todo frame.
		int _AppliedClip = -2;   // -2 = nunca aplicado (distinto de "nenhum")

		// Preenchida pelo AnimationWorld a cada frame, consumida pelo
		// SceneCollector. Vive no componente (e não no renderer) porque é
		// dado de SIMULAÇÃO, não de render — gameplay pode querer ler a
		// pose (IK, sockets, hitbox por bone).
		std::vector<glm::mat4> BonePalette;

		// Anima no EDITOR, sem precisar apertar Play.
		//
		// E como o Unreal e o Unity se comportam, e por um bom motivo: a
		// primeira coisa que voce faz ao importar um personagem e conferir se
		// a animacao esta certa. Obrigar a entrar em Play so pra isso torna o
		// ciclo de iteracao lento e o preview inutil.
		//
		// O botao Tocar/Pausar do Inspector controla Player.Playing; este flag
		// controla se o tempo corre fora do Play.
		bool PreviewInEditor = true;

		// Quem manda na pose desta entidade NESTE frame.
		//
		// Normalmente e o AnimationWorld: ele avalia o AnimGraph (ou o blend
		// space, ou o clipe) e reescreve o BonePalette INTEIRO, todo frame.
		//
		// Uma ferramenta de AUTORIA — o Sequencer, e amanha um pose editor —
		// precisa ser dona da pose enquanto edita. Sem este flag, o que ela
		// escreve e apagado microssegundos depois, no mesmo frame, e o sintoma
		// e o pior possivel: a UI mostra a key no lugar certo e o personagem
		// nao se mexe.
		//
		// Com o flag em true o AnimationWorld PULA a entidade por completo, e
		// quem o ligou fica responsavel por escrever o BonePalette (via
		// Pose + AnimationSampler::BuildSkinningMatrices) e por desliga-lo
		// quando terminar.
		//
		// `PreviewInEditor` NAO serve para isso: ele so decide se o TEMPO
		// avanca. Com ele em false a pose congela — e continua sendo
		// recalculada e regravada pelo grafo a cada frame.
		bool PoseOverride = false;

		// ── Debug ────────────────────────────────────────────────────────
		//
		// Desenha o esqueleto como linhas por cima da malha.
		//
		// Esta é A ferramenta de diagnóstico do sistema inteiro. Se o
		// personagem aparecer errado:
		//
		//   ossos CERTOS + malha explodida  -> o bug esta no compute shader
		//                                      ou nos pesos (skinning)
		//   ossos JA TORTOS                 -> o bug esta no loader ou no
		//                                      sampler (hierarquia/matrizes)
		//
		// Corta o espaco de busca pela metade em dois segundos.
		bool ShowSkeleton = false;

		// Matrizes GLOBAIS dos ossos (model-space), preenchidas pelo
		// AnimationWorld. Sao diferentes da BonePalette: a palette ja tem a
		// InverseBindPose aplicada e nao serve pra saber ONDE o osso esta.
		std::vector<glm::mat4> BoneGlobals;

		// SC43 — alguem esta ANEXADO a um socket deste personagem?
		//
		// As globals custam uma passada extra pela hierarquia e por muito
		// tempo so o desenho do esqueleto (ShowSkeleton) as pedia. Agora um
		// anexo tambem precisa delas, e ele nao tem como ligar ShowSkeleton
		// (isso acenderia o wireframe dos ossos no jogo).
		//
		// Recontado do zero a cada frame pelo AnimationWorld, antes do laco
		// principal: um contador persistente ficaria devendo o decremento no
		// dia em que a arma fosse destruida fora do caminho previsto, e o
		// personagem pagaria as globals para sempre.
		bool _WantsBoneGlobals = false;

		const Skeleton* GetSkeleton() const
		{
			return (Data && Data->GetSkeleton()) ? Data->GetSkeleton().get() : nullptr;
		}
	};

	// Material
	struct MaterialComponent
	{
		std::shared_ptr<Material> Data;
		std::string MaterialAssetUUID;
	};

	// Luz direcional
	struct LightComponent
	{
		std::shared_ptr<DirectionalLight> Data;
	};

	// ── SKY_LIGHT_V1 ────────────────────────────────────────────────────
	// A luz de ambiente (o ceu iluminando de todos os lados). Irmao do
	// LightComponent, mesmo padrao de shared_ptr. Ver axe/lighting/
	// directional_light.hpp para o porque da separacao.
	struct SkyLightComponent
	{
		std::shared_ptr<SkyLight> Data;
	};

	// Point Light
	struct PointLightComponent
	{
		std::shared_ptr<PointLight> Data;
	};

	// ═══════════════════════════════════════════════════════════════════════
	//  S0a — a cor era ImVec4, e era o ULTIMO imgui no nucleo da cena.
	//
	//  Depois de tirar o ScriptGraph do ScriptComponent, esta unica linha
	//  ainda obrigava components.hpp — e portanto TODA entidade da engine — a
	//  incluir <imgui.h>. Uma cor de pasta na hierarquia do editor nao e
	//  motivo para o runtime conhecer a biblioteca de interface.
	//
	//  glm::vec4 tem os mesmos .x/.y/.z/.w, entao os tres pontos do editor que
	//  a consomem continuam identicos; so a origem do tipo mudou. O editor
	//  converte para ImVec4 no ponto de desenho, que e onde a conversao
	//  pertence.
	// ═══════════════════════════════════════════════════════════════════════
	struct FolderComponent
	{
		glm::vec4 Color = glm::vec4(1.0f, 0.8f, 0.2f, 1.0f);
	};

	struct RelationshipComponent
	{
		entt::entity Parent = entt::null;
		std::vector<entt::entity> Children;
	};

	// ═══════════════════════════════════════════════════════════════════════
	//  SC43 — ANEXO A SOCKET
	//
	//  A arma na mao, a mochila nas costas, o efeito no pe. A entidade segue
	//  um SOCKET de outra entidade animada, e nao o transform dela.
	//
	//  ── POR QUE UM COMPONENTE, E NAO UM CAMPO NO RELATIONSHIP ─────────────
	//
	//  RelationshipComponent responde "quem e meu pai" — hierarquia, que vale
	//  para toda entidade e e o que a Hierarchy Window desenha e o que o
	//  destroy em cascata segue. Anexo a osso e OUTRA pergunta: "de onde vem
	//  o meu transform". A esmagadora maioria das entidades parenteadas nao
	//  quer isso, e enfiar SocketName no Relationship faria toda entidade da
	//  cena carregar uma string vazia.
	//
	//  Os dois convivem: o anexo normalmente TAMBEM e filho do personagem
	//  (para aparecer aninhado na hierarquia e morrer junto). Quem manda no
	//  transform e este componente; o Relationship segue sendo so parentesco.
	//
	//  ── COMO O TRANSFORM CHEGA AQUI ──────────────────────────────────────
	//
	//  O AnimationWorld — unico lugar do frame onde a pose e sabidamente
	//  fresca — calcula a matriz do socket em espaco de mundo e a deposita em
	//  _SocketWorld. O Scene::GetWorldTransform, que ja e o unico ponto por
	//  onde todo mundo pergunta "onde isto esta", usa essa matriz no lugar da
	//  cadeia de pais.
	//
	//  A interface entre os dois lados e uma mat4 pura: a animacao nao
	//  aprende a compor cena, e a cena nao aprende a amostrar pose.
	//
	//  ── MODO DE FALHA ────────────────────────────────────────────────────
	//
	//  _Valid falso (socket apagado, osso renomeado no reimport, personagem
	//  sem pose ainda) faz o GetWorldTransform cair na cadeia de pais normal:
	//  a arma aparece na ORIGEM do personagem, visivelmente errada mas
	//  visivel. Sumir seria pior — "nao renderizou" e "esta no lugar errado"
	//  tem causas diferentes e a primeira nao se diagnostica de olho.
	// ═══════════════════════════════════════════════════════════════════════
	struct SocketAttachmentComponent
	{
		// Entidade com SkeletalMeshComponent. entt::null = anexo inerte.
		entt::entity Target = entt::null;

		// Nome do socket no .axeskel do Target (SkeletalMeshAsset::Socket).
		// Vazio = segue o OSSO cru, se BoneName estiver preenchido.
		std::string SocketName;

		// ── Preenchidos pelo AnimationWorld, por frame ────────────────────
		glm::mat4 _SocketWorld{ 1.0f };
		bool      _Valid = false;

		// Cache do indice do osso. A resolucao por nome varre a lista de
		// bones; refazer isso por frame, por anexo, e um custo que nao compra
		// nada — o esqueleto nao muda entre frames. Invalidado quando o nome
		// do socket muda.
		int         _BoneIndex = -1;
		std::string _ResolvedFor;
	};

	// ═══════════════════════════════════════════════════════════════════════
	//  EditorTransientComponent — entidade que so existe enquanto uma
	//  ferramenta de autoria esta aberta.
	//
	//  Tag. Quem a carrega:
	//
	//    - NAO e serializada pelo SceneSerializer (nem no save da cena, nem
	//      no snapshot de Play).
	//    - NAO aparece na Hierarchy Window.
	//
	//  ── POR QUE ISTO PRECISOU EXISTIR ────────────────────────────────────
	//
	//  O Sequencer precisa mostrar a arma presa ao socket enquanto voce
	//  anima. O caminho certo ja existia inteiro — MeshComponent +
	//  SocketAttachmentComponent + Scene::GetWorldTransform — e desenhar um
	//  preview por fora, direto no renderer, seria reimplementar os tres.
	//
	//  Faltava so uma coisa: essas entidades nao podem vazar para o arquivo
	//  da cena. Sem a tag, fechar o Sequencer e salvar deixaria uma pistola
	//  fantasma anexada ao personagem, e o usuario nao teria de onde deduzir
	//  que ela veio de uma janela que nem esta mais aberta.
	//
	//  Generica de proposito: qualquer preview de autoria futuro (silhueta de
	//  IK, proxy de camera, ghost de drag) usa esta tag em vez de inventar o
	//  proprio mecanismo de exclusao — que e como se chega a tres.
	// ═══════════════════════════════════════════════════════════════════════
	struct EditorTransientComponent
	{
		// Quem criou. So diagnostico: aparece no log se alguma ferramenta
		// esquecer de limpar as suas.
		std::string Owner;
	};

	struct PostProcessComponent
	{
		PostProcessSettings Settings;
		SSAOSettings        SSAO;

		bool IsGlobal = true;

		// POSTPROCESS_DOMAIN_V1 — o material de efeito vive dentro de
		// `Settings` (UserMaterialUUID / UserBlendPoint / UserIntensity), e nao
		// num campo novo aqui.
		//
		// Nao e detalhe de organizacao: `Settings` e a struct que o
		// viewport_renderer (editor) e o world_renderer (jogo) ja copiam
		// INTEIRA para o renderer. Um campo solto neste componente exigiria
		// encanamento novo nos dois caminhos — e o do jogo e justamente o que
		// ninguem exercita todo dia.
	};

	// Interior Volume — caixa que bloqueia sol + ambient/IBL em ambientes
	// fechados. O tamanho da caixa vem da ESCALA do Transform da entity.
	// Ver comentário completo em axe/lighting/interior_volume.hpp.
	struct InteriorVolumeComponent
	{
		InteriorVolume Data;
	};

	// Reflection Probe — cubemap local pré-filtrado pro especular.
	// Ver comentário completo em axe/lighting/reflection_probe.hpp.
	struct ReflectionProbeComponent
	{
		ReflectionProbeSettings Settings;

		// Resultado da captura — runtime only, nunca serializado (barato
		// de recapturar: o load da cena rebakeia via BakeRequested).
		std::shared_ptr<ReflectionCapture> Capture;

		bool BakeRequested = false;
	};

	// Probe Volume (Light Probes / GI-lite) — grid de irradiância SH L1
	// bakeada. Ver comentário completo em axe/lighting/probe_volume.hpp.
	struct ProbeVolumeComponent
	{
		ProbeVolumeSettings Settings;

		// Resultado do bake — runtime only, NUNCA serializado (o load da
		// cena dispara um rebake automático via BakeRequested).
		std::shared_ptr<ProbeGrid> Grid;

		// Setado pelo Inspector (botão "Bake") ou pelo load da cena;
		// consumido (e resetado) pelo SceneCollector, que enfileira um
		// ProbeBakeRequest na RenderQueue — o editor nunca fala com o
		// renderer diretamente.
		bool BakeRequested = false;
	};

	// Câmera de jogo
	// ── Spring Arm ───────────────────────────────────────────────────────────
	// Define a posição da câmera em relação à entidade (braço de câmera).
	// Usado pelo GameCamera em modo ThirdPerson.
	struct SpringArmComponent
	{
		float Length = 5.0f;    // distância atrás do pawn
		float HeightOffset = 2.0f; // altura acima do pawn
		glm::vec3 SocketOffset = { 0, 0, 0 }; // offset lateral/depth fino
		float LagSpeed = 8.0f;   // suavização do follow (lerp)
		bool  EnableCameraLag = true;
		bool  MouseRotates = true;  // mouse orbita a câmera
	};

	struct CameraComponent
	{
		float Fov = 60.0f;
		float NearClip = 0.1f;
		float FarClip = 1000.0f;
		float MoveSpeed = 5.0f;
		float Sensitivity = 0.1f;
		bool  IsPrimary = true;
	};

	// ── CAMINHO NO MUNDO ─────────────────────────────────────────────────────
	//
	// Uma curva por onde algo se desloca: o trilho de uma camera de cutscene, a
	// rota de um elevador, a patrulha de um inimigo.
	//
	// ── POR QUE E UMA ENTIDADE DA CENA, E NAO PARTE DA SEQUENCE ──────────────
	//
	// Guardar os pontos dentro do `.axeseq` seria auto-contido e pior em tres
	// frentes: a curva ficaria invisivel no outliner, nao daria para reusar o
	// mesmo trilho em duas cutscenes, e nada fora do Sequencer poderia
	// aproveita-la — quando a IA precisasse de uma rota de patrulha, o motor
	// ganharia um SEGUNDO conceito de caminho.
	//
	// ── OS PONTOS SAO LOCAIS ─────────────────────────────────────────────────
	//
	// Relativos ao transform da entidade. Assim mover, girar ou escalar a
	// entidade leva o caminho INTEIRO junto — reposicionar um trilho e arrastar
	// um objeto, e nao reeditar oito pontos.
	struct SplineComponent
	{
		std::vector<glm::vec3> Points;

		// Fecha o ultimo ponto no primeiro. Sem emenda visivel: numa curva
		// fechada o vizinho de cada ponta e o outro lado.
		bool Closed = false;

		// Desenhar no viewport mesmo sem estar selecionada.
		bool AlwaysVisible = false;

		// ── CACHE (nao serializa) ────────────────────────────────────────────
		//
		// A tabela de comprimento — sem ela, percorrer a curva a passo constante
		// NAO da velocidade constante. Ver a nota em SplinePath.
		//
		// `_Dirty` em vez de reconstruir por frame: um caminho de 8 pontos vira
		// 128 amostras, e refazer isso a 60 Hz por curva da cena seria pagar
		// todo frame por uma coisa que muda quando o usuario arrasta um ponto.
		bool       _Dirty = true;
		SplinePath _Path;
	};

	// ── A SEQUENCE DENTRO DA CENA ────────────────────────────────────────────
	//
	// Ate aqui o `SequencerPlayer` so era instanciado pela janela do Sequencer.
	// Isso quer dizer que uma cutscene existia enquanto o EDITOR a mostrava, e
	// deixava de existir no Play — a metade de runtime nunca tinha sido ligada.
	//
	// E o mesmo desenho do bug do Control Rig (`SnapControlsToCurrentBones` com
	// um unico chamador, e esse chamador uma janela): uma feature que so vive no
	// editor parece pronta e nao esta.
	//
	// ── POR QUE UM COMPONENTE, E NAO UM CAMPO DA CENA ────────────────────────
	//
	// Uma cena pode ter varias cutscenes: a de abertura, a do elevador, a do
	// chefe. Cada uma tem o proprio tempo, o proprio loop e o proprio estado de
	// "ja tocou". Um campo unico na cena forcaria uma cutscene por nivel, e a
	// segunda exigiria um sistema paralelo — que e como se acumulam dois
	// caminhos para a mesma coisa.
	//
	// A entidade que carrega o componente NAO precisa ser nenhuma das entidades
	// animadas: o binding resolve os alvos por nome. Na pratica ela e um objeto
	// vazio, e isso e bom — a cutscene fica visivel e selecionavel no outliner
	// em vez de escondida numa aba de configuracao da cena.
	struct SequencePlayerComponent
	{
		// UUID do `.axeseq` no AssetDatabase. Nunca um caminho: um path
		// absoluto quebra quando o projeto muda de maquina, e um relativo
		// quebra quando o arquivo e movido dentro do projeto.
		std::string SequenceUUID;

		// Toca sozinha quando a cena comeca.
		bool PlayOnStart = false;

		bool Loop = false;

		// ── QUEM MANDA NA CAMERA ENQUANTO A CUTSCENE TOCA ────────────────────
		//
		// Ligado, e se a sequence animar uma entidade que tenha
		// CameraComponent, a camera do jogo passa a enxergar por ela enquanto
		// a sequence toca, e volta ao normal quando termina.
		//
		// A entidade nao e escolhida aqui de proposito: quem sabe qual camera
		// a cutscene usa e a PROPRIA sequence — e ela ja diz isso ao ter uma
		// track de transform apontando para a camera. Um campo separado seria
		// uma segunda fonte de verdade, que se descola da primeira no dia em
		// que alguem trocar a camera na timeline e esquecer do campo.
		//
		// Se a sequence animar mais de uma camera, vence a primeira do
		// outliner. Corte entre cameras e trabalho de uma track de Event,
		// quando existir.
		bool CameraCut = true;

		// ── ESTADO VIVO (nao serializa) ──────────────────────────────────────
		//
		// O SequenceWorld e o dono do SequencerPlayer; aqui fica so o handle
		// que liga a entidade a ele. Guardar o player DENTRO do componente
		// pareceria mais direto e seria pior: um componente de EnTT e copiado e
		// realocado quando o pool cresce, e o player carrega a copia profunda
		// inteira das bindings.
		bool _Playing = false;
		bool _Started = false;   // ja passou pelo PlayOnStart desta sessao
	};

	// Environment
	struct EnvironmentComponent
	{
		std::string HDRIPath;
		float       SkyboxRotation = 0.0f;

		// ── SKY_OFF_V1 ───────────────────────────────────────────────────
		//
		// Desliga o HDRI SEM apagar o caminho: o arquivo continua no
		// componente, e religar traz tudo de volta.
		//
		// Existe porque nao havia como tirar o HDRI da cena — e ele nao e so
		// fundo, e IBL: enquanto estivesse carregado, era impossivel testar
		// "sem fonte de luz nenhuma". Com isto desligado o cubemap e SOLTO,
		// entao HasSkybox() vira false e o HDRI para de desenhar E de
		// iluminar, que sao as duas coisas ao mesmo tempo.
		bool        UseHDRI = true;
	};

} // namespace axe