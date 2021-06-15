## General Information

### Scope of this document

This document is aimed to provide information about the v4l2\_codec2 project.
The target readers of this document are the developers and following maintainers
of this project, and the partners who are willing to use the V4L2 components.

### Introduction

v4l2\_codec2 project provides a component implementation of Codec2 framework,
the next-generation codec framework. The component implementation delegates the
request to the driver via V4L2 API.

## Quick Start Guide

### Prerequisites

*   Gralloc support for graphic buffer allocation
*   ION or Gralloc support for linear buffer allocation
*   DRM for buffer identification
*   [V4L2 stateful decoding API](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/dev-decoder.html)
*   [V4L2 encoding API](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/dev-encoder.html)
*   Widevine DRM for secure playback

### Enable V4L2 Components

Install the build package and files, and set the parameters in device.mk

```makefile
# Add the folder to the namespace.
PRODUCT_SOONG_NAMESPACES += external/v4l2_codec2

# Add the build target.
PRODUCT_PACKAGES += \
    android.hardware.media.c2@1.0-service-v4l2 \
    libc2plugin_store

# If a customized allocator is needed, then add this package.
# See more detail at "Customized allocator" section.
PRODUCT_PACKAGES += \
    libv4l2_codec2_vendor_allocator

# Install media_codecs_c2.xml.
# The destination is: /vendor/etc/media_codecs_c2.xml
PRODUCT_COPY_FILES += \
    <path_to_file>:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_c2.xml

# Set the customized property of v4l2_codec2, including:
# - The DRM device name and path pattern.
# - The maximum concurrent instances for decoder/encoder.
#   It should be the same as "concurrent-instances" at media_codec_c2.xml.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vendor.v4l2_codec2.drm_device_name=virtio_gpu \
    ro.vendor.v4l2_codec2.drm_device_path=/dev/dri/renderD* \
    ro.vendor.v4l2_codec2.decode_concurrent_instances=8 \
    ro.vendor.v4l2_codec2.encode_concurrent_instances=8

# Codec2.0 poolMask:
#   ION(16)
#   BUFFERQUEUE(18)
#   BLOB(19)
#   V4L2_BUFFERQUEUE(20)
#   V4L2_BUFFERPOOL(21)
#   SECURE_LINEAR(22)
#   SECURE_GRAPHIC(23)
#
# For linear buffer allocation:
#   If ION is chosen, then the mask should be 0xf50000
#   If BLOB is chosen, then the mask should be 0xfc0000
PRODUCT_PROPERTY_OVERRIDES += \
    debug.stagefright.c2-poolmask=0xf50000

# Install extended policy for codec2.
# The destination is: /vendor/etc/seccomp_policy/codec2.vendor.ext.policy
PRODUCT_COPY_FILES += \
    <path_to_policy>:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/codec2.vendor.ext.policy
```

Enable codec2 hidl in manifest.xml

```xml
<manifest version="1.0" type="device">
    <hal format="hidl">
        <name>android.hardware.media.c2</name>
        <transport>hwbinder</transport>
        <version>1.0</version>
        <interface>
            <name>IComponentStore</name>
            <instance>default</instance>
        </interface>
        <interface>
            <name>IConfigurable</name>
            <instance>default</instance>
        </interface>
    </hal>
</manifest>
```

Add decode and encode components in media\_codecs\_c2.xml

