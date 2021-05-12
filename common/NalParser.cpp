// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <v4l2_codec2/common/NalParser.h>

#include <algorithm>

namespace android {

NalParser::NalParser(const uint8_t* data, size_t length)
      : mCurrNalDataPos(data), mDataEnd(data + length) {
    mNextNalStartCodePos = findNextStartCodePos();
}

bool NalParser::locateNextNal() {
    if (mNextNalStartCodePos == mDataEnd) return false;
    mCurrNalDataPos = mNextNalStartCodePos + kNalStartCodeLength;  // skip start code.
    mNextNalStartCodePos = findNextStartCodePos();
    return true;
}

const uint8_t* NalParser::data() const {
    return mCurrNalDataPos;
}

size_t NalParser::length() const {
    if (mNextNalStartCodePos == mDataEnd) return mDataEnd - mCurrNalDataPos;
    size_t length = mNextNalStartCodePos - mCurrNalDataPos;
    // The start code could be 3 or 4 bytes, i.e., 0x000001 or 0x00000001.
    return *(mNextNalStartCodePos - 1) == 0x00 ? length - 1 : length;
}

const uint8_t* NalParser::findNextStartCodePos() const {
    return std::search(mCurrNalDataPos, mDataEnd, kNalStartCode,
                       kNalStartCode + kNalStartCodeLength);
}

}  // namespace android
