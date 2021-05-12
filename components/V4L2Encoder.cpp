// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//#define LOG_NDEBUG 0
#define LOG_TAG "V4L2Encoder"

#include <v4l2_codec2/components/V4L2Encoder.h>

#include <stdint.h>
#include <optional>
#include <vector>

#include <base/bind.h>
#include <base/files/scoped_file.h>
#include <base/memory/ptr_util.h>
#include <log/log.h>

#include <rect.h>
#include <v4l2_codec2/components/BitstreamBuffer.h>
#include <v4l2_device.h>

namespace android {

namespace {

const media::VideoPixelFormat kInputPixelFormat = media::VideoPixelFormat::PIXEL_FORMAT_NV12;

// The maximum size for output buffer, which is chosen empirically for a 1080p video.
constexpr size_t kMaxBitstreamBufferSizeInBytes = 2 * 1024 * 1024;  // 2MB
// The frame size for 1080p (FHD) video in pixels.
constexpr int k1080PSizeInPixels = 1920 * 1080;
// The frame size for 1440p (QHD) video in pixels.
constexpr int k1440PSizeInPixels = 2560 * 1440;

// Use quadruple size of kMaxBitstreamBufferSizeInBytes when the input frame size is larger than
// 1440p, double if larger than 1080p. This is chosen empirically for some 4k encoding use cases and
// the Android CTS VideoEncoderTest (crbug.com/927284).
size_t GetMaxOutputBufferSize(const media::Size& size) {
    if (size.GetArea() > k1440PSizeInPixels) return kMaxBitstreamBufferSizeInBytes * 4;
    if (size.GetArea() > k1080PSizeInPixels) return kMaxBitstreamBufferSizeInBytes * 2;
    return kMaxBitstreamBufferSizeInBytes;
}

// Define V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR control code if not present in header files.
#ifndef V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR
#define V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR (V4L2_CID_MPEG_BASE + 644)
#endif

}  // namespace

// static
std::unique_ptr<VideoEncoder> V4L2Encoder::create(
        media::VideoCodecProfile outputProfile, std::optional<uint8_t> level,
        const media::Size& visibleSize, uint32_t stride, uint32_t keyFramePeriod,
        FetchOutputBufferCB fetchOutputBufferCb, InputBufferDoneCB inputBufferDoneCb,
        OutputBufferDoneCB outputBufferDoneCb, DrainDoneCB drainDoneCb, ErrorCB errorCb,
        scoped_refptr<::base::SequencedTaskRunner> taskRunner) {
    ALOGV("%s()", __func__);

    std::unique_ptr<V4L2Encoder> encoder = ::base::WrapUnique<V4L2Encoder>(new V4L2Encoder(
            std::move(taskRunner), std::move(fetchOutputBufferCb), std::move(inputBufferDoneCb),
            std::move(outputBufferDoneCb), std::move(drainDoneCb), std::move(errorCb)));
    if (!encoder->initialize(outputProfile, level, visibleSize, stride, keyFramePeriod)) {
        return nullptr;
    }
    return encoder;
}

V4L2Encoder::V4L2Encoder(scoped_refptr<::base::SequencedTaskRunner> taskRunner,
                         FetchOutputBufferCB fetchOutputBufferCb,
                         InputBufferDoneCB inputBufferDoneCb, OutputBufferDoneCB outputBufferDoneCb,
                         DrainDoneCB drainDoneCb, ErrorCB errorCb)
      : mFetchOutputBufferCb(fetchOutputBufferCb),
        mInputBufferDoneCb(inputBufferDoneCb),
        mOutputBufferDoneCb(outputBufferDoneCb),
        mDrainDoneCb(std::move(drainDoneCb)),
        mErrorCb(std::move(errorCb)),
        mTaskRunner(std::move(taskRunner)) {
    ALOGV("%s()", __func__);

    mWeakThis = mWeakThisFactory.GetWeakPtr();
}

V4L2Encoder::~V4L2Encoder() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    mWeakThisFactory.InvalidateWeakPtrs();

    // Flushing the encoder will stop polling and streaming on the V4L2 device queues.
    flush();

    // Deallocate all V4L2 device input and output buffers.
    destroyInputBuffers();
    destroyOutputBuffers();
}

bool V4L2Encoder::encode(std::unique_ptr<InputFrame> frame) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(mState != State::UNINITIALIZED);

    // If we're in the error state we can immediately return, freeing the input buffer.
    if (mState == State::ERROR) {
        return false;
    }

    if (!frame) {
        ALOGW("Empty encode request scheduled");
        return false;
    }

    mEncodeRequests.push(EncodeRequest(std::move(frame)));

    // If we were waiting for encode requests, start encoding again.
    if (mState == State::WAITING_FOR_INPUT_FRAME) {
        setState(State::ENCODING);
        mTaskRunner->PostTask(FROM_HERE,
                              ::base::BindOnce(&V4L2Encoder::handleEncodeRequest, mWeakThis));
    }

    return true;
}

