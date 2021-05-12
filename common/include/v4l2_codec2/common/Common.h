// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ANDROID_V4L2_CODEC2_COMMON_COMMON_H
#define ANDROID_V4L2_CODEC2_COMMON_COMMON_H

#include <inttypes.h>

#include <optional>
#include <string>

#include <ui/Rect.h>
#include <ui/Size.h>

namespace android {

// The offset and stride of a video frame plane.
struct VideoFramePlane {
    uint32_t mOffset;
    uint32_t mStride;
};

// Check whether |rect1| completely contains |rect2|.
bool contains(const Rect& rect1, const Rect& rect2);

// Convert the specified |rect| to a string.
std::string toString(const Rect& rect);

// Get the area encapsulated by the |size|. Returns nullopt if multiplication causes overflow.
std::optional<int> getArea(const ui::Size& size);

// Check whether the specified |size| is empty
bool isEmpty(const ui::Size& size);

// Convert the specified |size| to a string.
std::string toString(const ui::Size& size);

}  // namespace android

#endif  // ANDROID_V4L2_CODEC2_COMMON_COMMON_H
