#include "TeBulletMeshCollider.h"
#include "TeBulletPhysics.h"
#include "TeBulletFCollider.h"
#include "TeBulletMesh.h"

namespace te
{
    BulletMeshCollider::BulletMeshCollider(BulletPhysics* physics, BulletScene* scene, const Vector3& position, const Quaternion& rotation)
        : BulletCollider(physics, scene)
    {
        _internal = te_new<BulletFCollider>(physics, scene, _shape);
        _internal->SetPosition(position);
        _internal->SetRotation(rotation);

        UpdateCollider();
    }

    BulletMeshCollider::~BulletMeshCollider()
    { 
        te_delete((BulletFCollider*)_internal);
        te_safe_delete(_shape);
    }

    void BulletMeshCollider::SetScale(const Vector3& scale)
    {
        MeshCollider::SetScale(scale);
        UpdateCollider();
    }

    void BulletMeshCollider::SetMesh(const HPhysicsMesh& mesh)
    {
        MeshCollider::SetMesh(mesh);
        UpdateCollider();
    }

    void BulletMeshCollider::SetCollisionType(PhysicsMeshType type)
    {
        MeshCollider::SetCollisionType(type);
        UpdateCollider();
    }

    void BulletMeshCollider::UpdateCollider()
    {
        if (_shape)
        {
            te_delete(_shape);
            _shape = nullptr;
            ((BulletFCollider*)_internal)->SetShape(_shape);
        }

        if (!_mesh.IsLoaded())
            return;

        BulletFMesh* fMesh = static_cast<BulletFMesh*>(_mesh->_getInternal());
        if (!fMesh)
        {
            TE_DEBUG("No data inside the PhysicsMesh");
            return;
        }

        if(_collisionType == PhysicsMeshType::Convex)
        {
            const SPtr<BulletMesh::ConvexMesh> convexMesh = fMesh->GetConvexMesh();

            if (!convexMesh)
            {
                TE_DEBUG("PhysicsMesh does not have any ConvexMesh Data");
                return;
            }
            
            _shape = te_new<btConvexHullShape>();
            btConvexHullShape* hullShape = (btConvexHullShape*)_shape;

            for (UINT32 i = 0; i < convexMesh->NumVertices; i++)
            {
                Vector3 position = *(Vector3*)(convexMesh->Vertices + convexMesh->Stride * i);
                hullShape->addPoint(ToBtVector3(position));
            }

            hullShape->setUserPointer(this);
            hullShape->optimizeConvexHull();
            hullShape->initializePolyhedralFeatures();

            ((BulletFCollider*)_internal)->SetShape(_shape);
            _shape->setLocalScaling(ToBtVector3(_internal ? _internal->GetScale() : Vector3::ONE));
        }
        else
        {
            const SPtr<BulletMesh::TriangleMesh> triangleMesh = fMesh->GetTriangleMesh();

            if (!triangleMesh)
            {
                TE_DEBUG("PhysicsMesh does not have any TriangleMesh Data");
                return;
            }

            btTriangleMesh* meshInterface = new btTriangleMesh();
            Vector3* vertices = (Vector3*)triangleMesh->Vertices;
            UINT32* indices32 = (UINT32*)(triangleMesh->Indices);
            UINT16* indices16 = (UINT16*)(triangleMesh->Indices);

            for (UINT32 i = 0; i < triangleMesh->NumIndices / 3; i++)
            {
                if (triangleMesh->Use32BitIndex)
                {
                    const btVector3& v0 = ToBtVector3(vertices[indices32[i * 3]]);
                    const btVector3& v1 = ToBtVector3(vertices[indices32[i * 3 + 1]]);
                    const btVector3& v2 = ToBtVector3(vertices[indices32[i * 3 + 2]]);
                    meshInterface->addTriangle(v0, v1, v2);
                }
                else
                {
                    const btVector3& v0 = ToBtVector3(vertices[indices16[i * 3]]);
                    const btVector3& v1 = ToBtVector3(vertices[indices16[i * 3 + 1]]);
                    const btVector3& v2 = ToBtVector3(vertices[indices16[i * 3 + 2]]);
                    meshInterface->addTriangle(v0, v1, v2);
                }
            }

            _shape = te_new<btBvhTriangleMeshShape>(meshInterface, true, true);
            _shape->setUserPointer(this);

            ((BulletFCollider*)_internal)->SetShape(_shape);
            _shape->setLocalScaling(ToBtVector3(_internal ? _internal->GetScale() : Vector3::ONE));
        }
    }

    void BulletMeshCollider::OnMeshChanged()
    {
        UpdateCollider();
    }
}
