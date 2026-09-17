#include "MTextureView.hpp"
#include "MTexture.hpp"
namespace GVM::RHI::Metal
{

    MTextureView::MTextureView()
    {
    }

    void MTextureView::init(MTexture *parentTexture, MTL::Texture *nativeTextureView, const eastl::string &labelName)
    {
        this->mParentTexture = parentTexture;
        this->mNativeTextureView = nativeTextureView;
        this->mLabelName = labelName;
        NS::String *nsLabel = NS::String::string(labelName.c_str(), NS::UTF8StringEncoding);
        nativeTextureView->setLabel(nsLabel);
    }

    TextureFormat MTextureView::getFormat() const
    {
        return mParentTexture != nullptr ? mParentTexture->getFormat() : TextureFormat::Undefined;
    }

    uint32_t MTextureView::getWidth() const
    {
        return mNativeTextureView != nullptr ? static_cast<uint32_t>(mNativeTextureView->width()) : 0u;
    }

    uint32_t MTextureView::getHeight() const
    {
        return mNativeTextureView != nullptr ? static_cast<uint32_t>(mNativeTextureView->height()) : 0u;
    }

    void MTextureView::destroy()
    {
        if (mNativeTextureView != nullptr)
        {
            mNativeTextureView->release();
            mNativeTextureView = nullptr;
        }
        mParentTexture = nullptr;
    }

    MTL::Texture *MTextureView::getNativeTextureView() const
    {
        return mNativeTextureView;
    }

    MTexture *MTextureView::getParentTexture() const
    {
        return mParentTexture;
    }

} // namespace GVM::RHI::Metal
