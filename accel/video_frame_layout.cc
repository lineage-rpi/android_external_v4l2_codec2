// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Note: ported from Chromium commit head: 3b7ce92816e2

#include "video_frame_layout.h"

#include <string.h>
#include <numeric>
#include <sstream>

#include "base/logging.h"

namespace media {

namespace {

template <class T>
std::string VectorToString(const std::vector<T>& vec) {
  std::ostringstream result;
  std::string delim;
  result << "[";
  for (auto& v : vec) {
    result << delim;
    result << v;
    if (delim.size() == 0)
      delim = ", ";
  }
  result << "]";
  return result.str();
}

std::vector<ColorPlaneLayout> PlanesFromStrides(
    const std::vector<int32_t> strides) {
  std::vector<ColorPlaneLayout> planes(strides.size());
  for (size_t i = 0; i < strides.size(); i++) {
    planes[i].stride = strides[i];
  }
  return planes;
}

}  // namespace

// static
std::optional<VideoFrameLayout> VideoFrameLayout::Create(
    VideoPixelFormat format,
    const android::ui::Size& coded_size) {
  return CreateWithStrides(format, coded_size,
                           std::vector<int32_t>(NumPlanes(format), 0));
}

// static
std::optional<VideoFrameLayout> VideoFrameLayout::CreateWithStrides(
    VideoPixelFormat format,
    const android::ui::Size& coded_size,
    std::vector<int32_t> strides) {
  return CreateWithPlanes(format, coded_size, PlanesFromStrides(strides));
}

// static
std::optional<VideoFrameLayout> VideoFrameLayout::CreateWithPlanes(
    VideoPixelFormat format,
    const android::ui::Size& coded_size,
    std::vector<ColorPlaneLayout> planes,
    size_t buffer_addr_align,
    uint64_t modifier) {
  // NOTE: Even if format is UNKNOWN, it is valid if coded_sizes is not Empty().
  // TODO(crbug.com/896135): Return base::nullopt,
  // if (format != PIXEL_FORMAT_UNKNOWN || !coded_sizes.IsEmpty())
  // TODO(crbug.com/896135): Return base::nullopt,
  // if (planes.size() != NumPlanes(format))
  return VideoFrameLayout(format, coded_size, std::move(planes),
                          false /*is_multi_planar */, buffer_addr_align,
                          modifier);
}

std::optional<VideoFrameLayout> VideoFrameLayout::CreateMultiPlanar(
    VideoPixelFormat format,
    const android::ui::Size& coded_size,
    std::vector<ColorPlaneLayout> planes,
    size_t buffer_addr_align,
    uint64_t modifier) {
  // NOTE: Even if format is UNKNOWN, it is valid if coded_sizes is not Empty().
  // TODO(crbug.com/896135): Return base::nullopt,
  // if (format != PIXEL_FORMAT_UNKNOWN || !coded_sizes.IsEmpty())
  // TODO(crbug.com/896135): Return base::nullopt,
  // if (planes.size() != NumPlanes(format))
  return VideoFrameLayout(format, coded_size, std::move(planes),
                          true /*is_multi_planar */, buffer_addr_align,
                          modifier);
}

VideoFrameLayout::VideoFrameLayout(VideoPixelFormat format,
                                   const android::ui::Size& coded_size,
                                   std::vector<ColorPlaneLayout> planes,
                                   bool is_multi_planar,
                                   size_t buffer_addr_align,
                                   uint64_t modifier)
    : format_(format),
      coded_size_(coded_size),
      planes_(std::move(planes)),
      is_multi_planar_(is_multi_planar),
      buffer_addr_align_(buffer_addr_align),
      modifier_(modifier) {}

VideoFrameLayout::~VideoFrameLayout() = default;
VideoFrameLayout::VideoFrameLayout(const VideoFrameLayout&) = default;
VideoFrameLayout::VideoFrameLayout(VideoFrameLayout&&) = default;
VideoFrameLayout& VideoFrameLayout::operator=(const VideoFrameLayout&) =
    default;

bool VideoFrameLayout::operator==(const VideoFrameLayout& rhs) const {
  return format_ == rhs.format_ && coded_size_ == rhs.coded_size_ &&
         planes_ == rhs.planes_ && is_multi_planar_ == rhs.is_multi_planar_ &&
         buffer_addr_align_ == rhs.buffer_addr_align_ &&
         modifier_ == rhs.modifier_;
}

bool VideoFrameLayout::operator!=(const VideoFrameLayout& rhs) const {
  return !(*this == rhs);
}

std::ostream& operator<<(std::ostream& ostream,
                         const VideoFrameLayout& layout) {
  ostream << "VideoFrameLayout(format: " << layout.format()
          << ", coded_size: " << layout.coded_size().width << "x"
          << layout.coded_size().height
          << ", planes (stride, offset, size): "
          << VectorToString(layout.planes())
          << ", is_multi_planar: " << layout.is_multi_planar()
          << ", buffer_addr_align: " << layout.buffer_addr_align()
          << ", modifier: " << layout.modifier() << ")";
  return ostream;
}

}  // namespace media
