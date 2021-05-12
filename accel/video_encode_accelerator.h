// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Note: ported from Chromium commit head: 9e40822e3a3d
// Note: only necessary functions are ported.

#ifndef MEDIA_VIDEO_VIDEO_ENCODE_ACCELERATOR_H_
#define MEDIA_VIDEO_VIDEO_ENCODE_ACCELERATOR_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/optional.h"
#include "base/time/time.h"

#include "size.h"
#include "video_codecs.h"

namespace media {

// Video encoder interface.
class VideoEncodeAccelerator {
 public:
  // Specification of an encoding profile supported by an encoder.
  struct SupportedProfile {
    SupportedProfile();
    SupportedProfile(VideoCodecProfile profile,
                     const Size& max_resolution,
                     uint32_t max_framerate_numerator = 0u,
                     uint32_t max_framerate_denominator = 1u);
    ~SupportedProfile();

    VideoCodecProfile profile;
    Size min_resolution;
    Size max_resolution;
    uint32_t max_framerate_numerator;
    uint32_t max_framerate_denominator;
  };
  using SupportedProfiles = std::vector<SupportedProfile>;
};

}  // namespace media

#endif  // MEDIA_VIDEO_VIDEO_ENCODE_ACCELERATOR_H_
