// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//#define LOG_NDEBUG 0
#define LOG_TAG "V4L2GraphicAllocator"

#include <inttypes.h>

#include <atomic>

#include <C2AllocatorGralloc.h>
#include <C2PlatformSupport.h>
#include <cutils/native_handle.h>
#include <log/log.h>

#include <v4l2_codec2/plugin_store/V4L2GraphicAllocator.h>

namespace android {
namespace {

bool isValidHandle(const native_handle_t* const handle) {
    if (handle == nullptr) {
        return false;
    }

    return ((size_t)handle->version == sizeof(native_handle_t) && handle->numFds >= 0 &&
            handle->numFds <= NATIVE_HANDLE_MAX_FDS && handle->numInts >= 0 &&
            handle->numInts <= NATIVE_HANDLE_MAX_INTS);
}

uint32_t getNextUniqueId() {
    static std::atomic<uint32_t> currentId = 0;
    return currentId.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

class C2HandleWithId : public C2Handle {
public:
    static C2Handle* WrapAndMoveNativeHandle(const native_handle_t* const nativeHandle,
                                             uint32_t uniqueId) {
        ALOGV("%s(uniqueId=%u)", __func__, uniqueId);

        if (!isValidHandle(nativeHandle)) {
            ALOGE("%s(): nativeHandle is not valid", __func__);
            return nullptr;
        }

        native_handle_t* handleWithId = native_handle_create(
                nativeHandle->numFds, nativeHandle->numInts + kExtraDataNumInts);
        if (handleWithId == nullptr) {
            ALOGE("%s(): native_handle_create(%d, %d) return nullptr: %d", __func__,
                  nativeHandle->numFds, nativeHandle->numInts + kExtraDataNumInts, errno);
            return nullptr;
        }

        memcpy(&handleWithId->data, &nativeHandle->data,
               sizeof(int) * (nativeHandle->numFds + nativeHandle->numInts));
        *getExtraData(handleWithId) = ExtraData{kMagic, uniqueId};
        return reinterpret_cast<C2Handle*>(handleWithId);
    }

    static native_handle_t* UnwrapAndMoveC2Handle(const C2Handle* const handleWithId) {
        ALOGV("%s()", __func__);

        if (!isValid(handleWithId)) {
            return nullptr;
        }

        native_handle_t* nativeHandle = native_handle_create(
                handleWithId->numFds, handleWithId->numInts - kExtraDataNumInts);
        if (nativeHandle == nullptr) {
            ALOGE("%s(): native_handle_create(%d, %d) return nullptr: %d", __func__,
                  handleWithId->numFds, handleWithId->numInts - kExtraDataNumInts, errno);
            return nullptr;
        }

        memcpy(&nativeHandle->data, &handleWithId->data,
               sizeof(int) * (nativeHandle->numFds + nativeHandle->numInts));
        return nativeHandle;
    }

    static std::optional<uint32_t> getUniqueId(const C2Handle* const handleWithId) {
        const ExtraData* data = getExtraData(handleWithId);
        if (data == nullptr) {
            return std::nullopt;
        }
        if (data->magic != kMagic) {
            ALOGE("%s(): magic field is mismatched", __func__);
            return std::nullopt;
        }

        return data->uniqueId;
    }

private:
    struct ExtraData {
        uint32_t magic;
        uint32_t uniqueId;
    };
    constexpr static uint32_t kMagic = 'V4L2';
    constexpr static int kExtraDataNumInts = sizeof(ExtraData) / sizeof(int);

    static bool isValid(const C2Handle* const handle) {
        return getUniqueId(handle) != std::nullopt;
    }

    static ExtraData* getExtraData(C2Handle* const handleWithId) {
        return const_cast<ExtraData*>(
                getExtraData(const_cast<const C2Handle* const>(handleWithId)));
    }
    static const ExtraData* getExtraData(const C2Handle* const handleWithId) {
        if (!isValidHandle(handleWithId) || handleWithId->numInts < kExtraDataNumInts) {
            ALOGE("%s(): handleWithId is not valid", __func__);
            return nullptr;
        }

        const ExtraData* data = reinterpret_cast<const ExtraData*>(
                &handleWithId
                         ->data[handleWithId->numFds + handleWithId->numInts - kExtraDataNumInts]);
        return data;
    }
};

V4L2GraphicAllocator::V4L2GraphicAllocator(id_t id)
      : mAllocator(new C2AllocatorGralloc(id, true)),
        mTraits(std::make_shared<C2Allocator::Traits>(
                C2Allocator::Traits{"android.allocator.v4l2", id, C2Allocator::GRAPHIC,
                                    C2MemoryUsage{0}, C2MemoryUsage{~0ull}})) {}

c2_status_t V4L2GraphicAllocator::newGraphicAllocation(
        uint32_t width, uint32_t height, uint32_t format, C2MemoryUsage usage,
        std::shared_ptr<C2GraphicAllocation>* allocation) {
    ALOGV("%s(size=%ux%u, format=%u, usage=%" PRIu64 ")", __func__, width, height, format,
          usage.expected);

    std::shared_ptr<C2GraphicAllocation> grallocAllocation;
    c2_status_t status =
            mAllocator->newGraphicAllocation(width, height, format, usage, &grallocAllocation);
    if (status != C2_OK) {
        ALOGE("%s(): Failed to call C2AllocatorGralloc::newGraphicAllocation(): %d", __func__,
              status);
        return status;
    }

    const C2Handle* grallocHandle = grallocAllocation->handle();
    uint32_t stride, igbpSlot, generation;
    uint64_t rawUsage, igbpId;
    android::_UnwrapNativeCodec2GrallocMetadata(grallocHandle, &width, &height, &format, &rawUsage,
                                                &stride, &generation, &igbpId, &igbpSlot);
    native_handle_t* nativeHandle = android::UnwrapNativeCodec2GrallocHandle(grallocHandle);
    if (nativeHandle == nullptr) {
        ALOGE("%s(): Failed to unwrap grallocHandle to nativeHandle", __func__);
        return C2_CORRUPTED;
    }

    C2Handle* handleWithId =
            C2HandleWithId::WrapAndMoveNativeHandle(nativeHandle, getNextUniqueId());
    native_handle_delete(nativeHandle);
    if (handleWithId == nullptr) {
        ALOGE("%s(): Failed to wrap nativeHandle to handleWithId", __func__);
        return C2_CORRUPTED;
    }

    C2Handle* grallocHandleWithId = android::WrapNativeCodec2GrallocHandle(
            handleWithId, width, height, format, rawUsage, stride, generation, igbpId, igbpSlot);
    native_handle_delete(handleWithId);
    if (grallocHandleWithId == nullptr) {
        ALOGE("%s(): Failed to inject uniqueId into grallocHandle", __func__);
        return C2_CORRUPTED;
    }

    return mAllocator->priorGraphicAllocation(grallocHandleWithId, allocation);
}

c2_status_t V4L2GraphicAllocator::priorGraphicAllocation(
        const C2Handle* handle, std::shared_ptr<C2GraphicAllocation>* allocation) {
    return mAllocator->priorGraphicAllocation(handle, allocation);
}

// static
std::optional<uint32_t> V4L2GraphicAllocator::getIdFromC2HandleWithId(
        const C2Handle* const grallocHandleWithId) {
    native_handle_t* handleWithId = android::UnwrapNativeCodec2GrallocHandle(grallocHandleWithId);
    auto uniqueId = C2HandleWithId::getUniqueId(handleWithId);
    native_handle_delete(handleWithId);
    return uniqueId;
}

// static
C2Handle* V4L2GraphicAllocator::WrapNativeHandleToC2HandleWithId(
        const native_handle_t* const nativeHandle, uint32_t width, uint32_t height, uint32_t format,
        uint64_t usage, uint32_t stride, uint32_t generation, uint64_t igbpId, uint32_t igbpSlot) {
    ALOGV("%s(size=%ux%u, format=%u, usage=%" PRIu64 ", stride=%u, generation=%u, igbpId=%" PRIu64
          ", idbpSlot=%u)",
          __func__, width, height, format, usage, stride, generation, igbpId, igbpSlot);

    C2Handle* handleWithId =
            C2HandleWithId::WrapAndMoveNativeHandle(nativeHandle, getNextUniqueId());
    if (handleWithId == nullptr) {
        ALOGE("%s(): Failed to wrap nativeHandle to handleWithId", __func__);
        return nullptr;
    }

    C2Handle* grallocHandleWithId = android::WrapNativeCodec2GrallocHandle(
            handleWithId, width, height, format, usage, stride, generation, igbpId, igbpSlot);
    native_handle_delete(handleWithId);
    if (grallocHandleWithId == nullptr) {
        ALOGE("%s(): Failed to wrap handleWithId to grallocHandleWithId", __func__);
        return nullptr;
    }

    return grallocHandleWithId;
}

// static
native_handle_t* V4L2GraphicAllocator::UnwrapAndMoveC2HandleWithId2NativeHandle(
        const C2Handle* const grallocHandleWithId, uint32_t* uniqueId, uint32_t* width,
        uint32_t* height, uint32_t* format, uint64_t* usage, uint32_t* stride, uint32_t* generation,
        uint64_t* igbpId, uint32_t* igbpSlot) {
    ALOGV("%s()", __func__);

    android::_UnwrapNativeCodec2GrallocMetadata(grallocHandleWithId, width, height, format, usage,
                                                stride, generation, igbpId, igbpSlot);
    native_handle_t* handleWithId = android::UnwrapNativeCodec2GrallocHandle(grallocHandleWithId);
    if (handleWithId == nullptr) {
        ALOGE("%s(): Failed to unwrap injected to handleWithId", __func__);
        return nullptr;
    }

    std::optional<uint32_t> id = C2HandleWithId::getUniqueId(handleWithId);
    native_handle_t* nativeHandle = C2HandleWithId::UnwrapAndMoveC2Handle(handleWithId);
    native_handle_delete(handleWithId);

    if (!id.has_value()) {
        ALOGE("%s(): Failed to get uniqueId", __func__);
        return nullptr;
    }

    *uniqueId = *id;
    return nativeHandle;
}

// static
C2Handle* V4L2GraphicAllocator::MigrateC2HandleWithId(const C2Handle* const grallocHandleWithId,
                                                      uint32_t newUsage, uint32_t newGeneration,
                                                      uint64_t newIgbpId, uint32_t newIgbpSlot) {
    ALOGV("%s(newUsage=%u, newGeneration=%u, newIgbpId=%" PRIu64 ", newIgbpSlot=%u)", __func__,
          newUsage, newGeneration, newIgbpId, newIgbpSlot);

    uint32_t width, height, format, stride, igbpSlot, generation;
    uint64_t usage, igbpId;
    android::_UnwrapNativeCodec2GrallocMetadata(grallocHandleWithId, &width, &height, &format,
                                                &usage, &stride, &generation, &igbpId, &igbpSlot);
    native_handle_t* handleWithId = android::UnwrapNativeCodec2GrallocHandle(grallocHandleWithId);
    if (handleWithId == nullptr) {
        ALOGE("%s(): Failed to unwrap injected to handleWithId", __func__);
        return nullptr;
    }

    C2Handle* migratedHandle =
            android::WrapNativeCodec2GrallocHandle(handleWithId, width, height, format, newUsage,
                                                   stride, newGeneration, newIgbpId, newIgbpSlot);
    native_handle_delete(handleWithId);
    return migratedHandle;
}

}  // namespace android
