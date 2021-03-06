#include "TeEditor.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"

#include "Widget/TeWidgetMenuBar.h"
#include "Widget/TeWidgetToolBar.h"
#include "Widget/TeWidgetProject.h"
#include "Widget/TeWidgetProperties.h"
#include "Widget/TeWidgetProfiler.h"
#include "Widget/TeWidgetSettings.h"
#include "Widget/TeWidgetRenderOptions.h"
#include "Widget/TeWidgetConsole.h"
#include "Widget/TeWidgetViewport.h"
#include "Widget/TeWidgetResources.h"
#include "Widget/TeWidgetScript.h"
#include "Widget/TeWidgetMaterials.h"

#include "Gui/TeGuiAPI.h"
#include "TeCoreApplication.h"
#include "Renderer/TeCamera.h"
#include "Scene/TeSceneManager.h"
#include "Scene/TeSceneObject.h"
#include "Components/TeCCamera.h"
#include "Components/TeCCameraUI.h"
#include "Components/TeCCameraFlyer.h"
#include "Components/TeCScript.h"
#include "Components/TeCRenderable.h"
#include "Components/TeCLight.h"
#include "Components/TeCSkybox.h"
#include "Components/TeCAnimation.h"
#include "Components/TeCBone.h"
#include "Components/TeCAudioSource.h"
#include "Components/TeCAudioListener.h"
#include "Components/TeCRigidBody.h"
#include "Components/TeCSoftBody.h"
#include "Components/TeCConeTwistJoint.h"
#include "Components/TeCD6Joint.h"
#include "Components/TeCHingeJoint.h"
#include "Components/TeCSliderJoint.h"
#include "Components/TeCSphericalJoint.h"
#include "Components/TeCBoxCollider.h"
#include "Components/TeCPlaneCollider.h"
#include "Components/TeCSphereCollider.h"
#include "Components/TeCCylinderCollider.h"
#include "Components/TeCCapsuleCollider.h"
#include "Components/TeCMeshCollider.h"
#include "Components/TeCConeCollider.h"
#include "Components/TeCHeightFieldCollider.h"
#include "Components/TeCAnimation.h"
#include "Scene/TeSceneManager.h"
#include "Resources/TeResourceManager.h"
#include "Resources/TeBuiltinResources.h"
#include "TeEditorResManager.h"
#include "Mesh/TeMesh.h"
#include "Math/TeVector2I.h"
#include "Image/TeColor.h"
#include "TeEditorUtils.h"
#include "Physics/TePhysics.h"

#include "Selection/TePicking.h"
#include "Selection/TeSelection.h"
#include "Selection/TeHud.h"

#include "RenderAPI/TeRenderTarget.h"

// TODO Temp for debug purpose
#include "Importer/TeImporter.h"
#include "Importer/TeMeshImportOptions.h"
#include "Importer/TeTextureImportOptions.h"
#include "Audio/TeAudioClipImportOptions.h"
#include "Material/TeMaterial.h"
#include "Material/TeShader.h"
#include "Threading/TeTaskScheduler.h"

#ifndef GImGui
ImGuiContext* GImGui = NULL;
#endif

namespace te
{
    TE_MODULE_STATIC_MEMBER(Editor)

    const Vector<UINT32> Editor::ComponentsWhichNeedGuizmo = {
        TID_CRenderable,
        TID_CRigidBody,
        TID_CSoftBody,
        TID_CLight,
        TID_CCamera,
        TID_CCameraFlyer,
        TID_CAudioSource,
        TID_CAudioListener,
        TID_CBoxCollider,
        TID_CCapsuleCollider,
        TID_CConeCollider,
        TID_CCylinderCollider,
        TID_CHeightFieldCollider,
        TID_CMeshCollider,
        TID_CPlaneCollider,
        TID_CSphereCollider,
        TID_CConeTwistJoint,
        TID_CD6Joint,
        TID_CHingeJoint,
        TID_CSliderJoint,
        TID_CSphericalJoint
    };

    Editor::Editor()
        : _editorBegun(false)
        , _pickingDirty(true)
        , _selectionDirty(true)
        , _hudDirty(true)
        , _physicsDirty(true)
        , _guizmoState(ImGuizmoState::Active)
        , _guizmoOperation(ImGuizmo::OPERATION::TRANSLATE)
        , _guizmoMode(ImGuizmo::MODE::WORLD)
    { }

    Editor::~Editor()
    {
        _widgets.clear();
    }

    void Editor::OnStartUp()
    {
        EditorResManager::StartUp();

        InitializeInput();
        InitializeScene();
        InitializeUICamera();
        InitializeViewportCamera();

        _picking = te_unique_ptr_new<Picking>();
        _selection = te_unique_ptr_new<Selection>();
        _hud = te_unique_ptr_new<Hud>();

        _picking->Initialize();
        _selection->Initialize();
        _hud->Initialize();

        if (GuiAPI::Instance().IsGuiInitialized())
            InitializeGui();
    }

    void Editor::OnShutDown()
    { 
        EditorResManager::ShutDown();
    }

    void Editor::Update()
    {
        if (GuiAPI::Instance().IsGuiInitialized())
        {
            GuiAPI::Instance().BeginFrame();
            GuiAPI::Instance().Update();

            ImGuizmo::BeginFrame();

            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
                BeginGui();

            for (auto widget : _widgets)
            {
                if (widget->BeginGui())
                {
                    widget->Update();
                    widget->EndGui();
                }
                else
                {
                    widget->UpdateBackground();
                }
            }

            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
                EndGui();
        }
    }

    void Editor::PostRender()
    {
        if (_previewViewportCamera->GetRenderSettings()->OverlayOnly)
            return;

        if (_hudDirty && _previewViewportCamera == _viewportCamera) // only for default camera
        {
            _hud->Render(_previewViewportCamera, _sceneSO);
        }

        if (_selectionDirty && _previewViewportCamera == _viewportCamera) // only for default camera
        {
            EditorUtils::RenderWindowData viewportData =
                static_cast<WidgetViewport*>(&*_settings.WViewport)->GetRenderWindowData();
            _selection->Render(_previewViewportCamera, viewportData);
        }

        if (_physicsDirty && _previewViewportCamera == _viewportCamera) // only for default camera
        {
            gPhysics().DrawDebug(_previewViewportCamera->_getCamera(), _previewViewportCamera->GetViewport()->GetTarget());
        }

        _hudDirty = false;
        _selectionDirty = false;
        _physicsDirty = false;
    }

