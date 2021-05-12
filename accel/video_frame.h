// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Note: ported from Chromium commit head: 602bc8fa60fa
// Note: only necessary functions are ported.
// Note: some OS-specific defines have been removed
// Note: WrapExternalSharedMemory() has been removed in Chromium, but is still
// present here. Porting the code to a newer version of VideoFrame is not
// useful, as this is only a temporary step and all usage of VideoFrame will
// be removed.

#ifndef VIDEO_FRAME_H_
#define VIDEO_FRAME_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/ref_counted.h"
#include "ui/Size.h"
#include "video_frame_layout.h"
#include "video_pixel_format.h"

namespace media {

class VideoFrame : public base::RefCountedThreadSafe<VideoFrame> {
 public:
  enum {
    kMaxPlanes = 4,

    kYPlane = 0,
    kARGBPlane = kYPlane,
    kUPlane = 1,
    kUVPlane = kUPlane,
    kVPlane = 2,
    kAPlane = 3,
  };

  static size_t NumPlanes(VideoPixelFormat format);

  // Returns the required allocation size for a (tightly packed) frame of the
  // given coded size and format.
  static size_t AllocationSize(VideoPixelFormat format, const android::ui::Size& coded_size);

  // Returns the plane Size (in bytes) for a plane of the given coded size
  // and format.
  static android::ui::Size PlaneSize(VideoPixelFormat format,
                        size_t plane,
                        const android::ui::Size& coded_size);

  // Returns horizontal bits per pixel for given |plane| and |format|.
  static int PlaneHorizontalBitsPerPixel(VideoPixelFormat format, size_t plane);

  // Returns bits per pixel for given |plane| and |format|.
  static int PlaneBitsPerPixel(VideoPixelFormat format, size_t plane);

  // Returns the number of bytes per element for given |plane| and |format|.
  static int BytesPerElement(VideoPixelFormat format, size_t plane);

  // Returns true if |plane| is a valid plane index for the given |format|.
  static bool IsValidPlane(size_t plane, VideoPixelFormat format);

  // Returns true if |plane| is a valid plane index for the given |format|.
  static bool IsValidPlane(VideoPixelFormat format, size_t plane);

  // Returns the pixel size of each subsample for a given |plane| and |format|.
  // E.g. 2x2 for the U-plane in PIXEL_FORMAT_I420.
  static android::ui::Size SampleSize(VideoPixelFormat format, size_t plane);
};

}  // namespace media

#endif  // VIDEO_FRAME_H_
