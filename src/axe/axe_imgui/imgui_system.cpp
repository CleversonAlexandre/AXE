#include "axe/axe_imgui/imgui_system.hpp"
#include "axe/axe_window/window.hpp"
#include "axe/log/log.hpp"

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

        // ── Estilo global — visual "moderno", consistente com os cards arredondados
        // já usados manualmente no Script Editor (Components/Variables/Events).
        // Sem isso, os widgets nativos do ImGui (InputText, Combo, sliders, botões
        // padrão) ficam com cantos retos e contraste chapado — destoando do resto.
        {
            ImGuiStyle& style = ImGui::GetStyle();
            ImGui::StyleColorsDark(&style);

            // Arredondamento pronunciado (~8-10px), estilo UE5/Figma
            style.WindowRounding = 8.0f;
            style.ChildRounding = 8.0f;
            style.FrameRounding = 8.0f;   // InputText, Combo, sliders, checkboxes
            style.PopupRounding = 8.0f;
            style.ScrollbarRounding = 10.0f;
            style.GrabRounding = 8.0f;    // bolinha de sliders/scrollbar
            style.TabRounding = 8.0f;

            // Bordas finas e discretas em vez de chapadas
            style.WindowBorderSize = 1.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupBorderSize = 1.0f;
            style.FrameBorderSize = 1.0f;
            style.TabBorderSize = 0.0f;

            // Respiro — mais espaço interno e entre itens, sem ficar exagerado
            style.FramePadding = ImVec2(8.0f, 5.0f);
            style.ItemSpacing = ImVec2(8.0f, 6.0f);
            style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
            style.WindowPadding = ImVec2(8.0f, 8.0f);
            style.ScrollbarSize = 14.0f;
            style.GrabMinSize = 10.0f;
            style.IndentSpacing = 18.0f;

            // Cores — fundo dos campos com mais profundidade (não tão chapado/escuro
            // quanto o padrão), e accent azul consistente com os cards já feitos.
            ImVec4* c = style.Colors;
            c[ImGuiCol_WindowBg] = ImVec4(0.098f, 0.106f, 0.133f, 1.00f);
            c[ImGuiCol_ChildBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            c[ImGuiCol_PopupBg] = ImVec4(0.087f, 0.094f, 0.118f, 0.98f);
            c[ImGuiCol_Border] = ImVec4(1.000f, 1.000f, 1.000f, 0.08f);
            c[ImGuiCol_FrameBg] = ImVec4(0.165f, 0.176f, 0.212f, 1.00f);
            c[ImGuiCol_FrameBgHovered] = ImVec4(0.204f, 0.220f, 0.267f, 1.00f);
            c[ImGuiCol_FrameBgActive] = ImVec4(0.165f, 0.176f, 0.212f, 1.00f);
            c[ImGuiCol_TitleBg] = ImVec4(0.071f, 0.078f, 0.098f, 1.00f);
            c[ImGuiCol_TitleBgActive] = ImVec4(0.110f, 0.122f, 0.165f, 1.00f);
            c[ImGuiCol_MenuBarBg] = ImVec4(0.087f, 0.094f, 0.118f, 1.00f);
            c[ImGuiCol_ScrollbarBg] = ImVec4(0.071f, 0.078f, 0.098f, 0.60f);
            c[ImGuiCol_ScrollbarGrab] = ImVec4(0.300f, 0.310f, 0.350f, 1.00f);
            c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.380f, 0.390f, 0.430f, 1.00f);
            c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.450f, 0.460f, 0.500f, 1.00f);
            c[ImGuiCol_CheckMark] = ImVec4(0.270f, 0.600f, 1.000f, 1.00f);
            c[ImGuiCol_SliderGrab] = ImVec4(0.270f, 0.600f, 1.000f, 1.00f);
            c[ImGuiCol_SliderGrabActive] = ImVec4(0.350f, 0.680f, 1.000f, 1.00f);
            c[ImGuiCol_Button] = ImVec4(0.180f, 0.192f, 0.231f, 1.00f);
            c[ImGuiCol_ButtonHovered] = ImVec4(0.230f, 0.245f, 0.290f, 1.00f);
            c[ImGuiCol_ButtonActive] = ImVec4(0.150f, 0.160f, 0.196f, 1.00f);
            c[ImGuiCol_Header] = ImVec4(0.180f, 0.220f, 0.330f, 1.00f);
            c[ImGuiCol_HeaderHovered] = ImVec4(0.230f, 0.280f, 0.400f, 1.00f);
            c[ImGuiCol_HeaderActive] = ImVec4(0.200f, 0.250f, 0.360f, 1.00f);
            c[ImGuiCol_Separator] = ImVec4(1.000f, 1.000f, 1.000f, 0.07f);
            c[ImGuiCol_Tab] = ImVec4(0.110f, 0.122f, 0.157f, 1.00f);
            c[ImGuiCol_TabHovered] = ImVec4(0.200f, 0.250f, 0.360f, 1.00f);
            c[ImGuiCol_TabActive] = ImVec4(0.160f, 0.200f, 0.290f, 1.00f);
            c[ImGuiCol_DockingPreview] = ImVec4(0.270f, 0.600f, 1.000f, 0.35f);
            c[ImGuiCol_ResizeGrip] = ImVec4(0.270f, 0.600f, 1.000f, 0.20f);
            c[ImGuiCol_ResizeGripHovered] = ImVec4(0.270f, 0.600f, 1.000f, 0.55f);
            c[ImGuiCol_ResizeGripActive] = ImVec4(0.270f, 0.600f, 1.000f, 0.80f);
        }
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