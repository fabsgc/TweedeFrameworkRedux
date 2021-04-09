#pragma once

#include "TeCorePrerequisites.h"
#include "Physics/TeJoint.h"

namespace te
{
    struct SPHERICAL_JOINT_DESC;

    /**
     * A spherical joint removes all translational degrees of freedom but allows all rotational degrees of freedom.
     * Essentially this ensures that the anchor points of the two bodies are always coincident. Bodies are allowed to
     * rotate around the anchor points, and their rotatation can be limited by an elliptical cone.
     */
    class TE_CORE_EXPORT SphericalJoint : public Joint
    {
    public:
        SphericalJoint(const SPHERICAL_JOINT_DESC& desc) { }
        virtual ~SphericalJoint() = default;

        /**
         * Creates a new spherical joint.
         *
         * @param[in]	scene		Scene to which to add the joint.
         * @param[in]	desc		Settings describing the joint.
         */
        static SPtr<SphericalJoint> Create(PhysicsScene& scene, const SPHERICAL_JOINT_DESC& desc);
    };

    /** Structure used for initializing a new SphericalJoint. */
    struct SPHERICAL_JOINT_DESC : JOINT_DESC
    { };
}