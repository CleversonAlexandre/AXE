// src/axe/animation/sequencer/sequence_apply.cpp
//
// Ver a nota de topo do .hpp: aqui mora a matematica de aplicar um sample, e
// so ela. Editor e runtime chamam estas mesmas funcoes.

#include "axe/animation/sequencer/sequence_apply.hpp"

namespace axe::sequence {

    void WriteComponent(BoneTransform& t, SequencerChannelComponent c, float v) {
        glm::vec3 euler = glm::degrees(glm::eulerAngles(t.Rotation));

        switch (c) {
        case SequencerChannelComponent::X:      t.Translation.x = v; return;
        case SequencerChannelComponent::Y:      t.Translation.y = v; return;
        case SequencerChannelComponent::Z:      t.Translation.z = v; return;
        case SequencerChannelComponent::ScaleX: t.Scale.x = v;       return;
        case SequencerChannelComponent::ScaleY: t.Scale.y = v;       return;
        case SequencerChannelComponent::ScaleZ: t.Scale.z = v;       return;
        case SequencerChannelComponent::RotX:   euler.x = v;         break;
        case SequencerChannelComponent::RotY:   euler.y = v;         break;
        case SequencerChannelComponent::RotZ:   euler.z = v;         break;
        }

        t.Rotation = glm::quat(glm::radians(euler));
    }

    bool RotationAxisOf(SequencerChannelComponent c, int& outAxis) {
        switch (c) {
        case SequencerChannelComponent::RotX: outAxis = 0; return true;
        case SequencerChannelComponent::RotY: outAxis = 1; return true;
        case SequencerChannelComponent::RotZ: outAxis = 2; return true;
        default: return false;
        }
    }

    Transform ApplyEntitySamples(const Transform& base,
        const std::vector<SequencerSample>& samples,
        int bindingIndex) {
        Transform t = base;

        for (const auto& s : samples) {
            if (s.BindingIndex != bindingIndex) continue;
            if (s.TargetType != SequencerTargetType::Entity) continue;

            switch (s.Component) {
            case SequencerChannelComponent::X:      t.Position.x = s.Value; break;
            case SequencerChannelComponent::Y:      t.Position.y = s.Value; break;
            case SequencerChannelComponent::Z:      t.Position.z = s.Value; break;

                // GRAUS na curva, RADIANOS no Transform. Ver a nota de unidades
                // no .hpp.
            case SequencerChannelComponent::RotX:   t.Rotation.x = glm::radians(s.Value); break;
            case SequencerChannelComponent::RotY:   t.Rotation.y = glm::radians(s.Value); break;
            case SequencerChannelComponent::RotZ:   t.Rotation.z = glm::radians(s.Value); break;

            case SequencerChannelComponent::ScaleX: t.Scale.x = s.Value; break;
            case SequencerChannelComponent::ScaleY: t.Scale.y = s.Value; break;
            case SequencerChannelComponent::ScaleZ: t.Scale.z = s.Value; break;
            }
        }

        t.UseWorldMatrix = false;
        return t;
    }

    void ApplyBoneSamples(Pose& pose, const Skeleton& skel,
        const std::vector<SequencerSample>& samples,
        int bindingIndex,
        std::vector<BoneEulerEdit>& scratch) {
        scratch.clear();

        for (const auto& s : samples) {
            if (s.BindingIndex != bindingIndex) continue;

            // Controle e socket seguem outro caminho (o rig). Ignorar em
            // silencio aqui e melhor que tratar como osso e mover a coisa
            // errada.
            if (s.TargetType != SequencerTargetType::Bone) continue;

            const int boneIdx = skel.FindBone(s.TargetName);
            if (boneIdx < 0 || boneIdx >= static_cast<int>(pose.Size())) continue;

            int axis = 0;
            if (RotationAxisOf(s.Component, axis)) {
                BoneEulerEdit* ed = nullptr;
                for (auto& e : scratch)
                    if (e.BoneIdx == boneIdx) { ed = &e; break; }

                if (!ed) {
                    BoneEulerEdit fresh;
                    fresh.BoneIdx = boneIdx;
                    // Eixos NAO keyados mantem o valor de repouso do osso.
                    fresh.Euler = glm::degrees(glm::eulerAngles(pose[boneIdx].Rotation));
                    scratch.push_back(fresh);
                    ed = &scratch.back();
                }

                ed->Euler[axis] = s.Value;
                continue;
            }

            WriteComponent(pose[boneIdx], s.Component, s.Value);
        }

        // Uma unica recomposicao de quaternion por osso rotacionado.
        for (const auto& e : scratch)
            pose[e.BoneIdx].Rotation = glm::quat(glm::radians(e.Euler));
    }

    void ApplyControlSamples(RigHierarchy& hierarchy,
        const SequencerBinding& binding,
        const std::vector<SequencerSample>& samples,
        int bindingIndex) {
        // 1. Neutro nos controles que esta sequence dirige. Ver a nota no .hpp.
        for (const auto& tr : binding.Tracks) {
            if (tr.TargetType != SequencerTargetType::Control) continue;

            const int idx = hierarchy.Find(tr.TargetName, RigElementType::Control);
            if (idx < 0) continue;

            RigElement& el = hierarchy[idx];
            if (el.ValueType != RigControlValue::Transform) continue;

            el.Value = BoneTransform{};   // identidade = neutro
        }

        // 2. A pose do animador.
        for (const auto& s : samples) {
            if (s.BindingIndex != bindingIndex) continue;
            if (s.TargetType != SequencerTargetType::Control) continue;

            const int idx = hierarchy.Find(s.TargetName, RigElementType::Control);
            if (idx < 0) continue;

            RigElement& el = hierarchy[idx];

            // ── CONTROLE DE CANAL (interruptor / slider) ─────────────────────
            //
            // BoolValue/FloatValue vivem FORA de Initial/Current e o
            // ResetToInitial nao os toca — de proposito, senao um interruptor
            // voltaria ao padrao a cada quadro. Entao escrever aqui basta.
            if (el.ValueType != RigControlValue::Transform) {
                el.FloatValue = s.Value;
                el.BoolValue = (s.Value >= 0.5f);
                continue;
            }

            glm::vec3 euler = glm::degrees(glm::eulerAngles(el.Value.Rotation));

            switch (s.Component) {
            case SequencerChannelComponent::X:      el.Value.Translation.x = s.Value; break;
            case SequencerChannelComponent::Y:      el.Value.Translation.y = s.Value; break;
            case SequencerChannelComponent::Z:      el.Value.Translation.z = s.Value; break;
            case SequencerChannelComponent::ScaleX: el.Value.Scale.x = s.Value; break;
            case SequencerChannelComponent::ScaleY: el.Value.Scale.y = s.Value; break;
            case SequencerChannelComponent::ScaleZ: el.Value.Scale.z = s.Value; break;

            case SequencerChannelComponent::RotX:   euler.x = s.Value;
                el.Value.Rotation = glm::quat(glm::radians(euler)); break;
            case SequencerChannelComponent::RotY:   euler.y = s.Value;
                el.Value.Rotation = glm::quat(glm::radians(euler)); break;
            case SequencerChannelComponent::RotZ:   euler.z = s.Value;
                el.Value.Rotation = glm::quat(glm::radians(euler)); break;
            }
        }
    }

} // namespace axe::sequence