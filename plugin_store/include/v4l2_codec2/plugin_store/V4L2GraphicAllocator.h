// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ANDROID_V4L2_CODEC2_PLUGIN_STORE_V4L2_GRAPHIC_ALLOCATOR_H
#define ANDROID_V4L2_CODEC2_PLUGIN_STORE_V4L2_GRAPHIC_ALLOCATOR_H

#include <stdint.h>

#include <optional>

#include <C2Buffer.h>

namespace android {

class V4L2GraphicAllocator : public C2Allocator {
public:
    V4L2GraphicAllocator(id_t id);
    ~V4L2GraphicAllocator() override = default;

    // C2Allocator implementation.
    id_t getId() const override { return mTraits->id; }
    C2String getName() const override { return mTraits->name; }
    std::shared_ptr<const Traits> getTraits() const override { return mTraits; }
    c2_status_t newGraphicAllocation(uint32_t width, uint32_t height, uint32_t format,
                                     C2MemoryUsage usage,
                                     std::shared_ptr<C2GraphicAllocation>* allocation) override;
    c2_status_t priorGraphicAllocation(const C2Handle* handle,
                                       std::shared_ptr<C2GraphicAllocation>* allocation) override;
    bool checkHandle(const C2Handle* const handle) const override;

    static std::optional<uint32_t> getIdFromC2HandleWithId(
            const C2Handle* const grallocHandleWithId);

    // Wrap native_handle_t to C2HandleWithId, then wrap it to C2HandleGralloc.
    static C2Handle* WrapNativeHandleToC2HandleWithId(const native_handle_t* const nativeHandle,
                                                      uint32_t width, uint32_t height,
                                                      uint32_t format, uint64_t usage,
                                                      uint32_t stride, uint32_t generation,
                                                      uint64_t igbpId, uint32_t igbpSlot);

    // Unwrap C2HandleWithId to C2HandleGralloc, and then unwrap it to native_handle_t and metadata.
    static native_handle_t* UnwrapAndMoveC2HandleWithId2NativeHandle(
            const C2Handle* const grallocHandleWithId, uint32_t* uniqueId, uint32_t* width,
            uint32_t* height, uint32_t* format, uint64_t* usage, uint32_t* stride,
            uint32_t* generation, uint64_t* igbpId, uint32_t* igbpSlot);

    static C2Handle* MigrateC2HandleWithId(const C2Handle* const handleWithId, uint32_t newUsage,
                                           uint32_t newGeneration, uint64_t newIgbpId,
                                           uint32_t newIgbpSlot);

private:
    std::shared_ptr<C2Allocator> mAllocator;
    std::shared_ptr<C2Allocator::Traits> mTraits;
};

}  // namespace android
#endif  // ANDROID_V4L2_CODEC2_PLUGIN_STORE_V4L2_GRAPHIC_ALLOCATOR_H