    void Editor::NeedsRedraw()
    {
        if (!_settings.WViewport)
            return;

        static_cast<WidgetViewport*>(&*_settings.WViewport)->NeedsRedraw();
        MakePickingDirty();
        MakeSelectionDirty();
        MakeHudDirty();
        MakePhysicsDirty();
    }

    void Editor::NeedsPicking(UINT32 x, UINT32 y)
    {
        if (!_settings.WViewport || _previewViewportCamera->GetRenderSettings()->OverlayOnly)
            return;

        if (!ImGuizmo::IsUsing())
        {
            if (_pickingDirty)
            {
                EditorUtils::RenderWindowData viewportData =
                    static_cast<WidgetViewport*>(&*_settings.WViewport)->GetRenderWindowData();
                Picking::RenderParam pickingData(viewportData.Width, viewportData.Height);

                _picking->ComputePicking(_previewViewportCamera, pickingData, _sceneSO);
                _pickingDirty = false;
            }

            SPtr<GameObject> gameObject = _picking->GetGameObjectAt(x, y);
            if (gameObject)
            {
                bool selectableSceneObject = (_selections.ClickedSceneObject && _selections.ClickedSceneObject->HasComponent(ComponentsWhichNeedGuizmo));

                if (!ImGuizmo::IsOver() || (!_selections.ClickedComponent && !selectableSceneObject))
                {
                    SPtr<Component> component = std::static_pointer_cast<Component>(gameObject);

                    _selections.ClickedComponent = component;
                    _selections.ClickedSceneObject = component->GetSceneObject().GetInternalPtr();

                    if (_settings.WProject)
                        std::static_pointer_cast<WidgetProject>(_settings.WProject)->ForceExpandToSelection();
                }
            }
            else
            {
                if (!_selections.ClickedComponent ||
                    _selections.ClickedComponent->GetCoreType() == TID_CRenderable ||
                    _selections.ClickedComponent->GetCoreType() == TID_CCamera ||
                    _selections.ClickedComponent->GetCoreType() == TID_CLight ||
                    _selections.ClickedComponent->GetCoreType() == TID_CAudioListener ||
                    _selections.ClickedComponent->GetCoreType() == TID_CRigidBody ||
                    _selections.ClickedComponent->GetCoreType() == TID_CSoftBody ||
                    _selections.ClickedComponent->GetCoreType() == TID_CBoxCollider ||
                    _selections.ClickedComponent->GetCoreType() == TID_CCapsuleCollider ||
                    _selections.ClickedComponent->GetCoreType() == TID_CConeCollider ||
                    _selections.ClickedComponent->GetCoreType() == TID_CMeshCollider ||
                    _selections.ClickedComponent->GetCoreType() == TID_CPlaneCollider ||
                    _selections.ClickedComponent->GetCoreType() == TID_CSphereCollider ||
                    _selections.ClickedComponent->GetCoreType() == TID_CHeightFieldCollider ||
                    _selections.ClickedComponent->GetCoreType() == TID_CConeTwistJoint ||
                    _selections.ClickedComponent->GetCoreType() == TID_CD6Joint ||
                    _selections.ClickedComponent->GetCoreType() == TID_CHingeJoint ||
                    _selections.ClickedComponent->GetCoreType() == TID_CSliderJoint ||
                    _selections.ClickedComponent->GetCoreType() == TID_CSphericalJoint ||
                    _selections.ClickedComponent->GetCoreType() == TID_CAudioSource)
                {
                    if (!ImGuizmo::IsOver())
                    {
                        _selections.ClickedComponent = nullptr;
                        _selections.ClickedSceneObject = nullptr;
                    }
                }
            }
        }

        NeedsRedraw();
    }

    void Editor::MakePickingDirty()
    {
        _pickingDirty = true;
    }

    void Editor::MakeHudDirty()
    {
        _hudDirty = true;
    }

    void Editor::MakeSelectionDirty()
    {
        _selectionDirty = true;
    }

    void Editor::MakePhysicsDirty()
    {
        _physicsDirty = true;
    }

    void Editor::InitializeInput()
    {
        auto inputConfig = gVirtualInput().GetConfiguration();

        inputConfig->RegisterButton(WidgetMenuBar::NEW_BINDING, TE_N, ButtonModifier::Ctrl);
        inputConfig->RegisterButton(WidgetMenuBar::OPEN_BINDING, TE_O, ButtonModifier::Ctrl);
        inputConfig->RegisterButton(WidgetMenuBar::SAVE_BINDING, TE_S, ButtonModifier::Ctrl);
        inputConfig->RegisterButton(WidgetMenuBar::SAVE_AS_BINDING, TE_S, ButtonModifier::ShiftCtrl);
        inputConfig->RegisterButton(WidgetMenuBar::QUIT_BINDING, TE_A, ButtonModifier::Ctrl);
        inputConfig->RegisterButton(WidgetMenuBar::LOAD_RESOURCE_BINDING, TE_R, ButtonModifier::Ctrl);

        inputConfig->RegisterButton(EditorUtils::DELETE_BINDING, TE_DELETE);
        inputConfig->RegisterButton(EditorUtils::COPY_BINDING, TE_C, ButtonModifier::Ctrl);
        inputConfig->RegisterButton(EditorUtils::PASTE_BINDING, TE_V, ButtonModifier::Ctrl);
        
        inputConfig->RegisterButton(CCameraUI::ROTATE_BINDING, TE_MOUSE_RIGHT);
        inputConfig->RegisterButton(CCameraUI::MOVE_BINDING, TE_LSHIFT);
        inputConfig->RegisterButton(CCameraUI::ZOOM_BINDING, TE_LCONTROL);

        inputConfig->RegisterAxis(CCameraUI::HORIZONTAL_AXIS_BINDING, VIRTUAL_AXIS_DESC((UINT32)InputAxis::MouseX));
        inputConfig->RegisterAxis(CCameraUI::VERTICAL_AXIS_BINDING, VIRTUAL_AXIS_DESC((UINT32)InputAxis::MouseY));
        inputConfig->RegisterAxis(CCameraUI::SCROLL_AXIS_BINDING, VIRTUAL_AXIS_DESC((UINT32)InputAxis::MouseZ));

        inputConfig->RegisterButton(WidgetViewport::RETARGET_BINDING, TE_DECIMAL);
        inputConfig->RegisterButton(WidgetViewport::PICKING_BINDING, TE_MOUSE_LEFT);
    }

    void Editor::InitializeScene()
    {
        _sceneSO = SceneObject::Create("Scene");
        LoadScene();
    }