```xml
<?xml version="1.0" encoding="utf-8" ?>
<MediaCodecs>
   <Encoders>
       <MediaCodec name="c2.v4l2.avc.encoder" type="video/avc">
           <Limit name="size" min="32x32" max="4096x4096" />
           <Limit name="alignment" value="2x2" />
           <Limit name="block-size" value="16x16" />
           <Limit name="blocks-per-second" range="1-983040" />
           <Limit name="bitrate" range="1-40000000" />
           <Limit name="concurrent-instances" max="8" />
           <Limit name="performance-point-1280x720" range="30-30" />
       </MediaCodec>
   </Encoders>

   <Decoders>
       <MediaCodec name="c2.v4l2.avc.decoder" type="video/avc" >
           <Limit name="size" min="16x16" max="4096x4096" />
           <Limit name="alignment" value="2x2" />
           <Limit name="block-size" value="16x16" />
           <Limit name="blocks-per-second" min="1" max="1879200" />
           <Limit name="bitrate" range="1-62500000" />
           <Limit name="concurrent-instances" max="8" />
           <Limit name="performance-point-3840x2160" range="30-30" />
           <Feature name="adaptive-playback" />
       </MediaCodec>

       <MediaCodec name="c2.v4l2.vp8.decoder" type="video/x-vnd.on2.vp8" >
           <Limit name="size" min="16x16" max="4096x4096" />
           <Limit name="alignment" value="2x2" />
           <Limit name="block-size" value="16x16" />
           <Limit name="blocks-per-second" min="1" max="1984500" />
           <Limit name="bitrate" range="1-62500000" />
           <Limit name="concurrent-instances" max="8" />
           <Limit name="performance-point-3840x2160" range="30-30" />
           <Feature name="adaptive-playback" />
       </MediaCodec>

       <MediaCodec name="c2.v4l2.vp9.decoder" type="video/x-vnd.on2.vp9" >
           <Limit name="size" min="16x16" max="4096x4096" />
           <Limit name="alignment" value="2x2" />
           <Limit name="block-size" value="16x16" />
           <Limit name="blocks-per-second" min="1" max="2073600" />
           <Limit name="bitrate" range="1-62500000" />
           <Limit name="concurrent-instances" max="8" />
           <Limit name="performance-point-3840x2160" range="30-30" />
           <Feature name="adaptive-playback" />
       </MediaCodec>

       <MediaCodec name="c2.v4l2.avc.decoder.secure" type="video/avc" >
           <Limit name="size" min="16x16" max="4096x4096" />
           <Limit name="alignment" value="2x2" />
           <Limit name="block-size" value="16x16" />
           <Limit name="blocks-per-second" min="1" max="1879200" />
           <Limit name="bitrate" range="1-62500000" />
           <Limit name="concurrent-instances" max="8" />
           <Limit name="performance-point-3840x2160" range="30-30" />
           <Feature name="adaptive-playback" />
           <Feature name="secure-playback" required="true" />
       </MediaCodec>

       <MediaCodec name="c2.v4l2.vp8.decoder.secure" type="video/x-vnd.on2.vp8" >
           <Limit name="size" min="16x16" max="4096x4096" />
           <Limit name="alignment" value="2x2" />
           <Limit name="block-size" value="16x16" />
           <Limit name="blocks-per-second" min="1" max="1984500" />
           <Limit name="bitrate" range="1-62500000" />
           <Limit name="concurrent-instances" max="8" />
           <Limit name="performance-point-3840x2160" range="30-30" />
           <Feature name="adaptive-playback" />
           <Feature name="secure-playback" required="true" />
       </MediaCodec>

       <MediaCodec name="c2.v4l2.vp9.decoder.secure" type="video/x-vnd.on2.vp9" >
           <Limit name="size" min="16x16" max="4096x4096" />
           <Limit name="alignment" value="2x2" />
           <Limit name="block-size" value="16x16" />
           <Limit name="blocks-per-second" min="1" max="2073600" />
           <Limit name="bitrate" range="1-62500000" />
           <Limit name="concurrent-instances" max="8" />
           <Limit name="performance-point-3840x2160" range="30-30" />
           <Feature name="adaptive-playback" />
           <Feature name="secure-playback" required="true" />
       </MediaCodec>
   </Decoders>
</MediaCodecs>
```

Set SELinux file policy in sepolicy/file\_contexts

```
/vendor/bin/hw/android\.hardware\.media\.c2@1\.0-service-v4l2(.*)?  u:object_r:mediacodec_exec:s0
```

Add additional permission in codec2.vendor.ext.policy

```
_llseek: 1
epoll_create1: 1
epoll_ctl: 1
epoll_pwait: 1
eventfd2: 1
fstat64: 1
fstatat64: 1
fstatfs64: 1
getcwd: 1
getdents64: 1
getuid32: 1
mmap2: 1
pselect6: 1
statfs64: 1
sysinfo: 1
ugetrlimit: 1
```

Set file permission in ueventd.rc

```
/dev/video*    0600   media      media
```

### Customized Allocator

The default allocator of the decoder's output buffer is C2AllocatorGralloc.
However, C2AllocatorGralloc might not fit the requirement for secure playback.
In this case, we can implement a customized C2Allocator, and load it at
run-time. Please create a library `libv4l2_codec2_vendor_allocator`, and export
a function `CreateVendorAllocator() `for creating the customized C2Allocator.
Here is an example below.

#### Example of Android.bp

```json
cc_library_shared {
    name: "libv4l2_codec2_vendor_allocator",
    vendor: true,

    defaults: [
        "libcodec2-impl-defaults",
    ],

    srcs: [
        "C2VendorAllocatorFactory.cpp",
    ],

    shared_libs: [
        "libc2plugin_store",
    ],
}
```

#### Example of C2VendorAllocatorFactory.cpp

```cpp
//#define LOG_NDEBUG 0
#define LOG_TAG "C2VendorAllocatorFactory"

#include <C2AllocatorGralloc.h>
#include <utils/Log.h>
#include <v4l2_codec2/plugin_store/V4L2AllocatorId.h>

namespace android {

::C2Allocator* CreateVendorAllocator(::C2Allocator::id_t allocatorId) {
    ALOGV("%s(%d)", __func__, allocatorId);

    // Change to create vendor-customized implementation.
    switch (allocatorId) {
    case V4L2AllocatorId::V4L2_BUFFERQUEUE:
        return new C2AllocatorGralloc(V4L2AllocatorId::V4L2_BUFFERQUEUE, true);
    case V4L2AllocatorId::V4L2_BUFFERPOOL:
        return new C2AllocatorGralloc(V4L2AllocatorId::V4L2_BUFFERPOOL, true);
    case V4L2AllocatorId::SECURE_LINEAR:
        return new C2AllocatorGralloc(V4L2AllocatorId::SECURE_LINEAR, true);
    case V4L2AllocatorId::SECURE_GRAPHIC:
        return new C2AllocatorGralloc(V4L2AllocatorId::SECURE_GRAPHIC, true);
    default:
        ALOGE("%s(): Unknown allocator ID: %d", __func__, allocatorId);
    }
    return nullptr;
}

}  // namespace android

extern "C" ::C2Allocator* CreateAllocator(::C2Allocator::id_t allocatorId) {
    return ::android::CreateVendorAllocator(allocatorId);
}
```

