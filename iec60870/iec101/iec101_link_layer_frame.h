#ifndef IEC_APDU_H
#define IEC_APDU_H

#include <vector>

namespace QIEC60870 {
namespace p101 {
enum class FrameParseErr {
  kNoError = 0,
  kNeedMoreData = 1,
  kBadFormat = 2,
  kCheckError = 3
};

enum class PRM { kFromStartupStation = 1, kFromSlaveStation = 0 };
enum class DIR { kFromMasterStation = 0, kFromSlaveStation = 1 };
enum class FCB { k0, k1 };
enum class FCV { kFCBValid = 1, kFCBInvalid = 0 };
enum class ACD { kLevel1DataWatingAccess = 1, kLevel1NoDataWatingAccess = 0 };
enum class DFC { kSlaveCannotRecv = 1, kSlaveCanRecv = 0 };

enum class StartupFunction {
  kResetRemoteLink = 0,
  kSendLinkStatus = 2,
  kSendUserData = 3,
  kSendNoanswerUserData = 4,
  kAccessRequest = 8,
  kRequestLinkStatus = 9,
  kRequestLevel1UserData = 10,
  kRequestLevel2UserData = 11,
};
enum class SlaveFunction {
  kConfirmedRecognized = 0,
  kConfirmedRejected = 1,
  kResponseUserData = 8,
  kResponseNotFoundUserData = 9,
  kResponseLinkStatus = 11,
};

const int kInvalidSlaveAddress = 0x00;
const int kBroadcastSlaveAddress = 0xffff;

/**
 * @brief Can describe both fixed frames and variable-length frames
 * if asdu_ is empty, it's a fixed frame, otherwise it's a variable frame
 */
class LinkLayerFrame {
public:
  LinkLayerFrame() = default;
  ~LinkLayerFrame() = default;

  LinkLayerFrame(uint8_t c, uint16_t a,
                 const std::vector<uint8_t> &asdu = std::vector<uint8_t>())
      : C(c), slaveAddress_(a), asdu_(asdu) {}

  /**
   * @brief PRM
   *
   * @return
   */
  bool isFromStartupStation() const { return ((C & 0x40) >> 6); }
  /**
   * @brief DIR
   *
   * @return
   */
  bool isFromMasterStation() const { return !((C & 0x80) >> 7); }
  /**
   * @brief FCB
   *
   * @return
   */
  bool fcb() const { return ((C & 0x20) >> 5); }
  /**
   * @brief ACD
   *
   * @return
   */
  bool hasLevel1DataWatingAccess() const { return ((C & 0x20) >> 5); }
  /**
   * @brief FCV
   *
   * @return
   */
  bool isValidFCB() const { return ((C & 0x10) >> 4); }
  /**
   * @brief DFC
   *
   * @return
   */
  bool isSlaveCannotRecv() const { return ((C & 0x10) >> 4); }
  /**
   * @brief FC
   *
   * @return
   */
  int functionCode() const { return C & 0x0f; }

  void setPRM(PRM prm) {
    C &= 0xbf;
    C |= (prm == PRM::kFromStartupStation ? 0x40 : 0x00);
  }

  void setDIR(DIR dir) {
    C &= 0x7f;
    C |= (dir == DIR::kFromMasterStation ? 0x00 : 0x80);
  }

  void setFCB(FCB fcb) {
    C &= 0xdf;
    C |= (fcb == FCB::k0 ? 0x00 : 0x20);
  }

  void setACD(ACD acd) {
    C &= 0xdf;
    C |= (acd == ACD::kLevel1DataWatingAccess ? 0x20 : 0x00);
  }

  void setFCV(FCV fcv) {
    C &= 0xef;
    C |= (fcv == FCV::kFCBValid ? 0x10 : 0x00);
  }

  void setDFC(DFC dfc) {
    C &= 0xef;
    C |= (dfc == DFC::kSlaveCannotRecv ? 0x10 : 0x00);
  }
  /**
   * @brief fc is StartupFunction/SlaveFunction
   *
   * @param fc
   */
  void setFC(int fc) {
    C &= 0xf0;
    C |= fc;
  }

  void setSlaveLevel12UserDataIsEmpty() {
    asdu_.clear();
    isE5Frame_ = true;
  }
  /**
   * @brief If the substation has no level 1 user data and level 2 user data,
   * return true
   *
   * @return
   */
  bool isSlaveLevel12UserDataEmpty() const { return isE5Frame_; }

  uint8_t ctrlDomain() const { return C; }

  /**
   * @brief if asdu is empty, that is, it's a fixed frame
   *
   * @return
   */
  bool hasAsdu() { return !asdu_.empty(); }

  std::vector<uint8_t> asdu() const { return asdu_; }
  int slaveAddress() const { return slaveAddress_; }

