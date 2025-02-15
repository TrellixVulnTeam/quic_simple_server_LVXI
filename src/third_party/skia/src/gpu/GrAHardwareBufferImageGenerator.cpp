/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkTypes.h"

#if defined(SK_BUILD_FOR_ANDROID) && __ANDROID_API__ >= 26
#define GL_GLEXT_PROTOTYPES
#define EGL_EGLEXT_PROTOTYPES


#include "src/gpu/GrAHardwareBufferImageGenerator.h"

#include <android/hardware_buffer.h>

#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrContext.h"
#include "include/gpu/GrTexture.h"
#include "include/gpu/gl/GrGLTypes.h"
#include "include/private/GrRecordingContext.h"
#include "src/core/SkExchange.h"
#include "src/core/SkMessageBus.h"
#include "src/gpu/GrAHardwareBufferUtils.h"
#include "src/gpu/GrContextPriv.h"
#include "src/gpu/GrProxyProvider.h"
#include "src/gpu/GrRecordingContextPriv.h"
#include "src/gpu/GrResourceCache.h"
#include "src/gpu/GrResourceProvider.h"
#include "src/gpu/GrResourceProviderPriv.h"
#include "src/gpu/GrTextureProxy.h"
#include "src/gpu/gl/GrGLDefines.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <GLES/glext.h>

#ifdef SK_VULKAN
#include "include/gpu/vk/GrVkExtensions.h"
#include "src/gpu/vk/GrVkGpu.h"
#endif

#define PROT_CONTENT_EXT_STR "EGL_EXT_protected_content"
#define EGL_PROTECTED_CONTENT_EXT 0x32C0

std::unique_ptr<SkImageGenerator> GrAHardwareBufferImageGenerator::Make(
        AHardwareBuffer* graphicBuffer, SkAlphaType alphaType, sk_sp<SkColorSpace> colorSpace,
        GrSurfaceOrigin surfaceOrigin) {
    AHardwareBuffer_Desc bufferDesc;
    AHardwareBuffer_describe(graphicBuffer, &bufferDesc);

    SkColorType colorType =
            GrAHardwareBufferUtils::GetSkColorTypeFromBufferFormat(bufferDesc.format);
    SkImageInfo info = SkImageInfo::Make(bufferDesc.width, bufferDesc.height, colorType,
                                         alphaType, std::move(colorSpace));

    bool createProtectedImage = 0 != (bufferDesc.usage & AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT);
    return std::unique_ptr<SkImageGenerator>(new GrAHardwareBufferImageGenerator(
            info, graphicBuffer, alphaType, createProtectedImage,
            bufferDesc.format, surfaceOrigin));
}

GrAHardwareBufferImageGenerator::GrAHardwareBufferImageGenerator(const SkImageInfo& info,
        AHardwareBuffer* hardwareBuffer, SkAlphaType alphaType, bool isProtectedContent,
        uint32_t bufferFormat, GrSurfaceOrigin surfaceOrigin)
    : INHERITED(info)
    , fHardwareBuffer(hardwareBuffer)
    , fBufferFormat(bufferFormat)
    , fIsProtectedContent(isProtectedContent)
    , fSurfaceOrigin(surfaceOrigin) {
    AHardwareBuffer_acquire(fHardwareBuffer);
}

