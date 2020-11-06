// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//#define LOG_NDEBUG 0
#define LOG_TAG "V4L2ComponentFactory"

#include <string>

#include <C2ComponentFactory.h>
#include <SimpleC2Interface.h>
#include <codec2/hidl/1.0/InputBufferManager.h>
#include <log/log.h>
#include <util/C2InterfaceHelper.h>

#include <v4l2_codec2/common/V4L2ComponentCommon.h>
#include <v4l2_codec2/components/V4L2DecodeComponent.h>
#include <v4l2_codec2/components/V4L2DecodeInterface.h>
#include <v4l2_codec2/components/V4L2EncodeComponent.h>
#include <v4l2_codec2/components/V4L2EncodeInterface.h>
#include <v4l2_codec2/store/V4L2ComponentStore.h>

namespace android {

class V4L2ComponentFactory : public C2ComponentFactory {
public:
    V4L2ComponentFactory(const char* componentName, bool isEncoder);
    ~V4L2ComponentFactory() override;

    // Implementation of C2ComponentFactory.
    c2_status_t createComponent(c2_node_id_t id, std::shared_ptr<C2Component>* const component,
                                ComponentDeleter deleter) override;
    c2_status_t createInterface(c2_node_id_t id,
                                std::shared_ptr<C2ComponentInterface>* const interface,
                                InterfaceDeleter deleter) override;

private:
    const std::string mComponentName;
    const bool mIsEncoder;
    std::shared_ptr<C2ReflectorHelper> mReflector;
};

V4L2ComponentFactory::V4L2ComponentFactory(const char* componentName, bool isEncoder)
      : mComponentName(componentName), mIsEncoder(isEncoder) {
    auto componentStore = V4L2ComponentStore::Create();
    if (componentStore == nullptr) {
        ALOGE("Could not create V4L2ComponentStore.");
        return;
    }
    mReflector = std::static_pointer_cast<C2ReflectorHelper>(componentStore->getParamReflector());

    {
        using namespace ::android::hardware::media::c2::V1_0;
        // To minimize IPC, we generally want the codec2 framework to release and
        // recycle input buffers when the corresponding work item is done. However,
        // sometimes it is necessary to provide more input to unblock a decoder.
        //
        // Optimally we would configure this on a per-context basis. However, the
        // InputBufferManager is a process-wide singleton, so we need to configure it
        // pessimistically. Basing the interval on frame timing can be suboptimal if
        // the decoded output isn't being displayed, but that's not a primary use case
        // and few videos will actually rely on this behavior.
        constexpr nsecs_t kMinFrameIntervalNs = 1000000000ull / 60;
        uint32_t delayCount = 0;
        for (auto c : kAllCodecs) {
            delayCount = std::max(delayCount, V4L2DecodeInterface::getOutputDelay(c));
        }
        utils::InputBufferManager::setNotificationInterval(delayCount * kMinFrameIntervalNs / 2);
    }
}

V4L2ComponentFactory::~V4L2ComponentFactory() = default;

c2_status_t V4L2ComponentFactory::createComponent(c2_node_id_t id,
                                                  std::shared_ptr<C2Component>* const component,
                                                  ComponentDeleter deleter) {
    ALOGV("%s(%d), componentName: %s, isEncoder: %d", __func__, id, mComponentName.c_str(),
          mIsEncoder);

    if (mReflector == nullptr) {
        ALOGE("mReflector doesn't exist.");
        return C2_CORRUPTED;
    }

    if (mIsEncoder) {
        *component = V4L2EncodeComponent::create(mComponentName, id, mReflector, deleter);
    } else {
        *component = V4L2DecodeComponent::create(mComponentName, id, mReflector, deleter);
    }
    return *component ? C2_OK : C2_BAD_VALUE;
}

c2_status_t V4L2ComponentFactory::createInterface(
        c2_node_id_t id, std::shared_ptr<C2ComponentInterface>* const interface,
        InterfaceDeleter deleter) {
    ALOGV("%s(), componentName: %s", __func__, mComponentName.c_str());

    if (mReflector == nullptr) {
        ALOGE("mReflector doesn't exist.");
        return C2_CORRUPTED;
    }

    if (mIsEncoder) {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new SimpleInterface<V4L2EncodeInterface>(
                        mComponentName.c_str(), id,
                        std::make_shared<V4L2EncodeInterface>(mComponentName, mReflector)),
                deleter);
        return C2_OK;
    } else {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new SimpleInterface<V4L2DecodeInterface>(
                        mComponentName.c_str(), id,
                        std::make_shared<V4L2DecodeInterface>(mComponentName, mReflector)),
                deleter);
        return C2_OK;
    }
}

}  // namespace android

__attribute__((cfi_canonical_jump_table)) extern "C" ::C2ComponentFactory* CreateCodec2Factory(
        const char* componentName) {
    ALOGV("%s(%s)", __func__, componentName);

    if (!android::V4L2ComponentName::isValid(componentName)) {
        ALOGE("Invalid component name: %s", componentName);
        return nullptr;
    }

    bool isEncoder = android::V4L2ComponentName::isEncoder(componentName);
    return new android::V4L2ComponentFactory(componentName, isEncoder);
}

__attribute__((cfi_canonical_jump_table)) extern "C" void DestroyCodec2Factory(
        ::C2ComponentFactory* factory) {
    ALOGV("%s()", __func__);
    delete factory;
}
