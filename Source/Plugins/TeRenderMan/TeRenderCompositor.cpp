#include "TeRenderCompositor.h"
#include "Renderer/TeCamera.h"
#include "Mesh/TeMesh.h"
#include "TeRendererView.h"
#include "TeRenderManOptions.h"
#include "TeRendererScene.h"
#include "TeRenderMan.h"
#include "Renderer/TeRendererUtility.h"

namespace te
{
    UnorderedMap<String, RenderCompositor::NodeType*> RenderCompositor::_nodeTypes;

    /** Renders all elements in a render queue. */
    void RenderQueueElements(const Vector<RenderQueueElement>& elements, const RendererView& view)
    {
        SPtr<Material> lastMaterial = nullptr;
        UINT32 gpuParamsBindFlags = 0;

        for(auto& entry : elements)
        {
            if(entry.ApplyPass)
                gRendererUtility().SetPass(entry.RenderElem->MaterialElem, entry.TechniqueIdx, entry.PassIdx);

            // If Material is the same as the previous object, we only set constant buffer params
            // Instead, we set full gpu params
            // We also set camera buffer view here (because it will set PerCameraBuffer correctly for the current pass on this material only once)
            if (!lastMaterial || lastMaterial != entry.RenderElem->MaterialElem)
            {
                RenderAPI& rapi = RenderAPI::Instance();
                gpuParamsBindFlags = GPU_BIND_ALL;
                lastMaterial = entry.RenderElem->MaterialElem;

                entry.RenderElem->GpuParamsElem[entry.PassIdx]
                    ->SetParamBlockBuffer("PerCameraBuffer", view.GetPerViewBuffer());
                
                rapi.SetGpuParams(entry.RenderElem->GpuParamsElem[entry.PassIdx],
                    GPU_BIND_PARAM_BLOCK, GPU_BIND_PARAM_BLOCK_LISTED, { "PerCameraBuffer" });
            }
            else
            {
                entry.RenderElem->MaterialElem->SetGpuParam(entry.RenderElem->GpuParamsElem[entry.PassIdx]);
                gpuParamsBindFlags = GPU_BIND_PARAM_BLOCK;
            }

            bool isInstanced = (entry.RenderElem->InstanceCount > 0) ? true : false;

            gRendererUtility().SetPassParams(entry.RenderElem->GpuParamsElem[entry.PassIdx], gpuParamsBindFlags, isInstanced);
            entry.RenderElem->Draw();
        }
    }

    RenderCompositor::~RenderCompositor()
    {
        Clear();
    }

    void RenderCompositor::Build(const RendererView& view, const String& finalNode)
    {
        Clear();

        UnorderedMap<String, UINT32> processedNodes;
        _isValid = true;

        std::function<bool(const String&)> registerNode = [&](const String& nodeId)
        {
            // Find node type
            auto iterFind = _nodeTypes.find(nodeId);
            if (iterFind == _nodeTypes.end())
            {
                TE_DEBUG("Cannot find render compositor node of type \"{" + String(nodeId.c_str()) + "}\".", __FILE__, __LINE__);
                return false;
            }

            NodeType* nodeType = iterFind->second;

            // Register current node
            auto iterFind2 = processedNodes.find(nodeId);

            // New node
            if (iterFind2 == processedNodes.end())
            {
                // Mark it as invalid for now
                processedNodes[nodeId] = -1;
            }

            // Register node dependencies
            Vector<String> depIds = nodeType->GetDependencies(view);
            for (auto& dep : depIds)
            {
                if (!registerNode(dep))
                    return false;
            }

            // Register current node
            UINT32 curIdx;

            // New node, properly populate its index
            if (iterFind2 == processedNodes.end())
            {
                iterFind2 = processedNodes.find(nodeId);

                curIdx = (UINT32)_nodeInfos.size();
                _nodeInfos.push_back(NodeInfo());
                processedNodes[nodeId] = curIdx;

                NodeInfo& nodeInfo = _nodeInfos.back();
                nodeInfo.Node = nodeType->Create();
                nodeInfo.Type = nodeType;
                nodeInfo.LastUseIdx = -1;

                for (auto& depId : depIds)
                {
                    iterFind2 = processedNodes.find(depId);

                    NodeInfo& depNodeInfo = _nodeInfos[iterFind2->second];
                    nodeInfo.Inputs.push_back(depNodeInfo.Node);
                }
            }
            else // Existing node
            {
                curIdx = iterFind2->second;

                // Check if invalid
                if (curIdx == (UINT32)-1)
                {
                    TE_DEBUG("Render compositor nodes recursion detected. Node \"{" + String(nodeId.c_str()) + "}\" "
                        "depends on node \"{" + String(iterFind->first.c_str()) + "}\" which is not available at this stage.", __FILE__, __LINE__);
                    return false;
                }
            }

            // Update dependency last use counters
            for (auto& dep : depIds)
            {
                iterFind2 = processedNodes.find(dep);

                NodeInfo& depNodeInfo = _nodeInfos[iterFind2->second];
                if (depNodeInfo.LastUseIdx == (UINT32)-1)
                    depNodeInfo.LastUseIdx = curIdx;
                else
                    depNodeInfo.LastUseIdx = std::max(depNodeInfo.LastUseIdx, curIdx);
            }

            return true;
        };

        _isValid = registerNode(finalNode);

        if (!_isValid)
            Clear();
    }