void V4L2Encoder::drain() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    // We can only start draining if all the requests in our input queue has been queued on the V4L2
    // device input queue, so we mark the last item in the input queue as EOS.
    if (!mEncodeRequests.empty()) {
        ALOGV("Marking last item (index: %" PRIu64 ") in encode request queue as EOS",
              mEncodeRequests.back().video_frame->index());
        mEncodeRequests.back().end_of_stream = true;
        return;
    }

    // Start a drain operation on the device. If no buffers are currently queued the device will
    // return an empty buffer with the V4L2_BUF_FLAG_LAST flag set.
    handleDrainRequest();
}

void V4L2Encoder::flush() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    handleFlushRequest();
}

bool V4L2Encoder::setBitrate(uint32_t bitrate) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    if (!mDevice->SetExtCtrls(V4L2_CTRL_CLASS_MPEG,
                              {media::V4L2ExtCtrl(V4L2_CID_MPEG_VIDEO_BITRATE, bitrate)})) {
        ALOGE("Setting bitrate to %u failed", bitrate);
        return false;
    }
    return true;
}

bool V4L2Encoder::setFramerate(uint32_t framerate) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    struct v4l2_streamparm parms;
    memset(&parms, 0, sizeof(v4l2_streamparm));
    parms.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    parms.parm.output.timeperframe.numerator = 1;
    parms.parm.output.timeperframe.denominator = framerate;
    if (mDevice->Ioctl(VIDIOC_S_PARM, &parms) != 0) {
        ALOGE("Setting framerate to %u failed", framerate);
        return false;
    }
    return true;
}

void V4L2Encoder::requestKeyframe() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    mKeyFrameCounter = 0;
}

media::VideoPixelFormat V4L2Encoder::inputFormat() const {
    return mInputLayout ? mInputLayout.value().format()
                        : media::VideoPixelFormat::PIXEL_FORMAT_UNKNOWN;
}

