#include "TeGpuProgram.h"
#include "TeGpuProgramManager.h"

namespace te
{
    GpuProgramBytecode::~GpuProgramBytecode()
    {
        if (Instructions.Data)
        {
            te_free(Instructions.Data);
        }
    }

    GpuProgram::GpuProgram(const GPU_PROGRAM_DESC& desc, GpuDeviceFlags deviceMask)
        : _type(desc.Type)
        , _language(desc.Language)
        , _entryPoint(desc.EntryPoint)
        , _source(desc.Source)
        , _bytecode(desc.Bytecode)
        , _needsAdjacencyInfo(desc.RequiresAdjacency)
    {
        _parametersDesc = te_shared_ptr_new<GpuParamDesc>();
    }

    GpuProgram::~GpuProgram()
    { }

    bool GpuProgram::IsSupported() const
    {
        return true;
    }

    SPtr<GpuProgram> GpuProgram::Create(const GPU_PROGRAM_DESC& desc, GpuDeviceFlags deviceMask)
    {
        return GpuProgramManager::Instance().Create(desc, deviceMask);
    }

    SPtr<GpuProgramBytecode> GpuProgram::CompileBytecode(const GPU_PROGRAM_DESC& desc)
    {
        return GpuProgramManager::Instance().CompileBytecode(desc);
    }
}