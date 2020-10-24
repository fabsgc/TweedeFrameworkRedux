#pragma once

#include "TeCorePrerequisites.h"
#include "Widget/TeWidget.h"
#include "Utility/TeModule.h"
#include <vector>
#include <memory>

namespace te
{
    class Editor : public Module<Editor>
    {
    public:
        TE_MODULE_STATIC_HEADER_MEMBER(Editor)

        Editor();
        ~Editor();

        /** @copydoc Module::OnStartUp */
        void OnStartUp() override;

        /** @copydoc Module::OnShutDown */
        void OnShutDown() override;

        /** Called every frame. */
        void Update();

        /** Called to inform the editor that some element has been modified and viewport must be updated */
        void NeedsRedraw();

        /** Get viewport camera handle */
        HCamera& GetViewportCamera() { return _viewportCamera; }

        /** Get viewport camera UI handle */
        HCameraUI& GetViewportCameraUI() { return _viewportCameraUI;  }

        /** Get a widget pointer given its type. Return nullptr if not widget has been found */
        Widget* GetWidget(Widget::WidgetType type)
        {
            for (auto& widget : _widgets)
            {
                if (widget->GetType() == type)
                    return widget.get();
            }

            return nullptr;
        }

    protected:
        struct EditorSettings
        {
            SPtr<Widget> WMenuBar  = nullptr;
            SPtr<Widget> WToolbar  = nullptr;
            SPtr<Widget> WProject  = nullptr;
            SPtr<Widget> WViewport = nullptr;
            const char* EditorName = "Editor";
            bool Show = true;
        };

    protected:
        void InitializeInput();
        void InitializeScene();
        void InitializeUICamera();
        void InitializeViewportCamera();
        void InitializeGui();
        void ApplyStyleGui() const;
        void BeginGui();
        void EndGui();

    protected:
        void LoadScene();

    protected:
        std::vector<SPtr<Widget>> _widgets;
        EditorSettings _settings;
        bool _editorBegun;

        HSceneObject _viewportSO;
        HSceneObject _sceneSO;

        HCamera _viewportCamera;
        HCameraUI _viewportCameraUI;
        HSceneObject _viewportCameraSO;
        
        HCamera _uiCamera;
        HSceneObject _uiCameraSO;

#if TE_PLATFORM == TE_PLATFORM_WIN32
        // TODO Temp for debug purpose
        HMesh _loadedMeshMonkey;
        HTexture _loadedTextureMonkey;
        HTexture _loadedCubemapTexture;

        HMaterial _materialMonkey;

        HSceneObject _sceneRenderableMonkeySO;
        HSceneObject _sceneLightSO;
        HSceneObject _sceneSkyboxSO;

        HLight _light;
        HSkybox _skybox;
        HRenderable _renderableMonkey;
#endif
    };
}
