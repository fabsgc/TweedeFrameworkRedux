#include "TeHud.h"

#include "../TeEditor.h"
#include "../TeEditorUtils.h"
#include "TeHudPickingMat.h"
#include "RenderAPI/TeRenderAPI.h"
#include "RenderAPI/TeRenderTarget.h"
#include "Scene/TeSceneObject.h"
#include "Components/TeCLight.h"
#include "Components/TeCCamera.h"

namespace te
{
    Hud::Hud()
        : _material(nullptr)
    { }

    void Hud::Initialize()
    {
        _material = HudPickingMat::Get();
        SelectionUtils::CreateHudInstanceBuffer(_instanceBuffer);
    }

    void Hud::Render(const HCamera& camera, const HSceneObject& root)
    {
        RenderAPI& rapi = RenderAPI::Instance();
        UINT32 clearBuffers = FBT_DEPTH;

        Vector<SelectionUtils::PerHudInstanceData> instancedElements;
        GetHudElements(camera, root, instancedElements);

        if (instancedElements.size() > 0)
        {
            rapi.SetRenderTarget(camera->GetViewport()->GetTarget());
            rapi.ClearViewport(clearBuffers, Color::Black);

            _material->BindCamera(camera, SelectionUtils::RenderType::Draw);

            rapi.SetVertexDeclaration(_instanceBuffer.PointVDecl);
            rapi.SetVertexBuffers(0, &_instanceBuffer.PointVB, 1);
            rapi.SetDrawOperation(DOT_POINT_LIST);

            UINT64 elementToDraw = static_cast<UINT64>(instancedElements.size());

            auto iterBegin = instancedElements.begin();
            auto iterRangeStart = iterBegin;
            auto iterRangeEnd = iterBegin + ((elementToDraw >= MAX_HUD_INSTANCED_BLOCK) ? MAX_HUD_INSTANCED_BLOCK : elementToDraw);

            do
            {
                UINT64 elementsDrawn = static_cast<UINT32>(iterRangeEnd - iterRangeStart);

                _material->BindHud(iterRangeStart, iterRangeEnd);
                _material->Bind();
                rapi.Draw(0, 1, static_cast<UINT32>(elementsDrawn));

                elementToDraw = elementToDraw - elementsDrawn;

                iterRangeStart = iterRangeEnd;
                iterRangeEnd = iterRangeStart + ((elementToDraw >= MAX_HUD_INSTANCED_BLOCK) ? MAX_HUD_INSTANCED_BLOCK : elementToDraw);
            } 
            while (elementToDraw > 0);

            rapi.SetRenderTarget(nullptr);
        } 
    }

    void Hud::GetHudElements(const HCamera& camera, const HSceneObject& sceneObject, Vector<SelectionUtils::PerHudInstanceData>& instancedElements)
    {
        SelectionUtils::PerHudInstanceData element;

        for (const auto& component : sceneObject->GetComponents())
        {
            TypeID_Core type = (TypeID_Core)component->GetCoreType();

            switch (type)
            {
                case TypeID_Core::TID_CCamera: 
                {
                    HCamera cameraElement = static_object_cast<CCamera>(component);
                    if (cameraElement->GetActive() && EditorUtils::DoFrustumCulling(camera, cameraElement))
                    {
                        const Transform& tfrm = cameraElement->GetTransform();
                        element.MatWorldNoScale = Matrix4::TRS(tfrm.GetPosition(), tfrm.GetRotation(), Vector3::ONE);
                        element.Type = static_cast<float>(SelectionUtils::HudType::Camera);
                        element.Color = Color::Black.GetAsVector4();

                        instancedElements.push_back(element);
                    }
                }
                break;

                default:
                break;
            }

            SelectionUtils::FillPerInstanceHud(instancedElements, camera, component, SelectionUtils::RenderType::Draw);
        }

        for (const auto& childSO : sceneObject->GetChildren())
            GetHudElements(camera, childSO, instancedElements);
    }
}
