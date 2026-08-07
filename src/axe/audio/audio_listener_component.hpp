#pragma once
#include "axe/core/types.hpp"

namespace axe
{
	// ── AudioListenerComponent ───────────────────────────────────────────────
	//
	// O "ouvido" da cena: de onde o som e escutado.
	//
	// NA MAIORIA DAS CENAS ESTE COMPONENTE NAO E NECESSARIO. Sem nenhum
	// listener autorado, o AudioWorld usa a CAMERA ATIVA (GameCamera em Play,
	// camera do editor em Edit) — que e o que quase todo jogo quer e o
	// comportamento padrao da Unreal. O componente existe para o caso em que
	// isso nao serve: camera de seguranca, cinematica com escuta presa ao
	// personagem, terceira pessoa com ouvido na cabeca e nao na camera.
	//
	// MAIS DE UM PRIMARY: o primeiro encontrado vence e os outros sao
	// ignorados, com um aviso uma vez por cena. Mesma regra que o
	// CameraComponent ja usa — nao inventamos politica nova, e nao ha
	// assert: dois listeners e um erro de autoria do usuario, e derrubar o
	// editor por dado autorado seria trocar um som errado por um crash.
	struct AXE_API AudioListenerComponent
	{
		bool IsPrimary = true;

		// ── POSICAO daqui, ORIENTACAO da camera ──────────────────────────
		//
		// O listener carrega duas informacoes, e num jogo de terceira pessoa
		// elas querem vir de lugares diferentes:
		//
		//   POSICAO no personagem — para que a DISTANCIA seja medida de onde
		//   o jogador "esta". Com a posicao na camera, um som ao lado do
		//   personagem soa metros mais longe do que deveria, e passos
		//   chegam abafados.
		//
		//   ORIENTACAO na camera — para que o PANNING bata com a tela. Preso
		//   ao personagem, orbitar a camera faz um som visivelmente a
		//   esquerda sair no ouvido direito. A sensacao e de audio quebrado,
		//   sem causa obvia.
		//
		// Ligado por padrao porque terceira pessoa e o caso comum. Desligue
		// para primeira pessoa estrita, onde cabeca e camera sao a mesma
		// coisa e a orientacao da entidade E a certa.
		//
		// (Sem nenhum listener na cena, a camera ativa fornece as duas — e o
		//  comportamento anterior ao A8, e continua valendo.)
		bool UseCameraOrientation = true;
	};

} // namespace axe