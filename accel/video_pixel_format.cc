// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Note: ported from Chromium commit head: 3b7ce92816e2
// Note: only necessary functions are ported from video_types.cc

#include "video_pixel_format.h"

#include "base/bits.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/stringprintf.h"

namespace media {

namespace {

enum {
   kMaxPlanes = 4,
   kYPlane = 0,
   kARGBPlane = kYPlane,
   kUPlane = 1,
   kUVPlane = kUPlane,
   kVPlane = 2,
   kAPlane = 3,
 };

}

std::string VideoPixelFormatToString(VideoPixelFormat format) {
  switch (format) {
    case PIXEL_FORMAT_UNKNOWN:
      return "PIXEL_FORMAT_UNKNOWN";
    case PIXEL_FORMAT_I420:
      return "PIXEL_FORMAT_I420";
    case PIXEL_FORMAT_YV12:
      return "PIXEL_FORMAT_YV12";
    case PIXEL_FORMAT_I422:
      return "PIXEL_FORMAT_I422";
    case PIXEL_FORMAT_I420A:
      return "PIXEL_FORMAT_I420A";
    case PIXEL_FORMAT_I444:
      return "PIXEL_FORMAT_I444";
    case PIXEL_FORMAT_NV12:
      return "PIXEL_FORMAT_NV12";
    case PIXEL_FORMAT_NV21:
      return "PIXEL_FORMAT_NV21";
    case PIXEL_FORMAT_YUY2:
      return "PIXEL_FORMAT_YUY2";
    case PIXEL_FORMAT_ARGB:
      return "PIXEL_FORMAT_ARGB";
    case PIXEL_FORMAT_XRGB:
      return "PIXEL_FORMAT_XRGB";
    case PIXEL_FORMAT_RGB24:
      return "PIXEL_FORMAT_RGB24";
    case PIXEL_FORMAT_MJPEG:
      return "PIXEL_FORMAT_MJPEG";
    case PIXEL_FORMAT_YUV420P9:
      return "PIXEL_FORMAT_YUV420P9";
    case PIXEL_FORMAT_YUV420P10:
      return "PIXEL_FORMAT_YUV420P10";
    case PIXEL_FORMAT_YUV422P9:
      return "PIXEL_FORMAT_YUV422P9";
    case PIXEL_FORMAT_YUV422P10:
      return "PIXEL_FORMAT_YUV422P10";
    case PIXEL_FORMAT_YUV444P9:
      return "PIXEL_FORMAT_YUV444P9";
    case PIXEL_FORMAT_YUV444P10:
      return "PIXEL_FORMAT_YUV444P10";
    case PIXEL_FORMAT_YUV420P12:
      return "PIXEL_FORMAT_YUV420P12";
    case PIXEL_FORMAT_YUV422P12:
      return "PIXEL_FORMAT_YUV422P12";
    case PIXEL_FORMAT_YUV444P12:
      return "PIXEL_FORMAT_YUV444P12";
    case PIXEL_FORMAT_Y16:
      return "PIXEL_FORMAT_Y16";
    case PIXEL_FORMAT_ABGR:
      return "PIXEL_FORMAT_ABGR";
    case PIXEL_FORMAT_XBGR:
      return "PIXEL_FORMAT_XBGR";
    case PIXEL_FORMAT_P016LE:
      return "PIXEL_FORMAT_P016LE";
    case PIXEL_FORMAT_XR30:
      return "PIXEL_FORMAT_XR30";
    case PIXEL_FORMAT_XB30:
      return "PIXEL_FORMAT_XB30";
    case PIXEL_FORMAT_BGRA:
      return "PIXEL_FORMAT_BGRA";
  }
  NOTREACHED() << "Invalid VideoPixelFormat provided: " << format;
  return "";
}

std::string FourccToString(uint32_t fourcc) {
  std::string result = "0000";
  for (size_t i = 0; i < 4; ++i, fourcc >>= 8) {
    const char c = static_cast<char>(fourcc & 0xFF);
    if (c <= 0x1f || c >= 0x7f)
      return base::StringPrintf("0x%x", fourcc);
    result[i] = c;
  }
  return result;
}

size_t BitDepth(VideoPixelFormat format) {
  switch (format) {
    case PIXEL_FORMAT_UNKNOWN:
      NOTREACHED();
      FALLTHROUGH;
    case PIXEL_FORMAT_I420:
    case PIXEL_FORMAT_YV12:
    case PIXEL_FORMAT_I422:
    case PIXEL_FORMAT_I420A:
    case PIXEL_FORMAT_I444:
    case PIXEL_FORMAT_NV12:
    case PIXEL_FORMAT_NV21:
    case PIXEL_FORMAT_YUY2:
    case PIXEL_FORMAT_ARGB:
    case PIXEL_FORMAT_XRGB:
    case PIXEL_FORMAT_RGB24:
    case PIXEL_FORMAT_MJPEG:
    case PIXEL_FORMAT_ABGR:
    case PIXEL_FORMAT_XBGR:
    case PIXEL_FORMAT_BGRA:
      return 8;
    case PIXEL_FORMAT_YUV420P9:
    case PIXEL_FORMAT_YUV422P9:
    case PIXEL_FORMAT_YUV444P9:
      return 9;
    case PIXEL_FORMAT_YUV420P10:
    case PIXEL_FORMAT_YUV422P10:
    case PIXEL_FORMAT_YUV444P10:
    case PIXEL_FORMAT_XR30:
    case PIXEL_FORMAT_XB30:
      return 10;
    case PIXEL_FORMAT_YUV420P12:
    case PIXEL_FORMAT_YUV422P12:
    case PIXEL_FORMAT_YUV444P12:
      return 12;
    case PIXEL_FORMAT_Y16:
    case PIXEL_FORMAT_P016LE:
      return 16;
  }
  NOTREACHED();
  return 0;
}

// If it is required to allocate aligned to multiple-of-two size overall for the
// frame of pixel |format|.
static bool RequiresEvenSizeAllocation(VideoPixelFormat format) {
  switch (format) {
    case PIXEL_FORMAT_ARGB:
    case PIXEL_FORMAT_XRGB:
    case PIXEL_FORMAT_RGB24:
    case PIXEL_FORMAT_Y16:
    case PIXEL_FORMAT_ABGR:
    case PIXEL_FORMAT_XBGR:
    case PIXEL_FORMAT_XR30:
    case PIXEL_FORMAT_XB30:
    case PIXEL_FORMAT_BGRA:
      return false;
    case PIXEL_FORMAT_NV12:
    case PIXEL_FORMAT_NV21:
    case PIXEL_FORMAT_I420:
    case PIXEL_FORMAT_MJPEG:
    case PIXEL_FORMAT_YUY2:
    case PIXEL_FORMAT_YV12:
    case PIXEL_FORMAT_I422:
    case PIXEL_FORMAT_I444:
    case PIXEL_FORMAT_YUV420P9:
    case PIXEL_FORMAT_YUV422P9:
    case PIXEL_FORMAT_YUV444P9:
    case PIXEL_FORMAT_YUV420P10:
    case PIXEL_FORMAT_YUV422P10:
    case PIXEL_FORMAT_YUV444P10:
    case PIXEL_FORMAT_YUV420P12:
    case PIXEL_FORMAT_YUV422P12:
    case PIXEL_FORMAT_YUV444P12:
    case PIXEL_FORMAT_I420A:
    case PIXEL_FORMAT_P016LE:
      return true;
    case PIXEL_FORMAT_UNKNOWN:
      break;
  }
  NOTREACHED() << "Unsupported video frame format: " << format;
  return false;
}

size_t NumPlanes(VideoPixelFormat format) {
  switch (format) {
    case PIXEL_FORMAT_YUY2:
    case PIXEL_FORMAT_ARGB:
    case PIXEL_FORMAT_BGRA:
    case PIXEL_FORMAT_XRGB:
    case PIXEL_FORMAT_RGB24:
    case PIXEL_FORMAT_MJPEG:
    case PIXEL_FORMAT_Y16:
    case PIXEL_FORMAT_ABGR:
    case PIXEL_FORMAT_XBGR:
    case PIXEL_FORMAT_XR30:
    case PIXEL_FORMAT_XB30:
      return 1;
    case PIXEL_FORMAT_NV12:
    case PIXEL_FORMAT_NV21:
    case PIXEL_FORMAT_P016LE:
      return 2;
    case PIXEL_FORMAT_I420:
    case PIXEL_FORMAT_YV12:
    case PIXEL_FORMAT_I422:
    case PIXEL_FORMAT_I444:
    case PIXEL_FORMAT_YUV420P9:
    case PIXEL_FORMAT_YUV422P9:
    case PIXEL_FORMAT_YUV444P9:
    case PIXEL_FORMAT_YUV420P10:
    case PIXEL_FORMAT_YUV422P10:
    case PIXEL_FORMAT_YUV444P10:
    case PIXEL_FORMAT_YUV420P12:
    case PIXEL_FORMAT_YUV422P12:
    case PIXEL_FORMAT_YUV444P12:
      return 3;
    case PIXEL_FORMAT_I420A:
      return 4;
    case PIXEL_FORMAT_UNKNOWN:
      // Note: PIXEL_FORMAT_UNKNOWN is used for end-of-stream frame.
      // Set its NumPlanes() to zero to avoid NOTREACHED().
      return 0;
  }
  NOTREACHED() << "Unsupported video frame format: " << format;
  return 0;
}

size_t AllocationSize(VideoPixelFormat format,
                                  const android::ui::Size& coded_size) {
  size_t total = 0;
  for (size_t i = 0; i < NumPlanes(format); ++i) {
      android::ui::Size plane_size = PlaneSize(format, i, coded_size);
      total += (plane_size.width * plane_size.height);
  }

  return total;
}

android::ui::Size PlaneSize(VideoPixelFormat format,
                           size_t plane,
                           const android::ui::Size& coded_size) {
  DCHECK(IsValidPlane(plane, format));

  int width = coded_size.width;
  int height = coded_size.height;
  if (RequiresEvenSizeAllocation(format)) {
    // Align to multiple-of-two size overall. This ensures that non-subsampled
    // planes can be addressed by pixel with the same scaling as the subsampled
    // planes.
    width = base::bits::Align(width, 2);
    height = base::bits::Align(height, 2);
  }

  const android::ui::Size subsample = SampleSize(format, plane);
  DCHECK(width % subsample.width == 0);
  DCHECK(height % subsample.height == 0);
  return android::ui::Size(BytesPerElement(format, plane) * width / subsample.width,
              height / subsample.height);
}

int PlaneHorizontalBitsPerPixel(VideoPixelFormat format,
                                            size_t plane) {
  DCHECK(IsValidPlane(plane, format));
  const int bits_per_element = 8 * BytesPerElement(format, plane);
  const int horiz_pixels_per_element = SampleSize(format, plane).width;
  DCHECK_EQ(bits_per_element % horiz_pixels_per_element, 0);
  return bits_per_element / horiz_pixels_per_element;
}

int PlaneBitsPerPixel(VideoPixelFormat format, size_t plane) {
  DCHECK(IsValidPlane(plane, format));
  return PlaneHorizontalBitsPerPixel(format, plane) /
         SampleSize(format, plane).height;
}

int BytesPerElement(VideoPixelFormat format, size_t plane) {
  DCHECK(IsValidPlane(format, plane));
  switch (format) {
    case PIXEL_FORMAT_ARGB:
    case PIXEL_FORMAT_BGRA:
    case PIXEL_FORMAT_XRGB:
    case PIXEL_FORMAT_ABGR:
    case PIXEL_FORMAT_XBGR:
    case PIXEL_FORMAT_XR30:
    case PIXEL_FORMAT_XB30:
      return 4;
    case PIXEL_FORMAT_RGB24:
      return 3;
    case PIXEL_FORMAT_Y16:
    case PIXEL_FORMAT_YUY2:
    case PIXEL_FORMAT_YUV420P9:
    case PIXEL_FORMAT_YUV422P9:
    case PIXEL_FORMAT_YUV444P9:
    case PIXEL_FORMAT_YUV420P10:
    case PIXEL_FORMAT_YUV422P10:
    case PIXEL_FORMAT_YUV444P10:
    case PIXEL_FORMAT_YUV420P12:
    case PIXEL_FORMAT_YUV422P12:
    case PIXEL_FORMAT_YUV444P12:
    case PIXEL_FORMAT_P016LE:
      return 2;
    case PIXEL_FORMAT_NV12:
    case PIXEL_FORMAT_NV21: {
      static const int bytes_per_element[] = {1, 2};
      DCHECK_LT(plane, base::size(bytes_per_element));
      return bytes_per_element[plane];
    }
    case PIXEL_FORMAT_YV12:
    case PIXEL_FORMAT_I420:
    case PIXEL_FORMAT_I422:
    case PIXEL_FORMAT_I420A:
    case PIXEL_FORMAT_I444:
      return 1;
    case PIXEL_FORMAT_MJPEG:
      return 0;
    case PIXEL_FORMAT_UNKNOWN:
      break;
  }
  NOTREACHED();
  return 0;
}

bool IsValidPlane(VideoPixelFormat format, size_t plane) {
  DCHECK_LE(NumPlanes(format), static_cast<size_t>(kMaxPlanes));
  return plane < NumPlanes(format);
}

android::ui::Size SampleSize(VideoPixelFormat format, size_t plane) {
  DCHECK(IsValidPlane(format, plane));

  switch (plane) {
    case kYPlane:  // and kARGBPlane:
    case kAPlane:
      return android::ui::Size(1, 1);

    case kUPlane:  // and kUVPlane:
    case kVPlane:
      switch (format) {
        case PIXEL_FORMAT_I444:
        case PIXEL_FORMAT_YUV444P9:
        case PIXEL_FORMAT_YUV444P10:
        case PIXEL_FORMAT_YUV444P12:
        case PIXEL_FORMAT_Y16:
          return android::ui::Size(1, 1);

        case PIXEL_FORMAT_I422:
        case PIXEL_FORMAT_YUV422P9:
        case PIXEL_FORMAT_YUV422P10:
        case PIXEL_FORMAT_YUV422P12:
          return android::ui::Size(2, 1);

        case PIXEL_FORMAT_YV12:
        case PIXEL_FORMAT_I420:
        case PIXEL_FORMAT_I420A:
        case PIXEL_FORMAT_NV12:
        case PIXEL_FORMAT_NV21:
        case PIXEL_FORMAT_YUV420P9:
        case PIXEL_FORMAT_YUV420P10:
        case PIXEL_FORMAT_YUV420P12:
        case PIXEL_FORMAT_P016LE:
          return android::ui::Size(2, 2);

        case PIXEL_FORMAT_UNKNOWN:
        case PIXEL_FORMAT_YUY2:
        case PIXEL_FORMAT_ARGB:
        case PIXEL_FORMAT_XRGB:
        case PIXEL_FORMAT_RGB24:
        case PIXEL_FORMAT_MJPEG:
        case PIXEL_FORMAT_ABGR:
        case PIXEL_FORMAT_XBGR:
        case PIXEL_FORMAT_XR30:
        case PIXEL_FORMAT_XB30:
        case PIXEL_FORMAT_BGRA:
          break;
      }
  }
  NOTREACHED();
  return android::ui::Size();
}

}  // namespace media