    void RenderCompositor::Execute(RenderCompositorNodeInputs& inputs) const
    { 
        if (!_isValid)
            return;

        Vector<const NodeInfo*> activeNodes;

        UINT32 idx = 0;
        for (auto& entry : _nodeInfos)
        {
            inputs.InputNodes = entry.Inputs;
            entry.Node->Render(inputs);
        
            activeNodes.push_back(&entry);

            for (UINT32 i = 0; i < (UINT32)activeNodes.size(); ++i)
            {
                if (activeNodes[i] == nullptr)
                    continue;

                if (activeNodes[i]->LastUseIdx <= idx)
                {
                    activeNodes[i]->Node->Clear();
                    activeNodes[i] = nullptr;
                }
            }

            idx++;
        }

        if (!_nodeInfos.empty())
            _nodeInfos.back().Node->Clear();
    }

    void RenderCompositor::Clear()
    {
        for (auto& entry : _nodeInfos)
            te_delete(entry.Node);

        _nodeInfos.clear();
        _isValid = false;
    }

    void RCNodeForwardPass::Render(const RenderCompositorNodeInputs& inputs)
    { 
        // Allocate necessary textures & targets
        GpuResourcePool& resPool = gGpuResourcePool();
        const RendererViewProperties& viewProps = inputs.View.GetProperties();

        const UINT32 width = viewProps.Target.ViewRect.width;
        const UINT32 height = viewProps.Target.ViewRect.height;
        const UINT32 numSamples = viewProps.Target.NumSamples;

        bool needsVelocity = inputs.View.RequiresVelocityWrites();

        // Note: Consider customizable formats. e.g. for testing if quality can be improved with higher precision normals.
        SceneTex = resPool.Get(POOLED_RENDER_TEXTURE_DESC::Create2D(PF_RGBA8, width, height, TU_RENDERTARGET,
            numSamples, true));
        //SpecularTex = resPool.Get(POOLED_RENDER_TEXTURE_DESC::Create2D(PF_RGBA8, width, height, TU_RENDERTARGET,
        //    numSamples, true));
        //AlbedoTex = resPool.Get(POOLED_RENDER_TEXTURE_DESC::Create2D(PF_RGBA8, width, height, TU_RENDERTARGET,
        //    numSamples, true));
        NormalTex = resPool.Get(POOLED_RENDER_TEXTURE_DESC::Create2D(PF_RGBA8, width, height, TU_RENDERTARGET,
            numSamples, true));
        EmissiveTex = resPool.Get(POOLED_RENDER_TEXTURE_DESC::Create2D(PF_RGBA8, width, height, TU_RENDERTARGET,
            numSamples, true));

        if (needsVelocity)
        {
        //    VelocityTex = resPool.Get(POOLED_RENDER_TEXTURE_DESC::Create2D(PF_RG16S, width, height, TU_RENDERTARGET,
        //        numSamples, false));
        }

        DepthTex = gGpuResourcePool().Get(POOLED_RENDER_TEXTURE_DESC::Create2D(PF_D32_S8X24, width, height, TU_DEPTHSTENCIL,
            numSamples, false));

        bool rebuildRT = false;
        if (RenderTargetTex != nullptr)
        {
            UINT32 targetIdx = 0;
            rebuildRT |= RenderTargetTex->GetColorTexture(targetIdx++) != SceneTex->Tex;
            //rebuildRT |= RenderTargetTex->GetColorTexture(targetIdx++) != SpecularTex->Tex;
            //rebuildRT |= RenderTargetTex->GetColorTexture(targetIdx++) != AlbedoTex->Tex;
            rebuildRT |= RenderTargetTex->GetColorTexture(targetIdx++) != NormalTex->Tex;
            rebuildRT |= RenderTargetTex->GetColorTexture(targetIdx++) != EmissiveTex->Tex;
            //if (needsVelocity) 
            //    rebuildRT |= RenderTargetTex->GetColorTexture(targetIdx++) != VelocityTex->Tex;
            rebuildRT |= RenderTargetTex->GetDepthStencilTexture() != DepthTex->Tex;
        }
        else
            rebuildRT = true;

        if (RenderTargetTex == nullptr || rebuildRT)
        {
            UINT32 targetIdx = 0;

            RENDER_TEXTURE_DESC gbufferDesc;
            gbufferDesc.ColorSurfaces[targetIdx].Tex = SceneTex->Tex;
            gbufferDesc.ColorSurfaces[targetIdx].Face = 0;
            gbufferDesc.ColorSurfaces[targetIdx].NumFaces = 1;
            gbufferDesc.ColorSurfaces[targetIdx].MipLevel = 0;
            targetIdx++;

            /*gbufferDesc.ColorSurfaces[targetIdx].Tex = SpecularTex->Tex;
            gbufferDesc.ColorSurfaces[targetIdx].Face = 0;
            gbufferDesc.ColorSurfaces[targetIdx].NumFaces = 1;
            gbufferDesc.ColorSurfaces[targetIdx].MipLevel = 0;
            targetIdx++;*/

            /*gbufferDesc.ColorSurfaces[targetIdx].Tex = AlbedoTex->Tex;
            gbufferDesc.ColorSurfaces[targetIdx].Face = 0;
            gbufferDesc.ColorSurfaces[targetIdx].NumFaces = 1;
            gbufferDesc.ColorSurfaces[targetIdx].MipLevel = 0;
            targetIdx++;*/

            gbufferDesc.ColorSurfaces[targetIdx].Tex = NormalTex->Tex;
            gbufferDesc.ColorSurfaces[targetIdx].Face = 0;
            gbufferDesc.ColorSurfaces[targetIdx].NumFaces = 1;
            gbufferDesc.ColorSurfaces[targetIdx].MipLevel = 0;
            targetIdx++;

            gbufferDesc.ColorSurfaces[targetIdx].Tex = EmissiveTex->Tex;
            gbufferDesc.ColorSurfaces[targetIdx].Face = 0;
            gbufferDesc.ColorSurfaces[targetIdx].NumFaces = 1;
            gbufferDesc.ColorSurfaces[targetIdx].MipLevel = 0;
            targetIdx++;

            /*if (needsVelocity)
            {
                gbufferDesc.ColorSurfaces[targetIdx].Tex = VelocityTex->Tex;
                gbufferDesc.ColorSurfaces[targetIdx].Face = 0;
                gbufferDesc.ColorSurfaces[targetIdx].NumFaces = 1;
                gbufferDesc.ColorSurfaces[targetIdx].MipLevel = 0;
                targetIdx++;
            }*/

            gbufferDesc.DepthStencilSurface.Tex = DepthTex->Tex;
            gbufferDesc.DepthStencilSurface.Face = 0;
            gbufferDesc.DepthStencilSurface.MipLevel = 0;

            RenderTargetTex = RenderTexture::Create(gbufferDesc);
        }

        const VisibilityInfo& visibility = inputs.View.GetVisibilityInfo();
        const auto numRenderables = (UINT32)inputs.Scene.Renderables.size();
        for (UINT32 i = 0; i < numRenderables; i++)
        {
            if (!visibility.Renderables[i].Visible &&
                (inputs.Options.CullingFlags & (UINT32)RenderManCulling::Frustum ||
                    inputs.Options.CullingFlags & (UINT32)RenderManCulling::Occlusion))
            {
                continue;
            }

            if (visibility.Renderables[i].Instanced &&
                (inputs.Options.InstancingMode == RenderManInstancing::Automatic||
                    inputs.Options.InstancingMode  == RenderManInstancing::Manual))
            {
                TE_PRINT("instanced");
                continue;
            }

            RendererRenderable* rendererRenderable = inputs.Scene.Renderables[i];
            rendererRenderable->UpdatePerCallBuffer(viewProps.ViewProjTransform);
        }

        RenderAPI& rapi = RenderAPI::Instance();

        rapi.SetRenderTarget(RenderTargetTex);
        rapi.SetRenderTarget(inputs.View.GetProperties().Target.Target);

        UINT32 clearBuffers = FBT_COLOR | FBT_DEPTH | FBT_STENCIL;
        rapi.ClearViewport(clearBuffers, Color::Black);
        rapi.ClearViewport(clearBuffers, inputs.View.GetProperties().Target.ClearColor);

        // Render all visible opaque elements
        RenderQueue* opaqueElements = inputs.View.GetOpaqueQueue().get();
        RenderQueue* transparentElements = inputs.View.GetTransparentQueue().get();
        RenderQueueElements(opaqueElements->GetSortedElements(), inputs.View);
        RenderQueueElements(transparentElements->GetSortedElements(), inputs.View);
    }

