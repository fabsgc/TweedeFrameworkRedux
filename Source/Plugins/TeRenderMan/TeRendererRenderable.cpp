#include "TeRendererRenderable.h"
#include "Mesh/TeMesh.h"
#include "Utility/TeBitwise.h"

namespace te
{ 
    PerObjectParamDef gPerObjectParamDef;
    PerCallParamDef gPerCallParamDef;

    void PerObjectBuffer::Update(SPtr<GpuParamBlockBuffer>& buffer, const Matrix4& tfrm, 
        const Matrix4& tfrmNoScale, const Matrix4& prevTfrm, UINT32 layer)
    {
        gPerObjectParamDef.gMatWorld.Set(buffer, tfrm.Transpose());
        gPerObjectParamDef.gMatInvWorld.Set(buffer, tfrm.InverseAffine().Transpose());
        gPerObjectParamDef.gMatWorldNoScale.Set(buffer, tfrmNoScale.Transpose());
        gPerObjectParamDef.gMatInvWorldNoScale.Set(buffer, tfrmNoScale.InverseAffine().Transpose());
        gPerObjectParamDef.gMatPrevWorld.Set(buffer, prevTfrm.Transpose());
        gPerObjectParamDef.gLayer.Set(buffer, (INT32)layer);
    }

    void RenderableElement::Draw() const
    {
        RenderAPI& rapi = RenderAPI::Instance();
        SPtr<VertexData> vertexData = MeshElem->GetVertexData();

        rapi.SetVertexDeclaration(MeshElem->GetVertexData()->vertexDeclaration);

        auto& vertexBuffers = vertexData->GetBuffers();
        if (vertexBuffers.size() > 0)
        {
            SPtr<VertexBuffer> buffers[TE_MAX_BOUND_VERTEX_BUFFERS];

            UINT32 endSlot = 0;
            UINT32 startSlot = TE_MAX_BOUND_VERTEX_BUFFERS;
            for (auto iter = vertexBuffers.begin(); iter != vertexBuffers.end(); ++iter)
            {
                if (iter->first >= TE_MAX_BOUND_VERTEX_BUFFERS)
                    TE_ASSERT_ERROR(false, "Buffer index out of range", __FILE__, __LINE__);

                startSlot = std::min(iter->first, startSlot);
                endSlot = std::max(iter->first, endSlot);
            }

            for (auto iter = vertexBuffers.begin(); iter != vertexBuffers.end(); ++iter)
            {
                buffers[iter->first - startSlot] = iter->second;
            }

            rapi.SetVertexBuffers(startSlot, buffers, endSlot - startSlot + 1);
        }

        SPtr<IndexBuffer> indexBuffer = MeshElem->GetIndexBuffer();
        rapi.SetIndexBuffer(indexBuffer);

        rapi.SetDrawOperation(SubMeshElem.DrawOp);

        UINT32 indexCount = SubMeshElem.IndexCount;
        rapi.DrawIndexed(SubMeshElem.IndexOffset + MeshElem->GetIndexOffset(), indexCount, MeshElem->GetVertexOffset(),
            vertexData->vertexCount, 0);

        MeshElem->_notifyUsedOnGPU();
    }

    RendererRenderable::RendererRenderable()
    {
        PerObjectParamBuffer = gPerObjectParamDef.CreateBuffer();
        PerCallParamBuffer = gPerCallParamDef.CreateBuffer();
    }

    RendererRenderable::~RendererRenderable()
    { }

    void RendererRenderable::UpdatePerObjectBuffer()
    {
        const Matrix4 worldNoScaleTransform = RenderablePtr->GetMatrixNoScale();
        const UINT32 layer = Bitwise::mostSignificantBit(RenderablePtr->GetLayer());
        PerObjectBuffer::Update(PerObjectParamBuffer, WorldTfrm, worldNoScaleTransform, PrevWorldTfrm, layer);
    }

    void RendererRenderable::UpdatePerCallBuffer(const Matrix4& viewProj, bool flush)
    {
        const Matrix4 worldViewProjMatrix = viewProj * RenderablePtr->GetMatrix();

        gPerCallParamDef.gMatWorldViewProj.Set(PerCallParamBuffer, worldViewProjMatrix.Transpose());

        if (flush)
            PerCallParamBuffer->FlushToGPU();
    }
}
