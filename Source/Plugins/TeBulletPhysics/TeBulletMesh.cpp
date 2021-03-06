#include "TeBulletMesh.h"
#include "Mesh/TeMeshData.h"
#include "RenderAPI/TeVertexDataDesc.h"
#include "TeBulletPhysics.h"
#include "Math/TeAABox.h"

namespace te
{
    BulletMesh::BulletMesh(const SPtr<MeshData>& meshData)
        : PhysicsMesh(meshData)
    { }

    void BulletMesh::Initialize()
    {
        if (_internal == nullptr) // Could be not-null if we're deserializing
            _internal = te_shared_ptr_new<BulletFMesh>(_initMeshData);

        PhysicsMesh::Initialize();
    }

    void BulletMesh::Destroy()
    {
        _internal = nullptr;
        PhysicsMesh::Destroy();
    }

    BulletFMesh::BulletFMesh()
        : FPhysicsMesh(nullptr, TID_FBulletMesh)
    { }

    BulletFMesh::BulletFMesh(const SPtr<MeshData>& meshData)
        : FPhysicsMesh(meshData, TID_FBulletMesh)
    {
        Initialize();
    }

    BulletFMesh::~BulletFMesh()
    {
        if (_triangleMesh)
            te_delete(_triangleMesh->Vertices);

        _convexMesh = nullptr;
        _triangleMesh = nullptr;
    }

    void BulletFMesh::Initialize()
    {
        if (!_meshData)
        {
            TE_DEBUG("Provided PhysicsMesh does not have any mesh data.");
            return;
        }

        SPtr<VertexDataDesc> vertexDesc = _meshData->GetVertexDesc();
        if (!vertexDesc->HasElement(VES_POSITION))
        {
            TE_DEBUG("Provided PhysicsMesh mesh data has no vertex positions.");
            return;
        }

        if (_meshData != nullptr)
        {
            // ConvexMesh
            {
                _convexMesh = te_shared_ptr_new<BulletMesh::ConvexMesh>();

                _convexMesh->NumVertices = _meshData->GetNumVertices();
                _convexMesh->Stride = vertexDesc->GetVertexStride();
                _convexMesh->Vertices = _meshData->GetElementData(VES_POSITION);
            }

            // TriangleMesh
            {
                _triangleMesh = te_shared_ptr_new<BulletMesh::TriangleMesh>();

                UINT32 numVertices = _meshData->GetNumVertices();
                UINT32 numIndices = _meshData->GetNumIndices();
                UINT32 vertexStride = vertexDesc->GetVertexStride();
                UINT32 indexStride = _meshData->GetIndexElementSize();
                UINT8* indices = (indexStride == sizeof(UINT32))
                    ? (UINT8*)_meshData->GetIndices32() : (UINT8*)_meshData->GetIndices16();

                UINT8* vertices = te_allocate<UINT8>(sizeof(Vector3) * numVertices);
                UINT8* vertexReader = _meshData->GetElementData(VES_POSITION);
                Vector3* vertexWriter = (Vector3*)vertices;

                for (UINT32 i = 0; i < numVertices; i++)
                {
                    Vector3* currVertex = (Vector3*)(vertexReader + (UINT8)vertexStride * i);
                    memcpy(vertexWriter, currVertex, sizeof(Vector3));

                    if(i < numVertices - 1)
                        vertexWriter++;
                }

                _triangleMesh->NumVertices = numVertices;
                _triangleMesh->NumIndices = numIndices;
                _triangleMesh->VertexStride = sizeof(Vector3);
                _triangleMesh->IndexStride = indexStride;
                _triangleMesh->Vertices = vertices;
                _triangleMesh->Indices = indices;
                _triangleMesh->Use32BitIndex = (indexStride == sizeof(UINT32)) ? true : false;
            }
        }
    }
}