    void RCNodeForwardPass::Clear()
    { 
        SceneTex = nullptr;
        SpecularTex = nullptr;
        AlbedoTex = nullptr;
        NormalTex = nullptr;
        EmissiveTex = nullptr;
        VelocityTex = nullptr;
        DepthTex = nullptr;
    }

    Vector<String> RCNodeForwardPass::GetDependencies(const RendererView& view)
    {
        return { };
    }

    // ############# SKYBOX

    void RCNodeSkybox::Render(const RenderCompositorNodeInputs& inputs)
    { 
        Skybox* skybox = nullptr;
        if (inputs.View.GetRenderSettings().EnableSkybox)
            skybox = inputs.Scene.SkyboxElem;
    }

    void RCNodeSkybox::Clear()
    { }

    Vector<String> RCNodeSkybox::GetDependencies(const RendererView& view)
    {
        return { RCNodeForwardPass::GetNodeId() };
    }

    // ############# POST PROCESS

    SPtr<Texture> RCNodePostProcess::GetLastOutput() const
    {
        UINT32 otherIdx = (_currentIdx + 1) % 2;
        if (_output[otherIdx])
            return _output[otherIdx]->Tex;

        return nullptr;
    }

    void RCNodePostProcess::Render(const RenderCompositorNodeInputs& inputs)
    { }