bool V4L2Encoder::initialize(media::VideoCodecProfile outputProfile, std::optional<uint8_t> level,
                             const media::Size& visibleSize, uint32_t stride,
                             uint32_t keyFramePeriod) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(keyFramePeriod > 0);

    mVisibleSize = visibleSize;
    mKeyFramePeriod = keyFramePeriod;
    mKeyFrameCounter = 0;

    // Open the V4L2 device for encoding to the requested output format.
    // TODO(dstaessens): Avoid conversion to VideoCodecProfile and use C2Config::profile_t directly.
    uint32_t outputPixelFormat =
            media::V4L2Device::VideoCodecProfileToV4L2PixFmt(outputProfile, false);
    if (!outputPixelFormat) {
        ALOGE("Invalid output profile %s", media::GetProfileName(outputProfile).c_str());
        return false;
    }

    mDevice = media::V4L2Device::Create();
    if (!mDevice) {
        ALOGE("Failed to create V4L2 device");
        return false;
    }

    if (!mDevice->Open(media::V4L2Device::Type::kEncoder, outputPixelFormat)) {
        ALOGE("Failed to open device for profile %s (%s)",
              media::GetProfileName(outputProfile).c_str(),
              media::FourccToString(outputPixelFormat).c_str());
        return false;
    }

    // Make sure the device has all required capabilities (multi-planar Memory-To-Memory and
    // streaming I/O), and whether flushing is supported.
    if (!mDevice->HasCapabilities(V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING)) {
        ALOGE("Device doesn't have the required capabilities");
        return false;
    }
    if (!mDevice->IsCommandSupported(V4L2_ENC_CMD_STOP)) {
        ALOGE("Device does not support flushing (V4L2_ENC_CMD_STOP)");
        return false;
    }

    // Get input/output queues so we can send encode request to the device and get back the results.
    mInputQueue = mDevice->GetQueue(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
    mOutputQueue = mDevice->GetQueue(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
    if (!mInputQueue || !mOutputQueue) {
        ALOGE("Failed to get V4L2 device queues");
        return false;
    }

    // First try to configure the specified output format, as changing the output format can affect
    // the configured input format.
    if (!configureOutputFormat(outputProfile)) return false;

    // Configure the input format. If the device doesn't support the specified format we'll use one
    // of the device's preferred formats in combination with an input format convertor.
    if (!configureInputFormat(kInputPixelFormat, stride)) return false;

    // Create input and output buffers.
    if (!createInputBuffers() || !createOutputBuffers()) return false;

    // Configure the device, setting all required controls.
    if (!configureDevice(outputProfile, level)) return false;

    // We're ready to start encoding now.
    setState(State::WAITING_FOR_INPUT_FRAME);
    return true;
}

void V4L2Encoder::handleEncodeRequest() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(mState == State::ENCODING || mState == State::ERROR);

    // If we're in the error state we can immediately return.
    if (mState == State::ERROR) {
        return;
    }

    // It's possible we flushed the encoder since this function was scheduled.
    if (mEncodeRequests.empty()) {
        return;
    }

    // Get the next encode request from the queue.
    EncodeRequest& encodeRequest = mEncodeRequests.front();

    // Check if the device has free input buffers available. If not we'll switch to the
    // WAITING_FOR_INPUT_BUFFERS state, and resume encoding once we've dequeued an input buffer.
    // Note: The input buffers are not copied into the device's input buffers, but rather a memory
    // pointer is imported. We still have to throttle the number of enqueues queued simultaneously
    // on the device however.
    if (mInputQueue->FreeBuffersCount() == 0) {
        ALOGV("Waiting for device to return input buffers");
        setState(State::WAITING_FOR_V4L2_BUFFER);
        return;
    }

    // Request the next frame to be a key frame each time the counter reaches 0.
    if (mKeyFrameCounter == 0) {
        if (!mDevice->SetExtCtrls(V4L2_CTRL_CLASS_MPEG,
                                  {media::V4L2ExtCtrl(V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME)})) {
            ALOGE("Failed requesting key frame");
            onError();
            return;
        }
    }
    mKeyFrameCounter = (mKeyFrameCounter + 1) % mKeyFramePeriod;

    // Enqueue the input frame in the V4L2 device.
    uint64_t index = encodeRequest.video_frame->index();
    uint64_t timestamp = encodeRequest.video_frame->timestamp();
    bool end_of_stream = encodeRequest.end_of_stream;
    if (!enqueueInputBuffer(std::move(encodeRequest.video_frame))) {
        ALOGE("Failed to enqueue input frame (index: %" PRIu64 ", timestamp: %" PRId64 ")", index,
              timestamp);
        onError();
        return;
    }
    mEncodeRequests.pop();

    // Start streaming and polling on the input and output queue if required.
    if (!mInputQueue->IsStreaming()) {
        ALOG_ASSERT(!mOutputQueue->IsStreaming());
        if (!mOutputQueue->Streamon() || !mInputQueue->Streamon()) {
            ALOGE("Failed to start streaming on input and output queue");
            onError();
            return;
        }
        startDevicePoll();
    }

    // Queue buffers on output queue. These buffers will be used to store the encoded bitstream.
    while (mOutputQueue->FreeBuffersCount() > 0) {
        if (!enqueueOutputBuffer()) return;
    }

    // Drain the encoder if requested.
    if (end_of_stream) {
        handleDrainRequest();
        return;
    }

    if (mEncodeRequests.empty()) {
        setState(State::WAITING_FOR_INPUT_FRAME);
        return;
    }

    // Schedule the next buffer to be encoded.
    mTaskRunner->PostTask(FROM_HERE,
                          ::base::BindOnce(&V4L2Encoder::handleEncodeRequest, mWeakThis));
}

void V4L2Encoder::handleFlushRequest() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    // Stop the device poll thread.
    stopDevicePoll();

    // Stop streaming on the V4L2 device, which stops all currently queued encode operations and
    // releases all buffers currently in use by the device.
    for (auto& queue : {mInputQueue, mOutputQueue}) {
        if (queue && queue->IsStreaming() && !queue->Streamoff()) {
            ALOGE("Failed to stop streaming on the device queue");
            onError();
        }
    }

    // Clear all outstanding encode requests and references to input and output queue buffers.
    while (!mEncodeRequests.empty()) {
        mEncodeRequests.pop();
    }
    for (auto& buf : mInputBuffers) {
        buf = nullptr;
    }
    for (auto& buf : mOutputBuffers) {
        buf = nullptr;
    }

    // Streaming and polling on the V4L2 device input and output queues will be resumed once new
    // encode work is queued.
    if (mState != State::ERROR) {
        setState(State::WAITING_FOR_INPUT_FRAME);
    }
}

void V4L2Encoder::handleDrainRequest() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    if (mState == State::DRAINING || mState == State::ERROR) {
        return;
    }

    setState(State::DRAINING);

    // If we're not streaming we can consider the request completed immediately.
    if (!mInputQueue->IsStreaming()) {
        onDrainDone(true);
        return;
    }

    struct v4l2_encoder_cmd cmd;
    memset(&cmd, 0, sizeof(v4l2_encoder_cmd));
    cmd.cmd = V4L2_ENC_CMD_STOP;
    if (mDevice->Ioctl(VIDIOC_ENCODER_CMD, &cmd) != 0) {
        ALOGE("Failed to stop encoder");
        onDrainDone(false);
        return;
    }
    ALOGV("%s(): Sent STOP command to encoder", __func__);
}

