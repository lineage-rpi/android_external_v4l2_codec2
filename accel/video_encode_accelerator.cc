// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Note: ported from Chromium commit head: 9e40822e3a3d
// Note: only necessary functions are ported.

#include "video_encode_accelerator.h"

namespace media {

VideoEncodeAccelerator::SupportedProfile::SupportedProfile()
    : profile(media::VIDEO_CODEC_PROFILE_UNKNOWN),
      max_framerate_numerator(0),
      max_framerate_denominator(0) {}

VideoEncodeAccelerator::SupportedProfile::SupportedProfile(
    VideoCodecProfile profile,
    const Size& max_resolution,
    uint32_t max_framerate_numerator,
    uint32_t max_framerate_denominator)
    : profile(profile),
      max_resolution(max_resolution),
      max_framerate_numerator(max_framerate_numerator),
      max_framerate_denominator(max_framerate_denominator) {}

VideoEncodeAccelerator::SupportedProfile::~SupportedProfile() = default;

}  // namespace media
