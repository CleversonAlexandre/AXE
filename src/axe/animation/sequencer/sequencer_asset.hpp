#pragma once

// src/axe/animation/sequencer/sequencer_asset.hpp
//
// SequencerAsset — o "molde" de uma Sequence (cutscene/animacao authorada).
//
// CONFIRMADO contra o repo AXE:
//   - Namespace axe (sem sub-namespace)
//   - UUID e std::string (ver components.hpp: AssetUUID, GraphAssetUUID etc.)
//   - nlohmann/json via <nlohmann/json.hpp> (premake: IncludeDir["nlohmann"] = "src/vendor")
//
// INVARIANTES PRESERVADAS (ver SEQUENCER_DESIGN.md §12):
//   - Asset e molde; instancias (SequencerPlayer) fazem deep copy em OnStart.
//   - Serializa so UUID (std::string) — nunca ponteiro nem path absoluto.
//   - AXE_API em todo tipo que cruza a DLL.
//
// NAO inclui imgui.h — vive em src/axe/. Regra B4/S0b.

#include "axe/core/types.hpp"          // AXE_API
#include "axe/animation/sequencer/sequencer_track.hpp"

#include <memory>
#include <string>
#include <vector>

namespace axe {

    class AXE_API SequencerAsset {
    public:
        SequencerAsset() = default;
        ~SequencerAsset() = default;

        // --- Identidade (preenchido pelo AssetDatabase no load) --------------
        const std::string& GetPath() const { return m_Path; }
        void SetPath(const std::string& path) { m_Path = path; }

        // --- Serializacao .axeseq (JSON, nlohmann) ---------------------------
        //
        // Formato SEQUENCER_V1 (ver SEQUENCER_DESIGN.md §6.1):
        //
        //   {
        //     "version": "SEQUENCER_V1",
        //     "fps": 30,
        //     "frame_range": { "start": 0, "end": 90 },
        //     "bindings": [ { ... } ]
        //   }
        bool LoadFromFile(const std::string& path);
        bool SaveToFile(const std::string& path) const;

        // --- Acessos ao data model -------------------------------------------
        const std::vector<SequencerBinding>& GetBindings() const { return m_Bindings; }

        // Substitui o data model INTEIRO de uma vez.
        //
        // Existe para o undo/redo do editor. A alternativa seria reconstruir a
        // arvore por AddBinding/AddTrack/AddSection/AddChannel/AddKey — o que
        // significa uma funcao de restauracao que precisa conhecer TODO campo do
        // modelo. No dia em que alguem acrescentasse um campo novo (e ja foram
        // quatro nesta sessao), ele ficaria de fora do undo em silencio, e o
        // sintoma seria "desfiz e perdi o offset do socket".
        //
        // Copiar o vetor inteiro e barato aqui pela mesma razao que e barato no
        // SequencerPlayer::SyncFrom, que faz exatamente isto TODO FRAME.
        void SetBindings(std::vector<SequencerBinding> bindings) {
            m_Bindings = std::move(bindings);
        }

        int  GetFps() const { return m_Fps; }
        void SetFps(int fps) { m_Fps = fps; }

        const SequencerFrameRange& GetFrameRange() const { return m_FrameRange; }
        void SetFrameRange(const SequencerFrameRange& r) { m_FrameRange = r; }

        bool IsEmpty() const { return m_Bindings.empty(); }
        size_t GetBindingCount() const { return m_Bindings.size(); }

        // --- Mutacoes (usadas pelo editor + comandos undo/redo) --------------
        int  AddBinding(const SequencerBinding& binding);
        void RemoveBinding(int index);
        SequencerBinding* GetBinding(int index);
        const SequencerBinding* GetBinding(int index) const;

        int  AddTrack(int bindingIndex, const SequencerTrack& track);
        void RemoveTrack(int bindingIndex, int trackIndex);

        int  AddSection(int bindingIndex, int trackIndex, const SequencerSection& section);
        int  AddChannel(int bindingIndex, int trackIndex, int sectionIndex,
            const SequencerChannel& channel);

        // Helper: encontra ou cria a section/channel para o frame atual.
        SequencerChannel* FindOrCreateChannel(int bindingIndex, int trackIndex,
            int sectionIndex,
            SequencerChannelComponent component);

        // Adiciona key no channel dado. Retorna indice da key no channel.
        // Se ja existe key no mesmo frame (com tolerancia 0.01), substitui.
        int AddKey(int bindingIndex, int trackIndex, int sectionIndex,
            int channelIndex, const SequencerKey& key);

        void RemoveKey(int bindingIndex, int trackIndex, int sectionIndex,
            int channelIndex, int keyIndex);

    private:
        std::string                     m_Path;
        int                             m_Fps = 30;
        SequencerFrameRange             m_FrameRange{ 0, 90 };
        std::vector<SequencerBinding>  m_Bindings;
    };

} // namespace axe