    void RCNodePostProcess::Clear()
    { }

    Vector<String> RCNodePostProcess::GetDependencies(const RendererView& view)
    {
        return {
            RCNodeForwardPass::GetNodeId(),
            RCNodeSkybox::GetNodeId()
        };
    }

    // ############# TONE MAPPING

    void RCNodeTonemapping::Render(const RenderCompositorNodeInputs& inputs)
    { }

    void RCNodeTonemapping::Clear()
    { }

    Vector<String> RCNodeTonemapping::GetDependencies(const RendererView& view)
    {
        Vector<String> deps = {
            RCNodeMotionBlur::GetNodeId(),
            RCNodePostProcess::GetNodeId()
        };

        if (view.GetRenderSettings().Bloom.Enabled)
            deps.push_back(RCNodeBloom::GetNodeId());

        return deps;
    }

    // ############# MOTION BLUR

    void RCNodeMotionBlur::Render(const RenderCompositorNodeInputs& inputs)
    { }

    void RCNodeMotionBlur::Clear()
    { }

    Vector<String> RCNodeMotionBlur::GetDependencies(const RendererView& view)
    {
        return { RCNodePostProcess::GetNodeId() };
    }

    // ############# GAUSSIAN DOF

    void RCNodeGaussianDOF::Render(const RenderCompositorNodeInputs& inputs)
    { }