    void Editor::InitializeUICamera()
    {
        _uiCameraSO = SceneObject::Create("UICamera");
        _uiCamera = _uiCameraSO->AddComponent<CCamera>();
        _uiCamera->GetViewport()->SetClearColorValue(Color(0.2f, 0.2f, 0.2f, 1.0f));
        _uiCamera->GetViewport()->SetTarget(gCoreApplication().GetWindow());
        _uiCamera->SetMain(true);
        _uiCamera->SetPriority(1);
        _uiCamera->Initialize();

        _uiCamera->SetNearClipDistance(5);
        _uiCamera->SetFarClipDistance(10000);
        _uiCamera->SetLayers(0);

        SPtr<RenderSettings> settings = _uiCamera->GetRenderSettings();
        settings->OverlayOnly = true;

        gSceneManager().SetMainRenderTarget(gCoreApplication().GetWindow());
        gCoreApplication().GetState().SetFlag(ApplicationState::Mode::Game, false);
        gCoreApplication().GetState().SetFlag(ApplicationState::Mode::Physics, false);
        gCoreApplication().GetState().SetFlag(ApplicationState::Mode::Scripting, false);
        gCoreApplication().GetState().SetFlag(ApplicationState::Mode::Animation, false);
    }

    void Editor::InitializeViewportCamera()
    {
        _viewportSO = SceneObject::Create("UIViewport");

        _viewportCameraSO = SceneObject::Create("UICamera");
        _viewportCameraSO->SetParent(_viewportSO);

        _viewportCamera = _viewportCameraSO->AddComponent<CCamera>();
        _viewportCameraUI = _viewportCameraSO->AddComponent<CCameraUI>();
        _viewportCamera->GetViewport()->SetClearColorValue(Color(0.42f, 0.67f, 0.94f, 1.0f));
        _viewportCamera->Initialize();
        _viewportCamera->SetMSAACount(gCoreApplication().GetWindow()->GetDesc().MultisampleCount);
        _viewportCamera->SetProjectionType(ProjectionType::PT_PERSPECTIVE);
        _viewportCamera->SetName("Viewport camera");

        _viewportCameraSO->SetPosition(Vector3(3.5f, 2.5f, 4.0f));
        _viewportCameraSO->LookAt(Vector3(0.0f, 0.75f, 0.0f));

        _viewportCameraUI->SetTarget(Vector3(0.0f, 0.75f, 0.0f));

        auto settings = _viewportCamera->GetRenderSettings();
        settings->ExposureScale = 0.9f;
        settings->Gamma = 1.0f;
        settings->Contrast = 2.0f;
        settings->Brightness = 0.0f;
        settings->MotionBlur.Enabled = false;

        _previewViewportCamera = _viewportCamera.GetNewHandleFromExisting();
    }

    void Editor::InitializeGui()
    {
        //Context already created in Gui plugin, we need to have the same pointed value in our local GimGui
        GImGui = ImGui::GetCurrentContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        io.ConfigWindowsResizeFromEdges = true;
        io.ConfigViewportsNoTaskBarIcon = true;
        ApplyStyleGui();

        _widgets.emplace_back(te_shared_ptr_new<WidgetMenuBar>()); _settings.WMenuBar = _widgets.back();
        _widgets.emplace_back(te_shared_ptr_new<WidgetToolBar>()); _settings.WToolbar = _widgets.back();
        _widgets.emplace_back(te_shared_ptr_new<WidgetProject>()); _settings.WProject = _widgets.back();
        _widgets.emplace_back(te_shared_ptr_new<WidgetProperties>()); _settings.WProperties = _widgets.back();
        _widgets.emplace_back(te_shared_ptr_new<WidgetProfiler>()); _settings.WProfiler = _widgets.back();
        _widgets.emplace_back(te_shared_ptr_new<WidgetSettings>()); _settings.WSettings = _widgets.back();
        _widgets.emplace_back(te_shared_ptr_new<WidgetRenderOptions>()); _settings.WRenderOptions = _widgets.back();
        _widgets.emplace_back(te_shared_ptr_new<WidgetConsole>()); _settings.WConsole = _widgets.back();
        _widgets.emplace_back(te_shared_ptr_new<WidgetScript>()); _settings.WScript = _widgets.back();
        _widgets.emplace_back(te_shared_ptr_new<WidgetMaterials>()); _settings.WMaterial = _widgets.back();
        _widgets.emplace_back(te_shared_ptr_new<WidgetResources>()); _settings.WResources = _widgets.back();
        _widgets.emplace_back(te_shared_ptr_new<WidgetViewport>()); _settings.WViewport = _widgets.back();

        for (auto widget : _widgets)
            widget->Initialize();
    }