void V4L2Encoder::onDrainDone(bool done) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(mState == State::DRAINING || mState == State::ERROR);

    if (mState == State::ERROR) {
        return;
    }

    if (!done) {
        ALOGE("draining the encoder failed");
        mDrainDoneCb.Run(false);
        onError();
        return;
    }

    ALOGV("Draining done");
    mDrainDoneCb.Run(true);

    // Draining the encoder is done, we can now start encoding again.
    if (!mEncodeRequests.empty()) {
        setState(State::ENCODING);
        mTaskRunner->PostTask(FROM_HERE,
                              ::base::BindOnce(&V4L2Encoder::handleEncodeRequest, mWeakThis));
    } else {
        setState(State::WAITING_FOR_INPUT_FRAME);
    }
}

bool V4L2Encoder::configureInputFormat(media::VideoPixelFormat inputFormat, uint32_t stride) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(mState == State::UNINITIALIZED);
    ALOG_ASSERT(!mInputQueue->IsStreaming());
    ALOG_ASSERT(!mVisibleSize.IsEmpty());

    // First try to use the requested pixel format directly.
    base::Optional<struct v4l2_format> format;
    auto fourcc = media::Fourcc::FromVideoPixelFormat(inputFormat, false);
    if (fourcc) {
        format = mInputQueue->SetFormat(fourcc->ToV4L2PixFmt(), mVisibleSize, 0, stride);
    }

    // If the device doesn't support the requested input format we'll try the device's preferred
    // input pixel formats and use a format convertor. We need to try all formats as some formats
    // might not be supported for the configured output format.
    if (!format) {
        std::vector<uint32_t> preferredFormats =
                mDevice->PreferredInputFormat(media::V4L2Device::Type::kEncoder);
        for (uint32_t i = 0; !format && i < preferredFormats.size(); ++i) {
            format = mInputQueue->SetFormat(preferredFormats[i], mVisibleSize, 0, stride);
        }
    }

    if (!format) {
        ALOGE("Failed to set input format to %s",
              media::VideoPixelFormatToString(inputFormat).c_str());
        return false;
    }

    // Check whether the negotiated input format is valid. The coded size might be adjusted to match
    // encoder minimums, maximums and alignment requirements of the currently selected formats.
    auto layout = media::V4L2Device::V4L2FormatToVideoFrameLayout(*format);
    if (!layout) {
        ALOGE("Invalid input layout");
        return false;
    }

    mInputLayout = layout.value();
    if (!media::Rect(mInputLayout->coded_size()).Contains(media::Rect(mVisibleSize))) {
        ALOGE("Input size %s exceeds encoder capability, encoder can handle %s",
              mVisibleSize.ToString().c_str(), mInputLayout->coded_size().ToString().c_str());
        return false;
    }

    // Calculate the input coded size from the format.
    // TODO(dstaessens): How is this different from mInputLayout->coded_size()?
    mInputCodedSize = media::V4L2Device::AllocatedSizeFromV4L2Format(*format);

    // Configuring the input format might cause the output buffer size to change.
    auto outputFormat = mOutputQueue->GetFormat();
    if (!outputFormat.first) {
        ALOGE("Failed to get output format (errno: %i)", outputFormat.second);
        return false;
    }
    uint32_t AdjustedOutputBufferSize = outputFormat.first->fmt.pix_mp.plane_fmt[0].sizeimage;
    if (mOutputBufferSize != AdjustedOutputBufferSize) {
        mOutputBufferSize = AdjustedOutputBufferSize;
        ALOGV("Output buffer size adjusted to: %u", mOutputBufferSize);
    }

    // The coded input size might be different from the visible size due to alignment requirements,
    // So we need to specify the visible rectangle. Note that this rectangle might still be adjusted
    // due to hardware limitations.
    media::Rect visibleRectangle(mVisibleSize.width(), mVisibleSize.height());

    struct v4l2_rect rect;
    memset(&rect, 0, sizeof(rect));
    rect.left = visibleRectangle.x();
    rect.top = visibleRectangle.y();
    rect.width = visibleRectangle.width();
    rect.height = visibleRectangle.height();

    // Try to adjust the visible rectangle using the VIDIOC_S_SELECTION command. If this is not
    // supported we'll try to use the VIDIOC_S_CROP command instead. The visible rectangle might be
    // adjusted to conform to hardware limitations (e.g. round to closest horizontal and vertical
    // offsets, width and height).
    struct v4l2_selection selection_arg;
    memset(&selection_arg, 0, sizeof(selection_arg));
    selection_arg.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    selection_arg.target = V4L2_SEL_TGT_CROP;
    selection_arg.r = rect;
    if (mDevice->Ioctl(VIDIOC_S_SELECTION, &selection_arg) == 0) {
        visibleRectangle = media::Rect(selection_arg.r.left, selection_arg.r.top,
                                       selection_arg.r.width, selection_arg.r.height);
    } else {
        struct v4l2_crop crop;
        memset(&crop, 0, sizeof(v4l2_crop));
        crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        crop.c = rect;
        if (mDevice->Ioctl(VIDIOC_S_CROP, &crop) != 0 ||
            mDevice->Ioctl(VIDIOC_G_CROP, &crop) != 0) {
            ALOGE("Failed to crop to specified visible rectangle");
            return false;
        }
        visibleRectangle = media::Rect(crop.c.left, crop.c.top, crop.c.width, crop.c.height);
    }

    ALOGV("Input format set to %s (size: %s, adjusted size: %dx%d, coded size: %s)",
          media::VideoPixelFormatToString(mInputLayout->format()).c_str(),
          mVisibleSize.ToString().c_str(), visibleRectangle.width(), visibleRectangle.height(),
          mInputCodedSize.ToString().c_str());

    mVisibleSize.SetSize(visibleRectangle.width(), visibleRectangle.height());
    return true;
}