    void RCNodeGaussianDOF::Clear()
    { }

    Vector<String> RCNodeGaussianDOF::GetDependencies(const RendererView& view)
    {
        return
        {
            RCNodeTonemapping::GetNodeId(),
            RCNodePostProcess::GetNodeId()
        };
    }

    // ############# FXAA

    void RCNodeFXAA::Render(const RenderCompositorNodeInputs& inputs)
    { }

    void RCNodeFXAA::Clear()
    { }

    Vector<String> RCNodeFXAA::GetDependencies(const RendererView& view)
    {
        return
        {
            RCNodeGaussianDOF::GetNodeId(),
            RCNodePostProcess::GetNodeId()
        };
    }

    // ############# SSAO

    void RCNodeSSAO::Render(const RenderCompositorNodeInputs& inputs)
    { }

    void RCNodeSSAO::Clear()
    { 
        _pooledOutput = nullptr;
    }

    Vector<String> RCNodeSSAO::GetDependencies(const RendererView& view)
    {
        return
        {
            RCNodePostProcess::GetNodeId()
        };
    }

    // ############# BLOOM

    void RCNodeBloom::Render(const RenderCompositorNodeInputs& inputs)
    { }

    void RCNodeBloom::Clear()
    {
        _pooledOutput = nullptr;
    }

    Vector<String> RCNodeBloom::GetDependencies(const RendererView& view)
    {
        return
        {
            RCNodePostProcess::GetNodeId()
        };
    }

    // ############# FINAL RENDER

    void RCNodeFinalResolve::Render(const RenderCompositorNodeInputs& inputs)
    {
        const RendererViewProperties& viewProps = inputs.View.GetProperties();

        SPtr<Texture> input;
        if (viewProps.RunPostProcessing)
        {
            RCNodePostProcess* postProcessNode = static_cast<RCNodePostProcess*>(inputs.InputNodes[0]);

            // Note: Ideally the last PP effect could write directly to the final target and we could avoid this copy
            input = postProcessNode->GetLastOutput();
        }
        else
        {
            RCNodeForwardPass* forwardPaddNode = static_cast<RCNodeForwardPass*>(inputs.InputNodes[0]);
            input = forwardPaddNode->SceneTex->Tex;
        }

        SPtr<RenderTarget> target = viewProps.Target.Target;

        RenderAPI& rapi = RenderAPI::Instance();
        rapi.SetRenderTarget(target);
        rapi.SetViewport(viewProps.Target.NrmViewRect);

        gRendererUtility().Blit(input, Rect2I::EMPTY, viewProps.FlipView);

        inputs.View._notifyCompositorTargetChanged(nullptr);
    }

    void RCNodeFinalResolve::Clear()
    { }

    Vector<String> RCNodeFinalResolve::GetDependencies(const RendererView& view)
    {
        const RendererViewProperties& viewProps = view.GetProperties();

        Vector<String> deps;
        if (viewProps.RunPostProcessing)
        {
            deps.push_back(RCNodePostProcess::GetNodeId());
        }
        else
        {
            deps.push_back(RCNodeForwardPass::GetNodeId());
            deps.push_back(RCNodeSkybox::GetNodeId());
        }

        return deps;
    }
}
