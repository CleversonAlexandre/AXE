#pragma once
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include "types.hpp"

#include "axe/log/log.hpp"
namespace axe
{
	//Reprensenta uma ação reversível
	struct Command
	{
		std::string Name; //Descrição da ação ("Add Node", Move Node", etc.)
		std::function<void()> Execute; //executa / refaz
		std::function<void()> Undo; //desfaz
	};

	// Pilha de undo/redo — uma instância por contexto
	//(MaterialEditor, SceneEditor, ScriptEditor, etc.)

	class AXE_API CommandHistory
	{
	public:
		//Executa um comando eo adiciona ao histórico
		void Push(Command cmd)
		{
			if (m_Index + 1 < (int)m_History.size())
				m_History.resize(m_Index + 1);

			// Execute pode ser nullptr quando a ação já foi executada
			// (ex: criar/deletar na hierarchy já aconteceu antes do Push)
			if (cmd.Execute)
				cmd.Execute();

			m_History.push_back(std::move(cmd));
			m_Index = (int)m_History.size() - 1;
		}
		void Undo()
		{
			if (!CanUndo()) return;

			m_RedoStack.push_back(m_History[m_Index]);
			if (m_History[m_Index].Undo) m_History[m_Index].Undo();
			--m_Index;
		}
		void Redo()
		{
			if (!CanRedo()) return;

			++m_Index;
			if (m_History[m_Index].Execute) m_History[m_Index].Execute();

			// Garante que o índice não ultrapassa o tamanho
			m_Index = std::min(m_Index, (int)m_History.size() - 1);
		}

		bool CanUndo() const { return m_Index >= 0; }
		bool CanRedo() const
		{
			return m_Index >= -1 && m_Index + 1 < (int)m_History.size();
		}

		const std::string& GetUndoName() const
		{
			static std::string empty;
			return CanUndo() ? m_History[m_Index].Name : empty;
		}

		const std::string& GetRedoName() const
		{
			static std::string empty;
			return CanRedo() ? m_RedoStack.back().Name : empty;
		}

		void Clear()
		{
			m_History.clear();
			m_RedoStack.clear();
			m_Index = -1;
		}


		void ReplaceTop(Command cmd)
		{
			if (m_History.empty())
			{
				Push(cmd);
				return;
			}
			// Executa o novo comando mas mantém o undo do comando original
			auto originalUndo = m_History.back().Undo;
			cmd.Execute();
			m_History.back().Name = cmd.Name;
			m_History.back().Execute = cmd.Execute;
			m_History.back().Undo = originalUndo; // ← mantém o undo original
			m_RedoStack.clear();
		}
	private:
		std::vector<Command> m_History;
		std::vector<Command> m_RedoStack;
		int m_Index = -1;


	};
}//namespace axe