bool V4L2Encoder::configureOutputFormat(media::VideoCodecProfile outputProfile) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(mState == State::UNINITIALIZED);
    ALOG_ASSERT(!mOutputQueue->IsStreaming());
    ALOG_ASSERT(!mVisibleSize.IsEmpty());

    auto format = mOutputQueue->SetFormat(
            media::V4L2Device::VideoCodecProfileToV4L2PixFmt(outputProfile, false), mVisibleSize,
            GetMaxOutputBufferSize(mVisibleSize));
    if (!format) {
        ALOGE("Failed to set output format to %s", media::GetProfileName(outputProfile).c_str());
        return false;
    }

    // The device might adjust the requested output buffer size to match hardware requirements.
    mOutputBufferSize = format->fmt.pix_mp.plane_fmt[0].sizeimage;

    ALOGV("Output format set to %s (buffer size: %u)", media::GetProfileName(outputProfile).c_str(),
          mOutputBufferSize);
    return true;
}

bool V4L2Encoder::configureDevice(media::VideoCodecProfile outputProfile,
                                  std::optional<const uint8_t> outputH264Level) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    // Enable frame-level bitrate control. This is the only mandatory general control.
    if (!mDevice->SetExtCtrls(V4L2_CTRL_CLASS_MPEG,
                              {media::V4L2ExtCtrl(V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, 1)})) {
        ALOGW("Failed enabling bitrate control");
        // TODO(b/161508368): V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE is currently not supported yet,
        // assume the operation was successful for now.
    }

    // Additional optional controls:
    // - Enable macroblock-level bitrate control.
    // - Set GOP length to 0 to disable periodic key frames.
    mDevice->SetExtCtrls(V4L2_CTRL_CLASS_MPEG,
                         {media::V4L2ExtCtrl(V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE, 1),
                          media::V4L2ExtCtrl(V4L2_CID_MPEG_VIDEO_GOP_SIZE, 0)});

    // All controls below are H.264-specific, so we can return here if the profile is not H.264.
    if (outputProfile >= media::H264PROFILE_MIN || outputProfile <= media::H264PROFILE_MAX) {
        return configureH264(outputProfile, outputH264Level);
    }

    return true;
}

bool V4L2Encoder::configureH264(media::VideoCodecProfile outputProfile,
                                std::optional<const uint8_t> outputH264Level) {
    // When encoding H.264 we want to prepend SPS and PPS to each IDR for resilience. Some
    // devices support this through the V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR control.
    // TODO(b/161495502): V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR is currently not supported
    // yet, just log a warning if the operation was unsuccessful for now.
    if (mDevice->IsCtrlExposed(V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR)) {
        if (!mDevice->SetExtCtrls(
                    V4L2_CTRL_CLASS_MPEG,
                    {media::V4L2ExtCtrl(V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR, 1)})) {
            ALOGE("Failed to configure device to prepend SPS and PPS to each IDR");
            return false;
        }
        ALOGV("Device supports prepending SPS and PPS to each IDR");
    } else {
        ALOGW("Device doesn't support prepending SPS and PPS to IDR");
    }

    std::vector<media::V4L2ExtCtrl> h264Ctrls;

    // No B-frames, for lowest decoding latency.
    h264Ctrls.emplace_back(V4L2_CID_MPEG_VIDEO_B_FRAMES, 0);
    // Quantization parameter maximum value (for variable bitrate control).
    h264Ctrls.emplace_back(V4L2_CID_MPEG_VIDEO_H264_MAX_QP, 51);

    // Set H.264 profile.
    int32_t profile = media::V4L2Device::VideoCodecProfileToV4L2H264Profile(outputProfile);
    if (profile < 0) {
        ALOGE("Trying to set invalid H.264 profile");
        return false;
    }
    h264Ctrls.emplace_back(V4L2_CID_MPEG_VIDEO_H264_PROFILE, profile);

    // Set H.264 output level. Use Level 4.0 as fallback default.
    int32_t h264Level =
            static_cast<int32_t>(outputH264Level.value_or(V4L2_MPEG_VIDEO_H264_LEVEL_4_0));
    h264Ctrls.emplace_back(V4L2_CID_MPEG_VIDEO_H264_LEVEL, h264Level);

    // Ask not to put SPS and PPS into separate bitstream buffers.
    h264Ctrls.emplace_back(V4L2_CID_MPEG_VIDEO_HEADER_MODE,
                           V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME);

    // Ignore return value as these controls are optional.
    mDevice->SetExtCtrls(V4L2_CTRL_CLASS_MPEG, std::move(h264Ctrls));

    return true;
}

