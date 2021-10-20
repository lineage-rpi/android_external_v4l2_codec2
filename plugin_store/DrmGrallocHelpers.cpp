// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//#define LOG_NDEBUG 0
#define LOG_TAG "DrmGrallocHelper"

#include <v4l2_codec2/plugin_store/DrmGrallocHelpers.h>

#include <fcntl.h>
#include <glob.h>

#include <string>
#include <vector>

#include <cutils/properties.h>
#include <drm/drm.h>
#include <log/log.h>

namespace android {
namespace {

// Return a list of paths that matches |pattern|, the unix style pathname pattern.
std::vector<std::string> glob(const std::string& pattern) {
    glob_t glob_result;
    memset(&glob_result, 0, sizeof(glob_result));

    std::vector<std::string> paths;
    if (glob(pattern.c_str(), GLOB_ERR | GLOB_NOESCAPE, NULL, &glob_result) == 0) {
        for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
            paths.emplace_back(glob_result.gl_pathv[i]);
        }
    }

    globfree(&glob_result);
    return paths;
}

std::optional<std::string> propertyGetString(const char* key) {
    char buf[PROPERTY_VALUE_MAX];
    int len = property_get(key, buf, nullptr);
    if (len == 0) {
        return std::nullopt;
    }
    return std::string(buf, len);
}

}  // namespace

std::optional<int> openRenderFd() {
    constexpr char kDevNamePropertyKey[] = "ro.vendor.v4l2_codec2.drm_device_name";
    constexpr char kDevPathPropertyKey[] = "ro.vendor.v4l2_codec2.drm_device_path";

    const auto devName = propertyGetString(kDevNamePropertyKey);
    if (!devName) {
        ALOGE("Failed to get DRM device name from Android property");
        return std::nullopt;
    }

    const auto devPath = propertyGetString(kDevPathPropertyKey).value_or("/dev/dri/renderD*");
    for (const auto filePath : glob(devPath)) {
        int fd = open(filePath.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }

        char name[32];
        struct drm_version v;
        memset(&v, 0, sizeof(v));
        v.name = name;
        v.name_len = sizeof(name);

        if (ioctl(fd, static_cast<int>(DRM_IOCTL_VERSION), &v)) {
            close(fd);
            continue;
        }
        if (devName->size() != v.name_len || *devName != name) {
            close(fd);
            continue;
        }
        return fd;
    }
    return std::nullopt;
}

std::optional<uint32_t> getDrmHandle(int renderFd, int primeFd) {
    ALOGV("%s(renderFd=%d, primeFd=%u)", __func__, renderFd, primeFd);

    struct drm_prime_handle prime;
    memset(&prime, 0, sizeof(prime));
    prime.fd = primeFd;

    if (ioctl(renderFd, static_cast<int>(DRM_IOCTL_PRIME_FD_TO_HANDLE), &prime)) {
        ALOGE("Can't translate prime fd %d to handle", prime.fd);
        return std::nullopt;
    }
    return prime.handle;
}

void closeDrmHandle(int renderFd, uint32_t handle) {
    ALOGV("%s(renderFd=%d, handle=%u)", __func__, renderFd, handle);

    struct drm_gem_close gem;
    memset(&gem, 0, sizeof(gem));
    gem.handle = handle;

    ioctl(renderFd, DRM_IOCTL_GEM_CLOSE, &gem);
}

}  // namespace android