  std::vector<uint8_t> encode() {
    bool isFixedFrame = asdu_.empty() && !isE5Frame_;
    std::vector<uint8_t> raw;
    if (isFixedFrame) {
      raw.push_back(0x10);
      raw.push_back(C);
      raw.push_back(static_cast<uint8_t>(slaveAddress_));
      raw.push_back(C + slaveAddress_); /// cs
      raw.push_back(0x16);
    } else if (isE5Frame_) {
      raw.push_back(0xe5);
    } else {
      raw.push_back(0x68);
      uint8_t len = 2 + asdu_.size();
      raw.push_back(len);
      raw.push_back(len);
      raw.push_back(0x68);
      raw.push_back(C);
      raw.push_back(static_cast<uint8_t>(slaveAddress_));
      raw.insert(raw.end(), asdu_.begin(), asdu_.end());
      uint8_t cs = C + slaveAddress_;
      for (const auto &ch : asdu_) {
        cs += ch;
      }
      raw.push_back(cs);
      raw.push_back(0x16);
    }
    return raw;
  }

private:
  uint8_t C = 0x00;
  uint16_t slaveAddress_ = kInvalidSlaveAddress;
  std::vector<uint8_t> asdu_;
  bool isE5Frame_ = false;
}; // namespace p101

class LinkLayerFrameCodec {
  enum State {
    kStart,
    kSecond68,
    kCtrlDomain,
    kAddressOffset0,
    kLengthOffset0,
    kLengthOffset1,
    kAsdu,
    kCs,
    kEnd,
    kDone
  };

public:
  /**
   * @brief decode decode raw data,
   * parse ctrlDomain,address,asdu
   * after decode, if the error()is FrameParseErr::kNoError,
   * then can call toLinkLayerFrame()
   *
   * @param data
   */
  void decode(const std::vector<uint8_t> &data) {
    for (const auto &ch : data) {
      data_.push_back(ch);
      switch (state_) {
      case kStart: {
        if (ch == 0x10) {
          isFixedFrame_ = true;
          state_ = kCtrlDomain;
        } else if (ch == 0x68) {
          isFixedFrame_ = false;
          state_ = kLengthOffset0;
        } else if (ch == 0xe5) {
          isE5Frame_ = true;
          isFixedFrame_ = false;
          err_ = FrameParseErr::kNoError;
          state_ = kDone;
        } else {
          err_ = FrameParseErr::kBadFormat;
          state_ = kDone;
        }
      } break;
      case kCtrlDomain: {
        ctrlDomain_ = ch;
        state_ = kAddressOffset0;
      } break;
      case kLengthOffset0: {
        length_[0] = ch;
        state_ = kLengthOffset1;
      } break;
      case kLengthOffset1: {
        length_[1] = ch;
        if (length_[0] != length_[1]) {
          err_ = FrameParseErr::kCheckError;
          state_ = kDone;
        } else {
          state_ = kSecond68;
        }
      } break;
      case kSecond68: {
        if (ch != 0x68) {
          err_ = FrameParseErr::kBadFormat;
          state_ = kDone;
        } else {
          state_ = kCtrlDomain;
        }
      } break;
      case kAddressOffset0: {
        slaveAddress_ = ch;
        state_ = isFixedFrame_ ? kCs : kAsdu;
      } break;
      case kAsdu: {
        asdu_.push_back(ch);
        if (length_[0] == asdu_.size() + 2) {
          state_ = kCs;
        }
      } break;
      case kCs: {
        cs_ = ch;
        state_ = kEnd;
      } break;
      case kEnd: {
        err_ = ch == 0x16 ? FrameParseErr::kNoError : FrameParseErr::kBadFormat;
        state_ = kDone;
      } break;
      case kDone: {

      } break;
      }

      if (state_ == kDone) {
        break;
      }
    }
    if (err_ == FrameParseErr::kNoError && !isE5Frame_) {
      uint8_t cs = calculateCs_();
      if (cs != cs_) {
        err_ = FrameParseErr::kCheckError;
      }
    }
  }

  /**
   * @brief if isDone( is true, then can call
   * error() to check decode is failed or successed,
   * if decode is successed, the can call toLinkLayerFrame(
   *
   * @return
   */
  FrameParseErr error() { return err_; }
  LinkLayerFrame toLinkLayerFrame() const {
    LinkLayerFrame frame;
    if (isE5Frame_) {
      frame.setSlaveLevel12UserDataIsEmpty();
    } else if (isFixedFrame_) {
      frame = LinkLayerFrame(ctrlDomain_, slaveAddress_);
    } else {
      frame = LinkLayerFrame(ctrlDomain_, slaveAddress_, asdu_);
    }
    return frame;
  }

private:
  uint8_t calculateCs_() {
    uint8_t cs = 0;
    cs += ctrlDomain_;
    cs += slaveAddress_;
    for (const auto &ch : asdu_) {
      cs += ch;
    }
    return cs;
  }

  uint8_t ctrlDomain_;
  uint8_t slaveAddress_;
  uint8_t length_[2];
  uint8_t cs_;
  std::vector<uint8_t> asdu_;
  bool isE5Frame_ = false;

  /// internal
  FrameParseErr err_ = FrameParseErr::kNeedMoreData;
  bool isFixedFrame_ = false;
  std::vector<uint8_t> data_;
  State state_ = kStart;
};

} // namespace p101
} // namespace QIEC60870

#endif