bool V4L2Encoder::startDevicePoll() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    if (!mDevice->StartPolling(::base::BindRepeating(&V4L2Encoder::serviceDeviceTask, mWeakThis),
                               ::base::BindRepeating(&V4L2Encoder::onPollError, mWeakThis))) {
        ALOGE("Device poll thread failed to start");
        onError();
        return false;
    }

    ALOGV("Device poll started");
    return true;
}

bool V4L2Encoder::stopDevicePoll() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    if (!mDevice->StopPolling()) {
        ALOGE("Failed to stop polling on the device");
        onError();
        return false;
    }

    ALOGV("Device poll stopped");
    return true;
}

void V4L2Encoder::onPollError() {
    ALOGV("%s()", __func__);
    onError();
}

void V4L2Encoder::serviceDeviceTask(bool /*event*/) {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(mState != State::UNINITIALIZED);

    if (mState == State::ERROR) {
        return;
    }

    // Dequeue completed input (VIDEO_OUTPUT) buffers, and recycle to the free list.
    while (mInputQueue->QueuedBuffersCount() > 0) {
        if (!dequeueInputBuffer()) break;
    }

    // Dequeue completed output (VIDEO_CAPTURE) buffers, and recycle to the free list.
    while (mOutputQueue->QueuedBuffersCount() > 0) {
        if (!dequeueOutputBuffer()) break;
    }

    ALOGV("%s() - done", __func__);
}

bool V4L2Encoder::enqueueInputBuffer(std::unique_ptr<InputFrame> frame) {
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(mInputQueue->FreeBuffersCount() > 0);
    ALOG_ASSERT(mState == State::ENCODING);
    ALOG_ASSERT(frame);
    ALOG_ASSERT(mInputLayout->format() == frame->pixelFormat());
    ALOG_ASSERT(mInputLayout->planes().size() == frame->planes().size());

    auto format = frame->pixelFormat();
    auto planes = frame->planes();
    auto index = frame->index();
    auto timestamp = frame->timestamp();

    ALOGV("%s(): queuing input buffer (index: %" PRId64 ")", __func__, index);

    auto buffer = mInputQueue->GetFreeBuffer();
    if (!buffer) {
        ALOGE("Failed to get free buffer from device input queue");
        return false;
    }

    // Mark the buffer with the frame's timestamp so we can identify the associated output buffers.
    buffer->SetTimeStamp(
            {.tv_sec = static_cast<time_t>(timestamp / ::base::Time::kMicrosecondsPerSecond),
             .tv_usec = static_cast<time_t>(timestamp % ::base::Time::kMicrosecondsPerSecond)});
    size_t bufferId = buffer->BufferId();

    for (size_t i = 0; i < planes.size(); ++i) {
        // Single-buffer input format may have multiple color planes, so bytesUsed of the single
        // buffer should be sum of each color planes' size.
        size_t bytesUsed = 0;
        if (planes.size() == 1) {
            bytesUsed = media::VideoFrame::AllocationSize(format, mInputLayout->coded_size());
        } else {
            bytesUsed = ::base::checked_cast<size_t>(
                    media::VideoFrame::PlaneSize(format, i, mInputLayout->coded_size()).GetArea());
        }

        // TODO(crbug.com/901264): The way to pass an offset within a DMA-buf is not defined
        // in V4L2 specification, so we abuse data_offset for now. Fix it when we have the
        // right interface, including any necessary validation and potential alignment.
        buffer->SetPlaneDataOffset(i, planes[i].mOffset);
        bytesUsed += planes[i].mOffset;
        // Workaround: filling length should not be needed. This is a bug of videobuf2 library.
        buffer->SetPlaneSize(i, mInputLayout->planes()[i].size + planes[i].mOffset);
        buffer->SetPlaneBytesUsed(i, bytesUsed);
    }

    if (!std::move(*buffer).QueueDMABuf(frame->fds())) {
        ALOGE("Failed to queue input buffer using QueueDMABuf");
        onError();
        return false;
    }

    ALOGV("Queued buffer in input queue (index: %" PRId64 ", timestamp: %" PRId64
          ", bufferId: %zu)",
          index, timestamp, bufferId);

    ALOG_ASSERT(!mInputBuffers[bufferId]);
    mInputBuffers[bufferId] = std::move(frame);

    return true;
}

