#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief HTTP/2 の DATA を、相手が SETTINGS_MAX_FRAME_SIZE で知らせた長さ以下に分ける。
 * @details 1 つの DATA で送ると、相手の上限(既定 16384)を超えたときに FRAME_SIZE_ERROR の GOAWAY で接続ごと切られた
 *          (2026-10-21 試験の CSMS の wss(19091)で 20095 バイトの DataTransfer。平文の 19080 は上限 1MB を知らせるので通った)。
 */
struct Http2DataSplitPolicy {
  /// RFC 9113 の SETTINGS_MAX_FRAME_SIZE の既定(最小)
  static constexpr std::uint32_t kDefaultMaxFrameSize = 16384U;
  /// RFC 9113 の SETTINGS_MAX_FRAME_SIZE の最大
  static constexpr std::uint32_t kMaxAllowedFrameSize = 16777215U;

  /**
   * @brief 次の DATA の中身の長さを返す。
   * @param remaining まだ送っていない長さ
   * @param serverMaxFrameSize 相手が知らせた上限(範囲の外なら既定・最大に寄せる)
   * @return 送る長さ(remaining が 0 なら 0)
   */
  static std::size_t nextChunk(std::size_t remaining, std::uint32_t serverMaxFrameSize) {
    std::uint32_t limit = serverMaxFrameSize;
    if (limit < kDefaultMaxFrameSize) {
      limit = kDefaultMaxFrameSize;
    } else if (limit > kMaxAllowedFrameSize) {
      limit = kMaxAllowedFrameSize;
    }
    return remaining < limit ? remaining : static_cast<std::size_t>(limit);
  }
};
