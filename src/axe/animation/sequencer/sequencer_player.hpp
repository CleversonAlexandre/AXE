#pragma once

// src/axe/animation/sequencer/sequencer_player.hpp
//
// SequencerPlayer — a "instancia viva" de uma Sequence por entidade.
//
// CONFIRMADO contra o repo AXE:
//   - Namespace axe (sem sub-namespace)
//   - AXE_API de axe/core/types.hpp
//
// INVARIANTES:
//   - Deep copy em OnStart (R1 no SEQUENCER_DESIGN.md §12).
//   - Single-threaded (NF1 — ROADMAP §7).
//   - NAO inclui imgui.h — vive em src/axe/.
//   - AXE_API porque cruza a DLL.

#include "axe/core/types.hpp"
#include "axe/animation/sequencer/sequencer_track.hpp"

#include <vector>

namespace axe {

    class SequencerAsset;

    enum class SequencerPlaybackMode : uint8_t {
        Paused = 0,
        Playing = 1,
        Scrubbing = 2
    };

    class AXE_API SequencerPlayer {
    public:
        SequencerPlayer() = default;
        ~SequencerPlayer() = default;

        // --- Ciclo de vida -------------------------------------------------
        //
        // OnStart: faz deep copy do asset (mold -> copia viva).
        //   IMPORTANTE: deep copy, NUNCA shared_ptr. (R1 no SEQUENCER_DESIGN §12.)
        void OnStart(const SequencerAsset& asset);
        void OnUpdate(float deltaTime);
        void OnStop();

        // Re-sincroniza a copia viva com o asset, PRESERVANDO frame e modo.
        //
        // POR QUE ISTO PRECISA EXISTIR:
        //   O player guarda um deep copy das bindings (R1 do design — duas
        //   entidades tocando a mesma sequence nao podem compartilhar tempo nem
        //   curvas). No RUNTIME isso esta certo, e o asset nunca muda.
        //
        //   No EDITOR o asset muda o tempo todo: cada key nova, cada drag, cada
        //   track. So com o OnStart essas edicoes NAO chegavam na copia — o
        //   usuario cravava uma key e o sample continuava lendo o estado de
        //   quando o player comecou. Era o segundo motivo de "nada acontece", e o
        //   mais silencioso de todos: a UI mostrava a key no lugar certo.
        //
        //   OnStart nao resolveria: ele reseta o frame para o inicio, e scrubar
        //   viraria impossivel.
        void SyncFrom(const SequencerAsset& asset);

        // --- Scrub ---------------------------------------------------------
        void Scrub(float frame) { m_CurrentFrame = frame; }

        // --- Playback config -----------------------------------------------
        // fps <= 0 congela o tempo (`frame += dt * fps`) sem erro nenhum — foi
        // assim que um combo mal ligado deixou o Play sem efeito. O clamp aqui
        // garante que nenhum caminho futuro consiga repetir isso.
        void  SetFps(int fps) { m_Fps = (fps > 0) ? fps : 30; }
        int   GetFps() const { return m_Fps; }

        void  SetLoop(bool loop) { m_Loop = loop; }
        bool  GetLoop() const { return m_Loop; }

        void  SetFrameRange(int start, int end) {
            m_StartFrame = static_cast<float>(start);
            m_EndFrame = static_cast<float>(end);
        }

        // --- Getters (UI usa) ----------------------------------------------
        float                 GetCurrentFrame() const { return m_CurrentFrame; }
        SequencerPlaybackMode GetMode()         const { return m_Mode; }
        void                  SetMode(SequencerPlaybackMode mode) { m_Mode = mode; }

        const std::vector<SequencerSample>& GetLastSamples() const { return m_LastSamples; }

        // Camada BASE: qual clipe cada binding esta tocando neste frame, e em que
        // segundo dele. Lista separada de propositio — ver SequencerClipSample.
        const std::vector<SequencerClipSample>& GetLastClipSamples() const {
            return m_LastClipSamples;
        }

        // Forca resample no frame atual (chamado pelo editor apos Scrub).
        void Resample();

    private:
        float                 m_CurrentFrame = 0.0f;
        float                 m_StartFrame = 0.0f;
        float                 m_EndFrame = 90.0f;
        int                   m_Fps = 30;
        bool                  m_Loop = false;
        SequencerPlaybackMode m_Mode = SequencerPlaybackMode::Paused;

        // Deep copy do asset (m_Bindings) — mutavel em runtime (drag de key
        // durante Play afeta so isto, nao o asset em disco).
        std::vector<SequencerBinding> m_Bindings;

        // Cache dos samples do ultimo OnUpdate/Resample.
        std::vector<SequencerSample>     m_LastSamples;
        std::vector<SequencerClipSample> m_LastClipSamples;

        // Interpola entre dois keys conforme Interp. t e [0,1].
        static float Interpolate(const SequencerKey& left, const SequencerKey& right, float frame);
    };

} // namespace axe