bool V4L2Encoder::enqueueOutputBuffer() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(mOutputQueue->FreeBuffersCount() > 0);

    auto buffer = mOutputQueue->GetFreeBuffer();
    if (!buffer) {
        ALOGE("Failed to get free buffer from device output queue");
        onError();
        return false;
    }

    std::unique_ptr<BitstreamBuffer> bitstreamBuffer;
    mFetchOutputBufferCb.Run(mOutputBufferSize, &bitstreamBuffer);
    if (!bitstreamBuffer) {
        ALOGE("Failed to fetch output block");
        onError();
        return false;
    }

    size_t bufferId = buffer->BufferId();

    std::vector<int> fds;
    fds.push_back(bitstreamBuffer->dmabuf_fd);
    if (!std::move(*buffer).QueueDMABuf(fds)) {
        ALOGE("Failed to queue output buffer using QueueDMABuf");
        onError();
        return false;
    }

    ALOG_ASSERT(!mOutputBuffers[bufferId]);
    mOutputBuffers[bufferId] = std::move(bitstreamBuffer);
    ALOGV("%s(): Queued buffer in output queue (bufferId: %zu)", __func__, bufferId);
    return true;
}

bool V4L2Encoder::dequeueInputBuffer() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(mState != State::UNINITIALIZED);
    ALOG_ASSERT(mInputQueue->QueuedBuffersCount() > 0);

    if (mState == State::ERROR) {
        return false;
    }

    bool success;
    media::V4L2ReadableBufferRef buffer;
    std::tie(success, buffer) = mInputQueue->DequeueBuffer();
    if (!success) {
        ALOGE("Failed to dequeue buffer from input queue");
        onError();
        return false;
    }
    if (!buffer) {
        // No more buffers ready to be dequeued in input queue.
        return false;
    }

    uint64_t index = mInputBuffers[buffer->BufferId()]->index();
    int64_t timestamp = buffer->GetTimeStamp().tv_usec +
                        buffer->GetTimeStamp().tv_sec * ::base::Time::kMicrosecondsPerSecond;
    ALOGV("Dequeued buffer from input queue (index: %" PRId64 ", timestamp: %" PRId64
          ", bufferId: %zu)",
          index, timestamp, buffer->BufferId());

    mInputBuffers[buffer->BufferId()] = nullptr;

    mInputBufferDoneCb.Run(index);

    // If we previously used up all input queue buffers we can start encoding again now.
    if ((mState == State::WAITING_FOR_V4L2_BUFFER) && !mEncodeRequests.empty()) {
        setState(State::ENCODING);
        mTaskRunner->PostTask(FROM_HERE,
                              ::base::BindOnce(&V4L2Encoder::handleEncodeRequest, mWeakThis));
    }

    return true;
}

bool V4L2Encoder::dequeueOutputBuffer() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(mState != State::UNINITIALIZED);
    ALOG_ASSERT(mOutputQueue->QueuedBuffersCount() > 0);

    if (mState == State::ERROR) {
        return false;
    }

    bool success;
    media::V4L2ReadableBufferRef buffer;
    std::tie(success, buffer) = mOutputQueue->DequeueBuffer();
    if (!success) {
        ALOGE("Failed to dequeue buffer from output queue");
        onError();
        return false;
    }
    if (!buffer) {
        // No more buffers ready to be dequeued in output queue.
        return false;
    }

    size_t encodedDataSize = buffer->GetPlaneBytesUsed(0) - buffer->GetPlaneDataOffset(0);
    ::base::TimeDelta timestamp = ::base::TimeDelta::FromMicroseconds(
            buffer->GetTimeStamp().tv_usec +
            buffer->GetTimeStamp().tv_sec * ::base::Time::kMicrosecondsPerSecond);

    ALOGV("Dequeued buffer from output queue (timestamp: %" PRId64
          ", bufferId: %zu, data size: %zu, EOS: %d)",
          timestamp.InMicroseconds(), buffer->BufferId(), encodedDataSize, buffer->IsLast());

    if (!mOutputBuffers[buffer->BufferId()]) {
        ALOGE("Failed to find output block associated with output buffer");
        onError();
        return false;
    }

    std::unique_ptr<BitstreamBuffer> bitstream_buffer =
            std::move(mOutputBuffers[buffer->BufferId()]);
    if (encodedDataSize > 0) {
        mOutputBufferDoneCb.Run(encodedDataSize, timestamp.InMicroseconds(), buffer->IsKeyframe(),
                                std::move(bitstream_buffer));
    }

    // If the buffer is marked as last and we were flushing the encoder, flushing is now done.
    if ((mState == State::DRAINING) && buffer->IsLast()) {
        onDrainDone(true);
        // Start the encoder again.
        struct v4l2_encoder_cmd cmd;
        memset(&cmd, 0, sizeof(v4l2_encoder_cmd));
        cmd.cmd = V4L2_ENC_CMD_START;
        if (mDevice->Ioctl(VIDIOC_ENCODER_CMD, &cmd) != 0) {
            ALOGE("Failed to restart encoder after draining (V4L2_ENC_CMD_START)");
            onError();
            return false;
        }
    }

    // Queue a new output buffer to replace the one we dequeued.
    buffer = nullptr;
    enqueueOutputBuffer();

    return true;
}

