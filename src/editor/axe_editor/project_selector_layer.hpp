#pragma once
#include "axe/layers/layer.hpp"
#include <string>
#include <filesystem>
#include <functional>

namespace axe
{
	class ProjectSelectorLayer : public Layer
	{
	public:
		//Callback chamado quando o projeto é selecionado
		using ProjectSelectedCallback = std::function<void(const std::filesystem::path&)>;

		ProjectSelectorLayer(ProjectSelectedCallback callback) :
			Layer("ProjectSelectirLayer"), m_Callback(callback) {}

		void OnAttach() override;
		void OnRender() override;

	private:
		void DrawNewProject();
		void DrawOpenProject();
		void DrawRecentProjects();

		bool TrySelectFolder(std::string& outPath);

		// ═══════════════════════════════════════════════════════════════════════
		//  PROJECT_NEW_V2 — o estado do destino, calculado ENQUANTO se digita
		//
		//  ── O QUE ISTO CONSERTA ────────────────────────────────────────────────
		//
		//  Criar projeto so falhava DEPOIS do clique, com "verifique se a pasta ja
		//  existe". A recusa em si esta certa — sobrescrever a pasta de alguem seria
		//  imperdoavel — mas a ferramenta nunca ajudava a sair dali. O contorno era
		//  ir no Explorer renomear a pasta antiga e voltar.
		//
		//  Sabendo o estado do destino a cada tecla, o botao pode dizer o que vai
		//  acontecer ANTES do clique, e oferecer a saida certa para cada caso:
		//  abrir o projeto que ja esta ali, ou sugerir um nome livre.
		// ═══════════════════════════════════════════════════════════════════════
		enum class TargetState
		{
			Free,             // pasta nao existe — pode criar
			ExistingProject,  // ja ha um .axeproject ali — oferecer abrir
			OccupiedFolder,   // pasta existe e nao e projeto — oferecer outro nome
			InvalidName,      // nome vazio ou com caractere que o disco recusa
			NoPath,
		};

		// Reavaliado por frame. Barato: duas chamadas de filesystem::exists numa
		// caixa de dialogo, e nao num laco de render de cena.
		TargetState EvaluateTarget(std::filesystem::path& outRoot,
			std::filesystem::path& outProjectFile) const;

		// Primeiro nome livre a partir do atual: "Meujogo", "Meujogo_2", ...
		std::string SuggestFreeName() const;

		void DrawSectionTitle(const char* icon, const char* label) const;

		ProjectSelectedCallback m_Callback;

		//Estado do formulario de novo projeto
		char m_ProjectName[256] = "Meujogo";
		char m_ProjectPath[512] = "";
		std::string m_ErrorMessage;

		//Aba ativa 
		enum class Tab { Recent, New, Open };
		Tab m_ActiveTab = Tab::Recent;

		bool m_ShouldClose = false;
	};

}//namespace axe