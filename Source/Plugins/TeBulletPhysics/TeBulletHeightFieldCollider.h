#pragma once

#include "TeBulletPhysicsPrerequisites.h"
#include "Physics/TeHeightFieldCollider.h"
#include "TeBulletCollider.h"
#include "TeBulletPhysics.h"

namespace te
{
    /** Bullet implementation of a HeightFieldCollider. */
    class BulletHeightFieldCollider : public HeightFieldCollider, public BulletCollider
    {
    public:
        BulletHeightFieldCollider(BulletPhysics* physics, BulletScene* scene, const Vector3& position, const Quaternion& rotation);
        ~BulletHeightFieldCollider();

        /** @copydoc HeightFieldCollider::SetScale */
        void SetScale(const Vector3& scale) override;

        /** @copydoc HeightFieldCollider::SetHeightField */
        void SetHeightField(const HPhysicsHeightField& heightField) override;

        /** @copydoc HeightFieldCollider::SetMinHeight */
        void SetMinHeight(const float& minHeight) override;

        /** @copydoc HeightFieldCollider::SetMinHeight */
        void SetMaxHeight(const float& minHeight) override;

    private:
        /** @copydoc BulletCollider::UpdateCollider */
        void UpdateCollider() override;

        /** @copydoc HeightFieldCollider::OnHeightFieldChanged */
        void OnHeightFieldChanged() override;

    private:
        btHeightfieldTerrainShape* _shape = nullptr;
        UINT8* _scaledHeightMap = nullptr;
    };
}
