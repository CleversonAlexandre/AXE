#include "axe/particles/particle_system_asset.hpp"
#include "axe/log/log.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace axe
{
    using json = nlohmann::json;

    // Serializa um único emitter pra JSON
    static json SerializeEmitter(const ParticleEmitterDef& e)
    {
        json j;
        j["name"] = e.Name;
        j["enabled"] = e.Enabled;
        j["duration"] = e.Duration;
        j["auto_destroy"] = e.AutoDestroy;
        j["warmup_time"] = e.WarmupTime;
        j["lod_enabled"] = e.LODEnabled;
        j["lod_distance_full"] = e.LODDistanceFull;
        j["lod_distance_zero"] = e.LODDistanceZero;

        j["emission_rate"] = e.EmissionRate;
        j["max_particles"] = e.MaxParticles;
        j["lifetime"] = e.Lifetime;
        j["lifetime_variation"] = e.LifetimeVariation;

        j["start_velocity"] = { e.StartVelocity.x,     e.StartVelocity.y,     e.StartVelocity.z };
        j["velocity_variation"] = { e.VelocityVariation.x, e.VelocityVariation.y, e.VelocityVariation.z };
        j["gravity"] = { e.Gravity.x,           e.Gravity.y,           e.Gravity.z };

        j["spawn_shape"] = e.SpawnShape;
        j["spawn_radius"] = e.SpawnRadius;
        j["spawn_height"] = e.SpawnHeight;
        j["spawn_helix_turns"] = e.SpawnHelixTurns;
        j["spawn_offset"] = { e.SpawnOffset.x, e.SpawnOffset.y, e.SpawnOffset.z };
        j["spawn_on_surface"] = e.SpawnOnSurface;
        j["velocity_follows_shape"] = e.VelocityFollowsShape;

        j["color_start"] = { e.ColorStart.x, e.ColorStart.y, e.ColorStart.z, e.ColorStart.w };
        j["color_end"] = { e.ColorEnd.x,   e.ColorEnd.y,   e.ColorEnd.z,   e.ColorEnd.w };
        j["size_start"] = e.SizeStart;
        j["size_end"] = e.SizeEnd;
        j["size_variation"] = e.SizeVariation;
        j["stretch_amount"] = e.StretchAmount;
        j["is_ribbon"] = e.IsRibbon;
        j["is_beam"] = e.IsBeam;
        j["beam_target"] = { e.BeamTargetOffset.x, e.BeamTargetOffset.y, e.BeamTargetOffset.z };
        j["beam_points"] = e.BeamPoints;
        j["beam_deviation"] = e.BeamDeviation;
        j["beam_flicker_speed"] = e.BeamFlickerSpeed;
        j["beam_width"] = e.BeamWidth;
        j["velocity_limit"] = e.VelocityLimit;
        j["rotation_min"] = e.RotationMin;
        j["rotation_max"] = e.RotationMax;
        j["rotation_speed_min"] = e.RotationSpeedMin;
        j["rotation_speed_max"] = e.RotationSpeedMax;
        j["spawn_cone_angle"] = e.SpawnConeAngle;
        // Color Curve
        json ccj = json::array();
        for (auto& k : e.ColorCurve)
            ccj.push_back({ {"t", k.Time}, {"r", k.Color.r}, {"g", k.Color.g}, {"b", k.Color.b}, {"a", k.Color.a} });
        j["color_curve"] = ccj;
        // Size Curve
        json scj = json::array();
        for (auto& k : e.SizeCurve)
            scj.push_back({ {"t", k.Time}, {"s", k.Size} });
        j["size_curve"] = scj;

        j["orbit_strength"] = e.OrbitStrength;
        j["orbit_axis"] = { e.OrbitAxis.x, e.OrbitAxis.y, e.OrbitAxis.z };
        j["turbulence_strength"] = e.TurbulenceStrength;
        j["turbulence_frequency"] = e.TurbulenceFrequency;
        j["turbulence_speed"] = e.TurbulenceSpeed;
        j["local_space"] = e.LocalSpace;
        j["collision_enabled"] = e.CollisionEnabled;
        j["collision_y"] = e.CollisionY;
        j["collision_bounciness"] = e.CollisionBounciness;
        j["collision_friction"] = e.CollisionFriction;

        j["particle_material_uuid"] = e.ParticleMaterialUUID;
        j["blend_mode"] = e.BlendMode;

        // Bursts
        json burstsJson = json::array();
        for (auto& b : e.Bursts)
        {
            json bj;
            bj["time"] = b.Time;
            bj["count"] = b.Count;
            bj["cycles"] = b.Cycles;
            bj["interval"] = b.Interval;
            burstsJson.push_back(bj);
        }
        j["bursts"] = burstsJson;

        // Flipbook
        j["flipbook_enabled"] = e.FlipbookEnabled;
        j["flipbook_cols"] = e.FlipbookCols;
        j["flipbook_rows"] = e.FlipbookRows;
        j["flipbook_cycles"] = e.FlipbookCycles;
        j["sub_emitter_uuid"] = e.SubEmitterUUID;
        j["light_enabled"] = e.LightEnabled;
        j["light_color"] = { e.LightColor.x, e.LightColor.y, e.LightColor.z };
        j["light_intensity"] = e.LightIntensity;
        j["light_radius"] = e.LightRadius;
        j["light_scale_by_particles"] = e.LightScaleByParticles;
        j["light_flicker"] = e.LightFlicker;
        j["light_flicker_speed"] = e.LightFlickerSpeed;
        j["light_flicker_amount"] = e.LightFlickerAmount;
        return j;
    }

    // Deserializa um emitter a partir de JSON (com defaults pra retrocompat)
    static ParticleEmitterDef DeserializeEmitter(const json& j)
    {
        ParticleEmitterDef e;
        e.Name = j.value("name", "Emitter");
        e.Enabled = j.value("enabled", true);
        e.Duration = j.value("duration", -1.0f);
        e.AutoDestroy = j.value("auto_destroy", false);
        e.WarmupTime = j.value("warmup_time", 0.0f);
        e.LODEnabled = j.value("lod_enabled", false);
        e.LODDistanceFull = j.value("lod_distance_full", 20.0f);
        e.LODDistanceZero = j.value("lod_distance_zero", 60.0f);

        e.EmissionRate = j.value("emission_rate", 30.0f);
        e.MaxParticles = j.value("max_particles", 500);
        e.Lifetime = j.value("lifetime", 2.0f);
        e.LifetimeVariation = j.value("lifetime_variation", 0.3f);

        if (j.contains("start_velocity") && j["start_velocity"].size() == 3)
            e.StartVelocity = { j["start_velocity"][0], j["start_velocity"][1], j["start_velocity"][2] };
        if (j.contains("velocity_variation") && j["velocity_variation"].size() == 3)
            e.VelocityVariation = { j["velocity_variation"][0], j["velocity_variation"][1], j["velocity_variation"][2] };
        if (j.contains("gravity") && j["gravity"].size() == 3)
            e.Gravity = { j["gravity"][0], j["gravity"][1], j["gravity"][2] };

        e.SpawnShape = j.value("spawn_shape", 0);
        e.SpawnRadius = j.value("spawn_radius", 0.5f);
        e.SpawnHeight = j.value("spawn_height", 2.0f);
        e.SpawnHelixTurns = j.value("spawn_helix_turns", 3.0f);
        if (j.contains("spawn_offset") && j["spawn_offset"].size() == 3)
            e.SpawnOffset = { j["spawn_offset"][0], j["spawn_offset"][1], j["spawn_offset"][2] };
        e.SpawnOnSurface = j.value("spawn_on_surface", true);
        e.VelocityFollowsShape = j.value("velocity_follows_shape", false);

        if (j.contains("color_start") && j["color_start"].size() == 4)
            e.ColorStart = { j["color_start"][0], j["color_start"][1], j["color_start"][2], j["color_start"][3] };
        if (j.contains("color_end") && j["color_end"].size() == 4)
            e.ColorEnd = { j["color_end"][0], j["color_end"][1], j["color_end"][2], j["color_end"][3] };

        e.SizeStart = j.value("size_start", 0.4f);
        e.SizeEnd = j.value("size_end", 0.0f);
        e.SizeVariation = j.value("size_variation", 0.2f);
        e.StretchAmount = j.value("stretch_amount", 0.0f);
        e.IsRibbon = j.value("is_ribbon", false);
        e.IsBeam = j.value("is_beam", false);
        if (j.contains("beam_target") && j["beam_target"].size() == 3)
            e.BeamTargetOffset = { j["beam_target"][0], j["beam_target"][1], j["beam_target"][2] };
        e.BeamPoints = j.value("beam_points", 24);
        e.BeamDeviation = j.value("beam_deviation", 0.4f);
        e.BeamFlickerSpeed = j.value("beam_flicker_speed", 10.f);
        e.BeamWidth = j.value("beam_width", 0.04f);
        e.VelocityLimit = j.value("velocity_limit", 0.0f);
        e.RotationMin = j.value("rotation_min", 0.0f);
        e.RotationMax = j.value("rotation_max", 6.2831853f);
        e.RotationSpeedMin = j.value("rotation_speed_min", -1.5f);
        e.RotationSpeedMax = j.value("rotation_speed_max", 1.5f);
        e.SpawnConeAngle = j.value("spawn_cone_angle", 25.0f);
        e.ColorCurve.clear();
        if (j.contains("color_curve") && j["color_curve"].is_array())
            for (auto& k : j["color_curve"])
                e.ColorCurve.push_back({ k.value("t",0.0f), { k.value("r",1.0f), k.value("g",1.0f), k.value("b",1.0f), k.value("a",1.0f) } });
        e.SizeCurve.clear();
        if (j.contains("size_curve") && j["size_curve"].is_array())
            for (auto& k : j["size_curve"])
                e.SizeCurve.push_back({ k.value("t",0.0f), k.value("s",1.0f) });

        e.OrbitStrength = j.value("orbit_strength", 0.0f);
        if (j.contains("orbit_axis") && j["orbit_axis"].size() == 3)
            e.OrbitAxis = { j["orbit_axis"][0], j["orbit_axis"][1], j["orbit_axis"][2] };
        e.TurbulenceStrength = j.value("turbulence_strength", 0.0f);
        e.TurbulenceFrequency = j.value("turbulence_frequency", 1.0f);
        e.TurbulenceSpeed = j.value("turbulence_speed", 1.0f);
        e.LocalSpace = j.value("local_space", false);
        e.CollisionEnabled = j.value("collision_enabled", false);
        e.CollisionY = j.value("collision_y", 0.0f);
        e.CollisionBounciness = j.value("collision_bounciness", 0.3f);
        e.CollisionFriction = j.value("collision_friction", 0.5f);

        // Suporta tanto "particle_material_uuid" quanto o antigo "texture_uuid"
        e.ParticleMaterialUUID = j.value("particle_material_uuid", j.value("texture_uuid", std::string("")));
        e.BlendMode = j.value("blend_mode", 1);

        // Bursts
        e.Bursts.clear();
        if (j.contains("bursts") && j["bursts"].is_array())
        {
            for (auto& bj : j["bursts"])
            {
                ParticleBurstDef b;
                b.Time = bj.value("time", 0.0f);
                b.Count = bj.value("count", 10);
                b.Cycles = bj.value("cycles", 1);
                b.Interval = bj.value("interval", 1.0f);
                e.Bursts.push_back(b);
            }
        }

        // Flipbook
        e.FlipbookEnabled = j.value("flipbook_enabled", false);
        e.FlipbookCols = j.value("flipbook_cols", 4);
        e.FlipbookRows = j.value("flipbook_rows", 4);
        e.FlipbookCycles = j.value("flipbook_cycles", 1.0f);
        e.SubEmitterUUID = j.value("sub_emitter_uuid", std::string(""));
        e.LightEnabled = j.value("light_enabled", false);
        if (j.contains("light_color") && j["light_color"].size() == 3)
            e.LightColor = { j["light_color"][0], j["light_color"][1], j["light_color"][2] };
        e.LightIntensity = j.value("light_intensity", 3.0f);
        e.LightRadius = j.value("light_radius", 5.0f);
        e.LightScaleByParticles = j.value("light_scale_by_particles", true);
        e.LightFlicker = j.value("light_flicker", false);
        e.LightFlickerSpeed = j.value("light_flicker_speed", 8.0f);
        e.LightFlickerAmount = j.value("light_flicker_amount", 0.4f);
        return e;
    }

    std::shared_ptr<ParticleSystemAsset> ParticleSystemAsset::Create(const std::string& name)
    {
        auto asset = std::make_shared<ParticleSystemAsset>();
        asset->m_Name = name;
        return asset;
    }

    std::shared_ptr<ParticleSystemAsset> ParticleSystemAsset::LoadFromFile(const std::filesystem::path& filepath)
    {
        auto asset = std::make_shared<ParticleSystemAsset>();
        asset->Emitters.clear(); // remove o emitter default antes de carregar
        if (!asset->Load(filepath)) return nullptr;
        return asset;
    }

    bool ParticleSystemAsset::Save(const std::filesystem::path& filepath)
    {
        json root;
        root["name"] = m_Name;
        root["version"] = "2.0";
        root["looping"] = Looping;

        json emittersJson = json::array();
        for (auto& e : Emitters)
            emittersJson.push_back(SerializeEmitter(e));
        root["emitters"] = emittersJson;

        std::filesystem::create_directories(filepath.parent_path());
        std::ofstream file(filepath);
        if (!file.is_open())
        {
            AXE_CORE_ERROR("ParticleSystemAsset: falha ao salvar '{}'", filepath.string());
            return false;
        }
        file << root.dump(4);
        m_FilePath = filepath;
        AXE_CORE_INFO("ParticleSystemAsset: salvo em '{}'", filepath.string());
        return true;
    }

    bool ParticleSystemAsset::Load(const std::filesystem::path& filepath)
    {
        if (!std::filesystem::exists(filepath))
        {
            AXE_CORE_ERROR("ParticleSystemAsset: '{}' não encontrado.", filepath.string());
            return false;
        }

        std::ifstream file(filepath);
        json root;
        try { root = json::parse(file); }
        catch (const json::exception& e)
        {
            AXE_CORE_ERROR("ParticleSystemAsset: erro ao parsear '{}': {}", filepath.string(), e.what());
            return false;
        }

        m_FilePath = filepath;
        m_Name = root.value("name", filepath.stem().string());
        Looping = root.value("looping", true);

        Emitters.clear();

        if (root.contains("emitters") && root["emitters"].is_array())
        {
            // Formato v2: lista de emissores
            for (auto& ej : root["emitters"])
                Emitters.push_back(DeserializeEmitter(ej));
        }
        else
        {
            // Retrocompat: formato v1 (parâmetros no nível raiz) →
            // migra automaticamente pra um único emitter.
            AXE_CORE_INFO("ParticleSystemAsset: migrando '{}' para formato v2.", filepath.string());
            Emitters.push_back(DeserializeEmitter(root));
            Emitters[0].Name = "Emitter";
        }

        if (Emitters.empty())
            Emitters.emplace_back(); // garante pelo menos um

        return true;
    }

} // namespace axe