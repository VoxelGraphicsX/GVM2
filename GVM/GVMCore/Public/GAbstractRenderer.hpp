#pragma once
#include "GRenderSetCommand.hpp"
#include "GVMCore.Defines.hpp"
#include <EASTL/RefCountedObject.h>
namespace GVM::Core
{

    class AbstractRendererImpl : public eastl::RefCountedObject
    {
    public:
        virtual void executeRenderSetCommand(GVM::Core::RenderSetHandle renderSetHandle, const AbstractRenderSetCommandEncoder &command) = 0;
        virtual AbstractRenderSetCommandEncoder createRenderSetCommandEncoder(GVM::Core::RenderSetHandle renderSetHandle) = 0;
        virtual bool checkRenderSetComponentResourceName(GVM::Core::RenderSetHandle renderSetHandle, GVM::Core::RenderComponentHandle renderComponentHandle, const eastl::string &name) = 0;
    };

    using AbstractRenderer = eastl::intrusive_ptr<AbstractRendererImpl>;

} // namespace GVM::Core
