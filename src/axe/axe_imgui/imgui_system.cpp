#include "axe/axe_imgui/imgui_system.hpp"
#include "axe/axe_window/window.hpp"
#include "axe/log/log.hpp"
#include "editor/axe_editor/editor_app.hpp"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <ImGuizmo.h>

namespace axe
{

    bool ImGuiSystem::Initialize(axe::Window* window)
    {
        IMGUI_CHECKVERSION();
        m_Context = ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

      

        m_NativeWindow = static_cast<GLFWwindow*>(window->GetNativeWindow());

        if (!ImGui_ImplGlfw_InitForOpenGL(m_NativeWindow, true))
        {
            AXE_CORE_ERROR("Failed to initialize ImGui GLFW backend");
            return false;
        }

        if (!ImGui_ImplOpenGL3_Init("#version 450"))
        {
            AXE_CORE_ERROR("Failed to initialize ImGui OpenGL3 backend");
            return false;
        }

        //AXE_CORE_INFO("ImGui initialized");
        return true;
    }

    void ImGuiSystem::Shutdown()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        AXE_CORE_INFO("ImGui shutdown");
    }

    void ImGuiSystem::BeginFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
    }

    void ImGuiSystem::EndFrame()
    {
        ImGui::Render();

        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData && drawData->Valid)
            ImGui_ImplOpenGL3_RenderDrawData(drawData);
    }

    void ImGuiSystem::OnEvent(Event& event)
    {
        // Backend GLFW cuida de todos os eventos — não precisamos processar manualmente
    }

} // namespace axe