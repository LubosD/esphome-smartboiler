#include "SBProtocol.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sb {

void SBProtocolRequest::write_le(uint8_t s) { this->mData.push_back(s); }

// write value in Little Endian
void SBProtocolRequest::write_le(uint16_t s) {
  this->mData.push_back((uint8_t) (s & 255));
  this->mData.push_back((uint8_t) (s >> 8));
}

// write value in Little Endian
void SBProtocolRequest::write_le(uint32_t s) {
  this->mData.push_back((uint8_t) (s & 255));
  this->mData.push_back((uint8_t) ((s >> 8) & 255));
  this->mData.push_back((uint8_t) ((s >> 16) & 255));
  this->mData.push_back((uint8_t) ((s >> 24) & 255));
}

// write value in Little Endian
void SBProtocolRequest::write_le(uint64_t s) {
  this->mData.push_back((uint8_t) (s & 255));
  this->mData.push_back((uint8_t) ((s >> 8) & 255));
  this->mData.push_back((uint8_t) ((s >> 16) & 255));
  this->mData.push_back((uint8_t) ((s >> 24) & 255));
  this->mData.push_back((uint8_t) ((s >> 32) & 255));
  this->mData.push_back((uint8_t) ((s >> 40) & 255));
  this->mData.push_back((uint8_t) ((s >> 48) & 255));
  this->mData.push_back((uint8_t) ((s >> 56) & 255));
}

void SBProtocolRequest::writeString(const std::string &s) {
  for (size_t i = 0; i < s.size(); i++) {
    this->mData.push_back(s[i]);
  }
}

uint32_t SBProtocolResult::load_uint32_le(size_t position) {
  uint32_t number;
  number = this->mByteData[position];
  number |= uint32_t(this->mByteData[position + 1]) << 8;
  number |= uint32_t(this->mByteData[position + 2]) << 16;
  number |= uint32_t(this->mByteData[position + 3]) << 24;
  return number;
}

uint16_t SBProtocolResult::load_uint16_le(size_t position) {
  uint16_t number;
  number = this->mRawData[position];
  number |= uint32_t(this->mRawData[position + 1]) << 8;
  return number;
}

SBProtocolResult::SBProtocolResult(const uint8_t *value, uint16_t value_len) {
  this->mRawData = value;
  this->mRqType = SBPacket::SBC_PACKET_NONE;
  if (value == nullptr || value_len < 2)
    return;

  // First two bytes contain a decimal value from SbcPacket as a string
  SBPacket cmd = static_cast<SBPacket>((value[0] - '0') * 10 + (value[1] - '0'));
  this->mRqType = cmd;
  if (cmd == SBPacket::SBC_PACKET_NIGHT_GETDAYS) {
    // copy next 12 bytes
  } else if (cmd == SBPacket::SBC_PACKET_NIGHT_GETDAYS2) {
    // copy next 9 bytes
  } else if (cmd == SBPacket::SBC_PACKET_GLOBAL_CONFIRMUID) {
    // confirmation packet includes UID of original request followed by up to 16 bytes of data
    if (value_len >= 4)
      this->mUid = this->load_uint16_le(2);
    for (int i = 4; i < value_len && i < 4 + 16; i++) {
      this->mByteData.push_back(value[i]);
    }
  } else if (cmd == SBPacket::SBC_PACKET_GLOBAL_FIRSTLOG || cmd == SBPacket::SBC_PACKET_GLOBAL_NEXTLOG) {
    // two uint32 located at index 2 and 10
  } else if (value_len > 4) {
    // the rest is a string (last two bytes are not part of it)
    // HOLIDAY_GET is also a string: two 8 digit decimal numbers (days to begin, length)
    std::string text(value + 2, value + value_len - 2);
    // the original app treats the payload as NUL terminated string
    auto nul = text.find('\0');
    if (nul != std::string::npos)
      text.resize(nul);
    this->mString = text;
  }
}

}  // namespace sb
}  // namespace esphome