    void Editor::ApplyStyleGui() const
    {
        {
            // Color settings    
            const auto color_text = ImVec4(0.810f, 0.810f, 0.810f, 1.000f);
            const auto color_text_disabled = ImVec4(color_text.x, color_text.y, color_text.z, 0.5f);
            const auto color_interactive = ImVec4(0.338f, 0.338f, 0.338f, 1.000f);
            const auto color_interactive_hovered = ImVec4(0.450f, 0.450f, 0.450f, 1.000f);
            const auto color_interactive_clicked = ImVec4(0.586f, 0.586f, 0.586f, 1.000f);
            const auto color_background = ImVec4(50.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1.0f);
            const auto color_background_content = ImVec4(35.0f / 255.0f, 35.0f / 255.0f, 35.0f / 255.0f, 1.0f);
            const auto color_shadow = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);

            // Use default dark style as a base
            ImGui::StyleColorsDark();

            // Colors
            ImVec4* colors = ImGui::GetStyle().Colors;
            colors[ImGuiCol_Text] = color_text;
            colors[ImGuiCol_TextDisabled] = color_text_disabled;
            colors[ImGuiCol_WindowBg] = color_background;            // Background of normal windows
            colors[ImGuiCol_ChildBg] = color_background;             // Background of child windows
            colors[ImGuiCol_PopupBg] = color_background;             // Background of popups, menus, tooltips windows
            colors[ImGuiCol_Border] = color_interactive;
            colors[ImGuiCol_BorderShadow] = color_shadow;
            colors[ImGuiCol_FrameBg] = color_background_content;     // Background of checkbox, radio button, plot, slider, text input
            colors[ImGuiCol_FrameBgHovered] = color_interactive;
            colors[ImGuiCol_FrameBgActive] = color_interactive_clicked;
            colors[ImGuiCol_TitleBg] = color_background_content;
            colors[ImGuiCol_TitleBgActive] = color_interactive;
            colors[ImGuiCol_TitleBgCollapsed] = color_background;
            colors[ImGuiCol_MenuBarBg] = color_background_content;
            colors[ImGuiCol_ScrollbarBg] = color_background_content;
            colors[ImGuiCol_ScrollbarGrab] = color_interactive;
            colors[ImGuiCol_ScrollbarGrabHovered] = color_interactive_hovered;
            colors[ImGuiCol_ScrollbarGrabActive] = color_interactive_clicked;
            colors[ImGuiCol_CheckMark] = color_text;
            colors[ImGuiCol_SliderGrab] = color_interactive;
            colors[ImGuiCol_SliderGrabActive] = color_interactive_clicked;
            colors[ImGuiCol_Button] = color_interactive;
            colors[ImGuiCol_ButtonHovered] = color_interactive_hovered;
            colors[ImGuiCol_ButtonActive] = color_interactive_clicked;
            colors[ImGuiCol_Header] = color_interactive;            // Header* colors are used for CollapsingHeader, TreeNode, Selectable, MenuItem
            colors[ImGuiCol_HeaderHovered] = color_interactive_hovered;
            colors[ImGuiCol_HeaderActive] = color_interactive_clicked;
            colors[ImGuiCol_Separator] = color_interactive;
            colors[ImGuiCol_SeparatorHovered] = color_interactive_hovered;
            colors[ImGuiCol_SeparatorActive] = color_interactive_clicked;
            colors[ImGuiCol_ResizeGrip] = color_interactive;
            colors[ImGuiCol_ResizeGripHovered] = color_interactive_hovered;
            colors[ImGuiCol_ResizeGripActive] = color_interactive_clicked;
            colors[ImGuiCol_Tab] = color_interactive;
            colors[ImGuiCol_TabHovered] = color_interactive_hovered;
            colors[ImGuiCol_TabActive] = color_interactive_clicked;
            colors[ImGuiCol_TabUnfocused] = color_interactive;
            colors[ImGuiCol_TabUnfocusedActive] = color_interactive;            // Might be called active, but it's active only because it's it's the only tab available, the user didn't really activate it
            colors[ImGuiCol_DockingPreview] = color_interactive_clicked;    // Preview overlay color when about to docking something
            colors[ImGuiCol_DockingEmptyBg] = color_interactive;            // Background color for empty node (e.g. CentralNode with no window docked into it)
            colors[ImGuiCol_PlotLines] = color_interactive;
            colors[ImGuiCol_PlotLinesHovered] = color_interactive_hovered;
            colors[ImGuiCol_PlotHistogram] = color_interactive;
            colors[ImGuiCol_PlotHistogramHovered] = color_interactive_hovered;
            colors[ImGuiCol_TextSelectedBg] = color_background;
            colors[ImGuiCol_DragDropTarget] = color_interactive_hovered;    // Color when hovering over target
            colors[ImGuiCol_NavHighlight] = color_background;             // Gamepad/keyboard: current highlighted item
            colors[ImGuiCol_NavWindowingHighlight] = color_background;             // Highlight window when using CTRL+TAB
            colors[ImGuiCol_NavWindowingDimBg] = color_background;             // Darken/colorize entire screen behind the CTRL+TAB window list, when active
            colors[ImGuiCol_ModalWindowDimBg] = color_background;             // Darken/colorize entire screen behind a modal window, when one is active

            // Spatial settings
            const auto fontSize = 14.0f;
            const auto roundness = 0.0f;

            // Spatial
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowBorderSize = 1.0f;
            style.FrameBorderSize = 0.0f;
            style.ScrollbarSize = 16.0f;
            style.FramePadding = ImVec2(6, 6);
            style.ItemSpacing = ImVec2(6, 6);
            style.WindowMenuButtonPosition = ImGuiDir_Right;
            style.WindowRounding = roundness;
            style.FrameRounding = roundness;
            style.PopupRounding = roundness;
            style.GrabRounding = roundness;
            style.ScrollbarRounding = roundness;
            style.Alpha = 1.0f;

            // Font
            auto& io = ImGui::GetIO();
            io.Fonts->AddFontFromFileTTF("Data/Fonts/CalibriBold.ttf", fontSize);
            //io.FontGlobalScale = fontScale;

            ImFontConfig config;
            config.MergeMode = true;
            static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
            io.Fonts->AddFontFromFileTTF("Data/Fonts/Fa-solid-900.ttf", fontSize, &config, icon_ranges);
        }
    }

    void Editor::BeginGui()
    {
        // Set window flags
        const auto window_flags =
            ImGuiWindowFlags_MenuBar |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoSavedSettings;

        // Set window position and size
        float offsetY = 0;
        offsetY += _settings.WMenuBar ? _settings.WMenuBar->GetHeight() : 0;
        offsetY += _settings.WToolbar ? _settings.WToolbar->GetHeight() : 0;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + offsetY));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - offsetY));
        ImGui::SetNextWindowViewport(viewport->ID);

        // Set window style
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.0f);

        // Begin window
        bool open = true;
        _editorBegun = ImGui::Begin(_settings.EditorName, &open, window_flags);
        ImGui::PopStyleVar(3);

        // Begin dock space
        if (_editorBegun)
        {
            // Dock space
            const auto windowId = ImGui::GetID(_settings.EditorName);
            if (!ImGui::DockBuilderGetNode(windowId))
            {
                // Reset current docking state
                ImGui::DockBuilderRemoveNode(windowId);
                ImGui::DockBuilderAddNode(windowId, ImGuiDockNodeFlags_None);
                ImGui::DockBuilderSetNodeSize(windowId, ImGui::GetMainViewport()->Size);

                ImGuiID dockMainId = windowId;

                ImGuiID dockLeftId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 0.20f, nullptr, &dockMainId);
                ImGuiID dockRightId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.25f, nullptr, &dockMainId);
                ImGuiID dockBottomId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.305f, nullptr, &dockMainId);

                const ImGuiID dockLeftBottomId = ImGui::DockBuilderSplitNode(dockLeftId, ImGuiDir_Down, 0.6f, nullptr, &dockLeftId);
                const ImGuiID dockRightBottomId = ImGui::DockBuilderSplitNode(dockRightId, ImGuiDir_Down, 0.6f, nullptr, &dockRightId);

                // Dock windows
                ImGui::DockBuilderDockWindow(PROJECT_TITLE, dockLeftId);
                ImGui::DockBuilderDockWindow(PROFILER_TITLE, dockRightId);
                ImGui::DockBuilderDockWindow(SETTINGS_TITLE, dockRightId);
                ImGui::DockBuilderDockWindow(RENDER_OPTIONS_TITLE, dockRightId);
                ImGui::DockBuilderDockWindow(CONSOLE_TITLE, dockBottomId);
                ImGui::DockBuilderDockWindow(RESOURCES_TITLE, dockBottomId);
                ImGui::DockBuilderDockWindow(VIEWPORT_TITLE, dockMainId);
                ImGui::DockBuilderDockWindow(SCRIPT_TITLE, dockMainId);
                ImGui::DockBuilderDockWindow(MATERIALS_TITLE, dockRightBottomId);
                ImGui::DockBuilderDockWindow(PROPERTIES_TITLE, dockLeftBottomId);
                ImGui::DockBuilderFinish(dockMainId);
            }

            ImGui::DockSpace(windowId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
        }
    }

    void Editor::EndGui()
    {
        if (_editorBegun)
            ImGui::End();
    }

    Widget* Editor::GetWidget(Widget::WidgetType type)
    {
        for (auto& widget : _widgets)
        {
            if (widget->GetType() == type)
                return widget.get();
        }

        return nullptr;
    }

    void Editor::PutFocus(WindowType type) const
    {
        switch (type)
        {
        case WindowType::Project:
            _settings.WProject->PutFocus();
            break;
        case WindowType::RenderOptions:
            _settings.WRenderOptions->PutFocus();
            break;
        case WindowType::Console:
            _settings.WConsole->PutFocus();
            break;
        case WindowType::Resources:
            _settings.WResources->PutFocus();
            break;
        case WindowType::Viewport:
            _settings.WViewport->PutFocus();
            break;
        case WindowType::Script:
            _settings.WScript->PutFocus();
            break;
        case WindowType::Materials:
            _settings.WMaterials->PutFocus();
            break;
        case WindowType::Properties:
            _settings.WProperties->PutFocus();
            break;
        case WindowType::Profiler:
            _settings.WProfiler->PutFocus();
            break;
        }
    }

    void GetComponentsFromTransform(const Transform& transform, float* matrixTranslation, float* matrixRotation, float* matrixScale)
    {
        Radian x, y, z;
        transform.GetRotation().ToEulerAngles(x, y, z);
        Vector3 position = transform.GetPosition();
        Vector3 rotation(x.ValueDegrees(), y.ValueDegrees(), z.ValueDegrees());
        Vector3 scale = transform.GetScale();

        matrixTranslation[0] = position.x;
        matrixTranslation[1] = position.y;
        matrixTranslation[2] = position.z;

        matrixRotation[0] = rotation.x;
        matrixRotation[1] = rotation.y;
        matrixRotation[2] = rotation.z;

        matrixScale[0] = scale.x;
        matrixScale[1] = scale.y;
        matrixScale[2] = scale.z;
    }

    void Editor::HandleImGuizmo()
    {
        bool somethingSelected = false;

        if (_selections.ClickedComponent)
        {
            somethingSelected = true;
        }
        else if(_selections.ClickedSceneObject)
        {
            if (_selections.ClickedSceneObject->HasComponent(ComponentsWhichNeedGuizmo))
                somethingSelected = true;
        }

        if (somethingSelected && _guizmoState == ImGuizmoState::Active && _previewViewportCamera == _viewportCamera)
        {
            Transform transform;
            const float* proj = nullptr;
            const float* view = nullptr;
            float matrixTranslation[3];
            float matrixRotation[3];
            float matrixScale[3];
            float worldMatrix[4][4];
            float deltaWorldMatrix[4][4];
            float snap[3] = { 0.1f, 0.1f, 0.1f };

            ImGuizmo::SetDrawlist();

            // Different snap for rotation
            if (_guizmoOperation == ImGuizmo::OPERATION::ROTATE)
                snap[0] = 2.5f;

            // Retrieves View and Projection Matrix
            const Matrix4& viewMatrix = _previewViewportCamera->GetViewMatrix().Transpose();
            const Matrix4& projectionMatrix = _previewViewportCamera->GetProjectionMatrixRS().Transpose();
            viewMatrix.GetAsFloat(view);
            projectionMatrix.GetAsFloat(proj);

            // Retrieves WorldMatrix
            if (_selections.ClickedSceneObject)
                transform = _selections.ClickedSceneObject->GetTransform();
            else if (_selections.ClickedComponent)
                transform = _selections.ClickedComponent->SO()->GetTransform();
            else
                return;

            GetComponentsFromTransform(transform, matrixTranslation, matrixRotation, matrixScale);
            ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, &worldMatrix[0][0]);

            // Guizmo rendering
            ImGuizmo::Manipulate(view, proj, _guizmoOperation, _guizmoMode, &worldMatrix[0][0], &deltaWorldMatrix[0][0], &snap[0]);

            // Transform update
            if (ImGuizmo::IsUsing())
            {
                ImGuizmo::DecomposeMatrixToComponents(&worldMatrix[0][0], matrixTranslation, matrixRotation, matrixScale);

                switch (_guizmoOperation)
                {
                case ImGuizmo::OPERATION::TRANSLATE:
                    ImGuizmo::DecomposeMatrixToComponents(&deltaWorldMatrix[0][0], matrixTranslation, matrixRotation, matrixScale);
                    _selections.ClickedSceneObject->Move(Vector3(matrixTranslation[0], matrixTranslation[1], matrixTranslation[2]));
                    break;

                case ImGuizmo::OPERATION::SCALE:
                    _selections.ClickedSceneObject->SetScale(Vector3(matrixScale[0], matrixScale[1], matrixScale[2]));
                    break;

                case ImGuizmo::OPERATION::ROTATE:
                {
                    Quaternion rotation;
                    ImGuizmo::DecomposeMatrixToComponents(&deltaWorldMatrix[0][0], matrixTranslation, matrixRotation, matrixScale);
                    rotation.FromEulerAngles(Radian(Degree(matrixRotation[0])), Radian(Degree(matrixRotation[1])), Radian(Degree(matrixRotation[2])));
                    _selections.ClickedSceneObject->Rotate(rotation);
                }
                break;

                default:
                    break;
                }

                NeedsRedraw();
            }
        }
    }

    void Editor::SetImGuizmoRect(const Vector2& position, const Vector2& size)
    {
        ImGuizmo::SetRect(position.x, position.y, size.x, size.y);
    }

    void Editor::SetImGuizmoState(ImGuizmoState state) 
    { 
        if (state == _guizmoState)
            return;

        _guizmoState = state;

        if (_guizmoState == ImGuizmoState::Active)
            ImGuizmo::Enable(true);
        else
            ImGuizmo::Enable(false);
    }

    void Editor::SetImGuizmoOperation(ImGuizmo::OPERATION operation)
    {
        _guizmoOperation = operation;
    }

    void Editor::SetImGuizmoMode(ImGuizmo::MODE mode)
    {
        _guizmoMode = mode;
    }

    void Editor::Save()
    {
        _settings.State = EditorState::Saved;
        // TODO Save
    }

    void Editor::Open()
    {
        _settings.State = EditorState::Saved;
        // TODO Open
    }

    void Editor::Paste()
    {
        if (!_selections.CopiedSceneObject && !_selections.CopiedComponent)
            return;

        SPtr<SceneObject> clickedSceneObject = _selections.ClickedSceneObject ?
            _selections.ClickedSceneObject : GetSceneRoot().GetInternalPtr();

        if (_selections.CopiedComponent)
        {
            UINT32 type = _selections.CopiedComponent->GetComponentType();
            if (!clickedSceneObject->GetComponent(type).Empty())
                return;

            switch (_selections.CopiedComponent->GetCoreType())
            {
                case TID_CCamera:
                {
                    HCamera component = clickedSceneObject->AddComponent<CCamera>();
                    component->Clone(_selections.CopiedComponent->GetHandle());
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CCameraFlyer:
                {
                    HCameraFlyer component = clickedSceneObject->AddComponent<CCameraFlyer>();
                    component->Clone(_selections.CopiedComponent->GetHandle());
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CCameraUI:
                {
                    HCameraUI component = clickedSceneObject->AddComponent<CCameraUI>();
                    component->Clone(_selections.CopiedComponent->GetHandle());
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CLight:
                {
                    SPtr<CLight> previousLight = std::static_pointer_cast<CLight>(_selections.CopiedComponent);
                    HLight component = clickedSceneObject->AddComponent<CLight>(previousLight->GetType());
                    component->Clone(_selections.CopiedComponent->GetHandle());
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CRenderable:
                {
                    HRenderable component = clickedSceneObject->AddComponent<CRenderable>();
                    component->Clone(_selections.CopiedComponent->GetHandle());
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CScript:
                {
                    HScript component = clickedSceneObject->AddComponent<CScript>();
                    component->Clone(_selections.CopiedComponent->GetHandle());
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CSkybox:
                {
                    if (SceneManager::Instance().FindComponents<CSkybox>().size() > 0)
                        break;

                    HSkybox component = clickedSceneObject->AddComponent<CSkybox>();
                    component->Clone(_selections.CopiedComponent->GetHandle());
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CAnimation:
                {
                    HAnimation component = clickedSceneObject->AddComponent<CAnimation>();
                    component->Clone(_selections.CopiedComponent->GetHandle());
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CBone:
                {
                    HBone component = clickedSceneObject->AddComponent<CBone>();
                    component->Clone(_selections.CopiedComponent->GetHandle());
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CAudioListener:
                {
                    HAudioListener component = clickedSceneObject->AddComponent<CAudioListener>();
                    component->Clone(_selections.CopiedComponent->GetHandle());
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CAudioSource:
                {
                    HAudioSource component = clickedSceneObject->AddComponent<CAudioSource>();
                    component->Clone(_selections.CopiedComponent->GetHandle());
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CRigidBody:
                {
                    HRigidBody component = clickedSceneObject->AddComponent<CRigidBody>();
                    component->Clone(_selections.CopiedComponent->GetHandle());
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CSoftBody:
                {
                    HSoftBody component = clickedSceneObject->AddComponent<CSoftBody>();
                    component->Clone(_selections.CopiedComponent->GetHandle());
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CConeTwistJoint:
                {
                    HConeTwistJoint component = clickedSceneObject->AddComponent<CConeTwistJoint>();
                    component->Clone(static_object_cast<CConeTwistJoint>(_selections.CopiedComponent->GetHandle()));
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CD6Joint:
                {
                    HD6Joint component = clickedSceneObject->AddComponent<CD6Joint>();
                    component->Clone(static_object_cast<CD6Joint>(_selections.CopiedComponent->GetHandle()));
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CHingeJoint:
                {
                    HHingeJoint component = clickedSceneObject->AddComponent<CHingeJoint>();
                    component->Clone(static_object_cast<CHingeJoint>(_selections.CopiedComponent->GetHandle()));
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CSliderJoint:
                {
                    HSliderJoint component = clickedSceneObject->AddComponent<CSliderJoint>();
                    component->Clone(static_object_cast<CSliderJoint>(_selections.CopiedComponent->GetHandle()));
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CSphericalJoint:
                {
                    HSphericalJoint component = clickedSceneObject->AddComponent<CSphericalJoint>();
                    component->Clone(static_object_cast<CSphericalJoint>(_selections.CopiedComponent->GetHandle()));
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CBoxCollider:
                {
                    HBoxCollider component = clickedSceneObject->AddComponent<CBoxCollider>();
                    component->Clone(static_object_cast<CBoxCollider>(_selections.CopiedComponent->GetHandle()));
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CPlaneCollider:
                {
                    HPlaneCollider component = clickedSceneObject->AddComponent<CPlaneCollider>();
                    component->Clone(static_object_cast<CPlaneCollider>(_selections.CopiedComponent->GetHandle()));
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CSphereCollider:
                {
                    HSphereCollider component = clickedSceneObject->AddComponent<CSphereCollider>();
                    component->Clone(static_object_cast<CSphereCollider>(_selections.CopiedComponent->GetHandle()));
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CCylinderCollider:
                {
                    HCylinderCollider component = clickedSceneObject->AddComponent<CCylinderCollider>();
                    component->Clone(static_object_cast<CCylinderCollider>(_selections.CopiedComponent->GetHandle()));
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CCapsuleCollider:
                {
                    HCapsuleCollider component = clickedSceneObject->AddComponent<CCapsuleCollider>();
                    component->Clone(static_object_cast<CCapsuleCollider>(_selections.CopiedComponent->GetHandle()));
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CMeshCollider:
                {
                    HMeshCollider component = clickedSceneObject->AddComponent<CMeshCollider>();
                    component->Clone(static_object_cast<CMeshCollider>(_selections.CopiedComponent->GetHandle()));
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CConeCollider:
                {
                    HConeCollider component = clickedSceneObject->AddComponent<CConeCollider>();
                    component->Clone(static_object_cast<CConeCollider>(_selections.CopiedComponent->GetHandle()));
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                case TID_CHeightFieldCollider:
                {
                    HHeightFieldCollider component = clickedSceneObject->AddComponent<CHeightFieldCollider>();
                    component->Clone(static_object_cast<CHeightFieldCollider>(_selections.CopiedComponent->GetHandle()));
                    component->Initialize();
                    _selections.ClickedComponent = component.GetInternalPtr();
                    _selections.CopiedComponent = component.GetInternalPtr();
                }
                break;

                default:
                break;
            }
        }
        else if (_selections.CopiedSceneObject)
        {
            if (_selections.CopiedSceneObject == GetSceneRoot().GetInternalPtr())
                return;

            HSceneObject sceneObject = SceneObject::Create("SceneObject");
            sceneObject->Clone(_selections.CopiedSceneObject);
            sceneObject->SetParent(clickedSceneObject->GetHandle());

            _selections.ClickedSceneObject = sceneObject.GetInternalPtr();
            _selections.CopiedSceneObject = sceneObject.GetInternalPtr();
        }

        NeedsRedraw();
        GetSettings().State = Editor::EditorState::Modified;
    }

    void Editor::Delete()
    {
        if (!_selections.ClickedSceneObject && !_selections.ClickedComponent)
            return;

        if (_selections.ClickedComponent)
        {
            if (_selections.CopiedComponent == _selections.ClickedComponent)
                _selections.CopiedComponent = nullptr;

            if (_selections.HoveredComponent == _selections.ClickedComponent)
                _selections.HoveredComponent = nullptr;

            _selections.ClickedComponent->GetSceneObject()->DestroyComponent(_selections.ClickedComponent.get(), true);
            _selections.ClickedComponent = nullptr;
        }
        else if (_selections.ClickedSceneObject)
        {
            if (_selections.ClickedSceneObject == GetSceneRoot().GetInternalPtr())
                return;

            _selections.CopiedComponent = nullptr;
            _selections.HoveredComponent = nullptr;

            if (_selections.CopiedSceneObject == _selections.ClickedSceneObject)
                _selections.CopiedSceneObject = nullptr;

            if (_selections.HoveredSceneObject == _selections.ClickedSceneObject)
                _selections.HoveredSceneObject = nullptr;

            _selections.ClickedSceneObject->Destroy(true);
            _selections.ClickedSceneObject = nullptr;
        }

        NeedsRedraw();
        GetSettings().State = Editor::EditorState::Modified;
    }

    void Editor::LoadScene()
    {
#if TE_PLATFORM == TE_PLATFORM_WIN32
        // ######################################################
        auto meshAnimImportOptions = MeshImportOptions::Create();
        meshAnimImportOptions->ImportNormals = true;
        meshAnimImportOptions->ImportTangents = true;
        meshAnimImportOptions->CpuCached = false;
        meshAnimImportOptions->ImportSkin = true;
        meshAnimImportOptions->ImportBlendShapes = false;
        meshAnimImportOptions->ImportAnimations = true;
        meshAnimImportOptions->ImportRootMotion = true;

        auto meshImportOptions = MeshImportOptions::Create();
        meshImportOptions->ImportNormals = true;
        meshImportOptions->ImportTangents = true;
        meshImportOptions->CpuCached = false;

        //meshImportOptions->ScaleSystemUnit = true;
        //meshImportOptions->ScaleFactor = 0.01f;;

        auto textureImportOptions = TextureImportOptions::Create();
        textureImportOptions->CpuCached = false;
        textureImportOptions->GenerateMips = true;
        textureImportOptions->Format = Util::IsBigEndian() ? PF_RGBA8 : PF_BGRA8;

        auto textureCubeMapImportOptions = TextureImportOptions::Create();
        textureCubeMapImportOptions->CpuCached = false;
        textureCubeMapImportOptions->CubemapType = CubemapSourceType::Faces;
        textureCubeMapImportOptions->IsCubemap = true;
        textureCubeMapImportOptions->Format = Util::IsBigEndian() ? PF_RGBA8 : PF_BGRA8;

        auto audioClipImportOptions = AudioClipImportOptions::Create();

        // ######################################################

        // ######################################################
        auto knightResources = EditorResManager::Instance().LoadAll("Data/Meshes/Knight/Knight.dae", meshAnimImportOptions);

        _loadedMeshKnight = static_resource_cast<Mesh>(knightResources->Entries[0].Res);
        _animationClipKnight = static_resource_cast<AnimationClip>(knightResources->Entries[2].Res);
        // ######################################################

        // ######################################################
        //_loadedMeshPlane = EditorResManager::Instance().Load<Mesh>("C:/Users/FBN6/Desktop/grandma/scene.gltf", meshImportOptions);
        _loadedMeshPlane = EditorResManager::Instance().Load<Mesh>("Data/Meshes/Plane/plane.obj", meshImportOptions);
        _loadedSkyboxTexture = EditorResManager::Instance().Load<Texture>("Data/Textures/Skybox/skybox_day_medium.png", textureCubeMapImportOptions);
        _loadedSkyboxIrradianceTexture = EditorResManager::Instance().Load<Texture>("Data/Textures/Skybox/skybox_day_irradiance_small.png", textureCubeMapImportOptions);
        _loadedKnightDiffuseTexture = EditorResManager::Instance().Load<Texture>("Data/Textures/Knight/diffuse-small.png", textureImportOptions);
        // ######################################################

        // ######################################################
        _loadedMeshKnight->SetName("Knight Mesh");
        _loadedMeshPlane->SetName("Plane Mesh");
        _loadedSkyboxTexture->SetName("Skybox Radiance");
        _loadedSkyboxIrradianceTexture->SetName("Skybox Irradiance");
        // ###################################################### 

        // ######################################################
        HShader _shader = gBuiltinResources().GetBuiltinShader(BuiltinShader::Opaque);

        MaterialProperties properties;

        _planeMaterial = Material::Create(_shader);
        _planeMaterial->SetName("Plane Material");
        _planeMaterial->SetSamplerState("AnisotropicSampler", gBuiltinResources().GetBuiltinSampler(BuiltinSampler::Anisotropic));
        _planeMaterial->SetProperties(properties);

        properties.UseDiffuseMap = true;

        _knightMaterial = Material::Create(_shader);
        _knightMaterial->SetName("Knight Material");
        _knightMaterial->SetTexture("DiffuseMap", _loadedKnightDiffuseTexture);
        _knightMaterial->SetSamplerState("AnisotropicSampler", gBuiltinResources().GetBuiltinSampler(BuiltinSampler::Anisotropic));
        _knightMaterial->SetProperties(properties);
        // ######################################################

        // ######################################################
        _sceneSkyboxSO = SceneObject::Create("Skybox");
        _sceneSkyboxSO->SetParent(_sceneSO);
        _skybox = _sceneSkyboxSO->AddComponent<CSkybox>();
        _skybox->SetTexture(_loadedSkyboxTexture);
        _skybox->SetIrradiance(_loadedSkyboxIrradianceTexture);
        _skybox->Initialize();
        // ######################################################

        // ######################################################
        _sceneLightSO = SceneObject::Create("Light");
        _sceneLightSO->SetParent(_sceneSO);
        _light = _sceneLightSO->AddComponent<CLight>(LightType::Directional);
        _light->Initialize();
        _sceneLightSO->Rotate(Vector3(1.0f, 0.0f, 0.0f), -Radian(Math::HALF_PI / 2.0f));
        _sceneLightSO->Move(Vector3(0.0f, 4.0f, 4.0f));
        // ######################################################

        // ######################################################
        _sceneRenderableKnightSO = SceneObject::Create("Knight");
        _sceneRenderableKnightSO->SetParent(_sceneSO);

        _renderableKnight = _sceneRenderableKnightSO->AddComponent<CRenderable>();
        _renderableKnight->SetMesh(_loadedMeshKnight);
        _renderableKnight->SetMaterial(_knightMaterial, true);
        _renderableKnight->SetName("Knight Renderable");
        _renderableKnight->Initialize();

        _animationKnight = _sceneRenderableKnightSO->AddComponent<CAnimation>();
        _animationKnight->SetName("Knight animation");
        _animationKnight->Initialize();
        _animationKnight->SetDefaultClip(_animationClipKnight);

        _rigidBodyKnight = _sceneRenderableKnightSO->AddComponent<CRigidBody>();
        _rigidBodyKnight->SetName("Rigid Body Knight");
        _rigidBodyKnight->SetFriction(1.0f);
        _rigidBodyKnight->SetRollingFriction(1.0f);
        _rigidBodyKnight->SetCollisionReportMode(CollisionReportMode::ReportPersistent);
        _rigidBodyKnight->Initialize();
        _boxColliderKnight = _sceneRenderableKnightSO->AddComponent<CBoxCollider>();
        _boxColliderKnight->Initialize();
        _boxColliderKnight->SetCenter(Vector3(0.0f, 1.0f, 0.0f));
        _boxColliderKnight->SetScale(Vector3(1.0f, 2.0f, 1.0f));
        
        _sceneRenderablePlaneSO = SceneObject::Create("Plane");
        _sceneRenderablePlaneSO->SetParent(_sceneSO);
        _renderablePlane = _sceneRenderablePlaneSO->AddComponent<CRenderable>();
        _renderablePlane->SetMesh(_loadedMeshPlane);
        _renderablePlane->SetMaterial(_planeMaterial);
        _renderablePlane->SetName("Plane Renderable");
        _renderablePlane->Initialize();
        _sceneRenderablePlaneSO->Move(Vector3(0.0, -1.0f, 0.0f));

        _rigidBodyPlane = _sceneRenderablePlaneSO->AddComponent<CRigidBody>();
        _rigidBodyPlane->SetName("Rigid Body Plane");
        _rigidBodyPlane->SetFriction(1.0f);
        _rigidBodyPlane->SetRollingFriction(1.0f);
        _rigidBodyPlane->SetIsKinematic(true);
        _rigidBodyPlane->SetCollisionReportMode(CollisionReportMode::ReportPersistent);
        _rigidBodyPlane->Initialize();
        _planeColliderKnight = _sceneRenderablePlaneSO->AddComponent<CBoxCollider>();
        _planeColliderKnight->SetExtents(Vector3(5.0f, 0.2f, 5.0f));
        _planeColliderKnight->SetPosition(Vector3(0.0f, -0.2f, 0.0f));
        _planeColliderKnight->Initialize();
        // ######################################################

        // ######################################################
        _sceneScriptSO = SceneObject::Create("Script");
        _sceneScriptSO->SetParent(_sceneSO);
        _script = _sceneScriptSO->AddComponent<CScript>();
        _script->SetNativeScript("DefaultScript");
        _script->Initialize();
        // ######################################################

        // ######################################################
        audioClipImportOptions->Is3D = true;
        _audioClip = EditorResManager::Instance().Load<AudioClip>("Data/Sounds/AirHorn.ogg", audioClipImportOptions);

        _sceneAudioSourceSO = SceneObject::Create("Audio Source");
        _sceneAudioSourceSO->SetParent(_sceneSO);
        _sceneAudioSourceSO->Move(Vector3(5.0f, 0.0f, 0.0f));

        _sceneAudioListenerSO = SceneObject::Create("Audio Listener");
        _sceneAudioListenerSO->SetParent(_sceneSO);
        _sceneAudioListenerSO->Move(Vector3(7.0f, 0.0f, 0.0f));

        _audioSource = _sceneAudioSourceSO->AddComponent<CAudioSource>();
        _audioSource->Initialize();
        _audioSource->SetIsLooping(true);
        _audioSource->SetClip(_audioClip);
        //_audioSource->Play();

        _audioListener = _sceneAudioListenerSO->AddComponent<CAudioListener>();
        _audioListener->Initialize();
        // ######################################################

        // ######################################################
        _sceneJointSO = SceneObject::Create("Joint");
        _sceneJointSO->SetParent(_sceneSO);

        _planeKnightSphericalJoint = _sceneJointSO->AddComponent<CSphericalJoint>();
        _planeKnightSphericalJoint->SetBody(JointBody::Anchor, _rigidBodyPlane);
        _planeKnightSphericalJoint->SetBody(JointBody::Target, _rigidBodyKnight);
        _planeKnightSphericalJoint->Initialize();
        // ######################################################

        HShader opaqueShader = gBuiltinResources().GetBuiltinShader(BuiltinShader::Opaque);
        HShader transparentShader = gBuiltinResources().GetBuiltinShader(BuiltinShader::Transparent);
        HShader transparentShaderCullNone = gBuiltinResources().GetBuiltinShader(BuiltinShader::TransparentCullNone);

        EditorResManager::Instance().Add<Material>(_planeMaterial);
        EditorResManager::Instance().Add<Material>(_knightMaterial);
        EditorResManager::Instance().Add<Shader>(opaqueShader);
        EditorResManager::Instance().Add<Shader>(transparentShader);
        EditorResManager::Instance().Add<Shader>(transparentShaderCullNone);
#endif
    }

    Editor& gEditor()
    {
        return Editor::Instance();
    }
}