GrAHardwareBufferImageGenerator::~GrAHardwareBufferImageGenerator() {
    AHardwareBuffer_release(fHardwareBuffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

GrSurfaceProxyView GrAHardwareBufferImageGenerator::makeView(GrRecordingContext* context) {
    if (context->priv().abandoned()) {
        return {};
    }

    auto direct = context->priv().asDirectContext();
    if (!direct) {
        return {};
    }

    GrBackendFormat backendFormat = GrAHardwareBufferUtils::GetBackendFormat(direct,
                                                                             fHardwareBuffer,
                                                                             fBufferFormat,
                                                                             false);

    GrColorType grColorType = SkColorTypeToGrColorType(this->getInfo().colorType());

    int width = this->getInfo().width();
    int height = this->getInfo().height();

    GrSurfaceDesc desc;
    desc.fWidth = width;
    desc.fHeight = height;

    GrTextureType textureType = GrTextureType::k2D;
    if (context->backend() == GrBackendApi::kOpenGL) {
        textureType = GrTextureType::kExternal;
    } else if (context->backend() == GrBackendApi::kVulkan) {
        VkFormat format;
        SkAssertResult(backendFormat.asVkFormat(&format));
        if (format == VK_FORMAT_UNDEFINED) {
            textureType = GrTextureType::kExternal;
        }
    }

    auto proxyProvider = context->priv().proxyProvider();

    AHardwareBuffer* hardwareBuffer = fHardwareBuffer;
    AHardwareBuffer_acquire(hardwareBuffer);

    const bool isProtectedContent = fIsProtectedContent;

    class AutoAHBRelease {
    public:
        AutoAHBRelease(AHardwareBuffer* ahb) : fAhb(ahb) {}
        // std::function() must be CopyConstructible, but ours should never actually be copied.
        AutoAHBRelease(const AutoAHBRelease&) { SkASSERT(0); }
        AutoAHBRelease(AutoAHBRelease&& that) : fAhb(that.fAhb) { that.fAhb = nullptr; }
        ~AutoAHBRelease() { fAhb ? AHardwareBuffer_release(fAhb) : void(); }

        AutoAHBRelease& operator=(AutoAHBRelease&& that) {
            fAhb = skstd::exchange(that.fAhb, nullptr);
            return *this;
        }
        AutoAHBRelease& operator=(const AutoAHBRelease&) = delete;

        AHardwareBuffer* get() const { return fAhb; }

    private:
        AHardwareBuffer* fAhb;
    };

    GrSwizzle readSwizzle = context->priv().caps()->getReadSwizzle(backendFormat, grColorType);

    sk_sp<GrTextureProxy> texProxy = proxyProvider->createLazyProxy(
            [direct, buffer = AutoAHBRelease(hardwareBuffer), width, height, isProtectedContent,
             backendFormat, grColorType](
                    GrResourceProvider* resourceProvider) -> GrSurfaceProxy::LazyCallbackResult {
                GrAHardwareBufferUtils::DeleteImageProc deleteImageProc = nullptr;
                GrAHardwareBufferUtils::UpdateImageProc updateImageProc = nullptr;
                GrAHardwareBufferUtils::TexImageCtx texImageCtx = nullptr;

                GrBackendTexture backendTex =
                        GrAHardwareBufferUtils::MakeBackendTexture(direct, buffer.get(),
                                                                   width, height,
                                                                   &deleteImageProc,
                                                                   &updateImageProc,
                                                                   &texImageCtx,
                                                                   isProtectedContent,
                                                                   backendFormat,
                                                                   false);
                if (!backendTex.isValid()) {
                    return {};
                }
                SkASSERT(deleteImageProc && texImageCtx);

                // We make this texture cacheable to avoid recreating a GrTexture every time this
                // is invoked. We know the owning SkIamge will send an invalidation message when the
                // image is destroyed, so the texture will be removed at that time.
                sk_sp<GrTexture> tex = resourceProvider->wrapBackendTexture(
                        backendTex, grColorType, kBorrow_GrWrapOwnership, GrWrapCacheable::kYes,
                        kRead_GrIOType);
                if (!tex) {
                    deleteImageProc(texImageCtx);
                    return {};
                }

                if (deleteImageProc) {
                    tex->setRelease(deleteImageProc, texImageCtx);
                }

                return tex;
            },
            backendFormat, desc, readSwizzle, GrRenderable::kNo, 1, fSurfaceOrigin,
            GrMipMapped::kNo, GrMipMapsStatus::kNotAllocated, GrInternalSurfaceFlags::kReadOnly,
            SkBackingFit::kExact, SkBudgeted::kNo, GrProtected::kNo,
            GrSurfaceProxy::UseAllocator::kYes);

    return GrSurfaceProxyView(std::move(texProxy), fSurfaceOrigin, readSwizzle);
}

sk_sp<GrTextureProxy> GrAHardwareBufferImageGenerator::onGenerateTexture(
        GrRecordingContext* context, const SkImageInfo& info,
        const SkIPoint& origin, bool willNeedMipMaps) {
    GrSurfaceProxyView texProxyView = this->makeView(context);
    if (!texProxyView.proxy()) {
        return nullptr;
    }

    if (0 == origin.fX && 0 == origin.fY &&
        info.width() == this->getInfo().width() && info.height() == this->getInfo().height()) {
        // If the caller wants the full texture we're done. The caller will handle making a copy for
        // mip maps if that is required.
        return texProxyView.asTextureProxyRef();
    }
    // Otherwise, make a copy for the requested subset.
    SkIRect subset = SkIRect::MakeXYWH(origin.fX, origin.fY, info.width(), info.height());

    GrMipMapped mipMapped = willNeedMipMaps ? GrMipMapped::kYes : GrMipMapped::kNo;

    GrColorType grColorType = SkColorTypeToGrColorType(this->getInfo().colorType());
    return GrSurfaceProxy::Copy(context, texProxyView.proxy(), texProxyView.origin(), grColorType,
                                mipMapped, subset, SkBackingFit::kExact,
                                SkBudgeted::kYes).asTextureProxyRef();
}

bool GrAHardwareBufferImageGenerator::onIsValid(GrContext* context) const {
    if (nullptr == context) {
        return false; //CPU backend is not supported, because hardware buffer can be swizzled
    }
    return GrBackendApi::kOpenGL == context->backend() ||
           GrBackendApi::kVulkan == context->backend();
}

#endif //SK_BUILD_FOR_ANDROID_FRAMEWORK
