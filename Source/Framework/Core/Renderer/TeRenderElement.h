#pragma once

#include "TeCorePrerequisites.h"
#include "RenderAPI/TeSubMesh.h"
#include "Mesh/TeMesh.h"
#include "Material/TeMaterial.h"

namespace te
{
    class TE_CORE_EXPORT RenderElement
    {
    public:
        /**	Reference to the mesh to render. */
        SPtr<Mesh> MeshElem;

        /**	Portion of the mesh to render. */
        SubMesh SubMeshElem;

        /**	Material to render the mesh with. */
        SPtr<Material> MaterialElem;

        /** Index of the technique in the material to render the element with. */
        UINT32 DefaultTechniqueIdx = 0;

        /** Renderer specific value that identifies the type of this renderable element. */
        UINT32 Type = 0;

        /*  All params used by this element for all passes */
        Vector<SPtr<GpuParams>> GpuParamsElem;

        /** Executes the draw call for the render element. */
        virtual void Draw() const = 0;

    protected:
        RenderElement();
        virtual ~RenderElement();
    };
}