bool V4L2Encoder::createInputBuffers() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(!mInputQueue->IsStreaming());
    ALOG_ASSERT(mInputBuffers.empty());

    // No memory is allocated here, we just generate a list of buffers on the input queue, which
    // will hold memory handles to the real buffers.
    if (mInputQueue->AllocateBuffers(kInputBufferCount, V4L2_MEMORY_DMABUF) < kInputBufferCount) {
        ALOGE("Failed to create V4L2 input buffers.");
        return false;
    }

    mInputBuffers.resize(mInputQueue->AllocatedBuffersCount());
    return true;
}

bool V4L2Encoder::createOutputBuffers() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(!mOutputQueue->IsStreaming());
    ALOG_ASSERT(mOutputBuffers.empty());

    // No memory is allocated here, we just generate a list of buffers on the output queue, which
    // will hold memory handles to the real buffers.
    if (mOutputQueue->AllocateBuffers(kOutputBufferCount, V4L2_MEMORY_DMABUF) <
        kOutputBufferCount) {
        ALOGE("Failed to create V4L2 output buffers.");
        return false;
    }

    mOutputBuffers.resize(mOutputQueue->AllocatedBuffersCount());
    return true;
}

void V4L2Encoder::destroyInputBuffers() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(!mInputQueue->IsStreaming());

    if (!mInputQueue || mInputQueue->AllocatedBuffersCount() == 0) return;
    mInputQueue->DeallocateBuffers();
    mInputBuffers.clear();
}

void V4L2Encoder::destroyOutputBuffers() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(!mOutputQueue->IsStreaming());

    if (!mOutputQueue || mOutputQueue->AllocatedBuffersCount() == 0) return;
    mOutputQueue->DeallocateBuffers();
    mOutputBuffers.clear();
}

void V4L2Encoder::onError() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    if (mState != State::ERROR) {
        setState(State::ERROR);
        mErrorCb.Run();
    }
}

void V4L2Encoder::setState(State state) {
    ALOG_ASSERT(mTaskRunner->RunsTasksInCurrentSequence());

    // Check whether the state change is valid.
    switch (state) {
    case State::UNINITIALIZED:
        break;
    case State::WAITING_FOR_INPUT_FRAME:
        ALOG_ASSERT(mState != State::ERROR);
        break;
    case State::WAITING_FOR_V4L2_BUFFER:
        ALOG_ASSERT(mState == State::ENCODING);
        break;
    case State::ENCODING:
        ALOG_ASSERT(mState == State::WAITING_FOR_INPUT_FRAME ||
                    mState == State::WAITING_FOR_V4L2_BUFFER || mState == State::DRAINING);
        break;
    case State::DRAINING:
        ALOG_ASSERT(mState == State::ENCODING || mState == State::WAITING_FOR_INPUT_FRAME);
        break;
    case State::ERROR:
        break;
    }

    ALOGV("Changed encoder state from %s to %s", stateToString(mState), stateToString(state));
    mState = state;
}

const char* V4L2Encoder::stateToString(State state) {
    switch (state) {
    case State::UNINITIALIZED:
        return "UNINITIALIZED";
    case State::WAITING_FOR_INPUT_FRAME:
        return "WAITING_FOR_INPUT_FRAME";
    case State::WAITING_FOR_V4L2_BUFFER:
        return "WAITING_FOR_V4L2_BUFFER";
    case State::ENCODING:
        return "ENCODING";
    case State::DRAINING:
        return "DRAINING";
    case State::ERROR:
        return "ERROR";
    }
}

}  // namespace android
