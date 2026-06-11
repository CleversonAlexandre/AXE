#pragma once
#include "axe/core/types.hpp"
#include "axe/utils/glm_config.hpp"
#include <memory>
#include <functional>

namespace axe
{
    using TriggerEnterCallback = std::function<void(uint32_t, uint32_t)>;
    using TriggerExitCallback = std::function<void(uint32_t, uint32_t)>;

    struct RaycastHit
    {
        bool      Hit = false;
        glm::vec3 Point = {};
        glm::vec3 Normal = {};
        float     Distance = 0.0f;
        uint32_t  BodyID = 0xffffffff;
    };

    class AXE_API PhysicsSystem
    {
    public:
        static PhysicsSystem& Get()
        {
            static PhysicsSystem instance;
            return instance;
        }

        void Initialize();
        void Shutdown();
        void Step(float deltaTime);

        // Retorna referência opaca ao BodyInterface — só usar em .cpp que inclui Jolt
        void* GetBodyInterfacePtr();

        RaycastHit Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 1000.0f);
        void SetTriggerCallbacks(TriggerEnterCallback onEnter, TriggerExitCallback onExit);

        bool IsInitialized() const { return m_Initialized; }
        void* GetJoltSystemPtr() { return m_PhysicsSystemPtr; }
        void* GetTempAllocPtr() { return m_TempAllocPtr; }
        void* GetBPLayerPtr() { return m_BPLayerInterface; }
        void* GetObjVsBPPtr() { return m_ObjVsBPFilter; }
        void* GetObjVsObjPtr() { return m_ObjVsObjFilter; }

    private:
        PhysicsSystem() = default;
        ~PhysicsSystem() = default;

        bool     m_Initialized = false;
        void* m_PhysicsSystemPtr = nullptr;
        void* m_TempAllocPtr = nullptr;
        void* m_JobSystemPtr = nullptr;
        void* m_BPLayerInterface = nullptr;
        void* m_ObjVsBPFilter = nullptr;
        void* m_ObjVsObjFilter = nullptr;

        TriggerEnterCallback m_OnTriggerEnter;
        TriggerExitCallback  m_OnTriggerExit;

        static constexpr uint32_t s_MaxBodies = 65536;
        static constexpr uint32_t s_MaxBodyPairs = 65536;
        static constexpr uint32_t s_MaxContactConstraints = 10240;
        static constexpr float    s_FixedTimeStep = 1.0f / 60.0f;
        float                     m_Accumulator = 0.0f;
    };

} // namespace axe