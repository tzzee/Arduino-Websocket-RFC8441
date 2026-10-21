#ifndef WEBSOCKETCLIENT_HTTP2WINDOWUPDATE_H_
#define WEBSOCKETCLIENT_HTTP2WINDOWUPDATE_H_

/**
 * @file Http2WindowUpdate.h
 * @brief HTTP/2 の WINDOW_UPDATE の増分(RFC 9113 6.9: 予約 1 ビット + 31 ビット、ネットワークバイト順)の符号化(純粋ロジック)。
 * @details 2026-10-18 まで、増分の uint32 をメモリの並びのまま(ESP32 はリトルエンディアン)送っていた。相手は
 *          ビッグエンディアンで読むので 4280 が約 9.4 億に化け、数回で窓が 2^31-1 を超えて FLOW_CONTROL_ERROR
 *          (GOAWAY 0x3)になり、接続ごと切られて取引中の要求が失われた(耐久試験で約 20〜60 分に 1 回)。
 */

#include <cstdint>

namespace Http2WindowUpdate {

/// 増分の最大(31 ビット)
constexpr std::uint32_t kMaxIncrement = 0x7FFFFFFFu;

/**
 * @brief 増分を WINDOW_UPDATE の本文(4 バイト、ビッグエンディアン、最上位ビットは 0)に書く。
 * @param increment 増分(31 ビットを超える分は落とす)
 * @param out 書き先(4 バイト)
 */
inline void encodeIncrement(std::uint32_t increment, std::uint8_t out[4]) {
  const std::uint32_t v = increment & kMaxIncrement;
  out[0] = static_cast<std::uint8_t>(v >> 24);
  out[1] = static_cast<std::uint8_t>(v >> 16);
  out[2] = static_cast<std::uint8_t>(v >> 8);
  out[3] = static_cast<std::uint8_t>(v);
}

/**
 * @brief WINDOW_UPDATE の本文(4 バイト)から増分を読む(予約ビットは無視する)。
 * @param in 本文(4 バイト)
 * @return 増分
 */
inline std::uint32_t decodeIncrement(const std::uint8_t in[4]) {
  return ((static_cast<std::uint32_t>(in[0]) << 24) | (static_cast<std::uint32_t>(in[1]) << 16) |
          (static_cast<std::uint32_t>(in[2]) << 8) | static_cast<std::uint32_t>(in[3])) & kMaxIncrement;
}

/**
 * @brief 受信の窓を足し戻す(WINDOW_UPDATE を送る)かを返す。
 * @details 窓の残りを「(初期値 + 足し戻した累計) - 受けた累計」と符号なしで引くと、受けた累計が窓を超えたとき
 *          巨大な値に化けて「まだ残っている」と判断し、二度と WINDOW_UPDATE を送らなかった。受けた累計を DATA の
 *          ヘッダー 9 バイト込みで数えていた(相手は中身だけで数える)ので、16KB の DATA で窓を少し超え、相手は窓が
 *          尽きて残りを送らず、20000 文字の下りが 16470 バイトで止まった(2026-10-21)。足し算で比べる。
 * @param initialWindow 窓の初期値
 * @param granted 足し戻した累計
 * @param consumed 受けた DATA の中身の累計
 * @param keepSize 窓の残りがこれを下回ったら足し戻す
 * @return 送るなら true
 */
inline bool shouldRefill(std::uint32_t initialWindow, std::uint32_t granted, std::uint32_t consumed,
                         std::uint32_t keepSize) {
  return static_cast<std::uint64_t>(consumed) + keepSize >
         static_cast<std::uint64_t>(initialWindow) + granted;
}

/**
 * @brief 足し戻す増分を返す(受けた分だけ戻し、窓を初期値に戻す)。
 * @param granted 足し戻した累計
 * @param consumed 受けた DATA の中身の累計
 * @return 増分(0 なら送らない。WINDOW_UPDATE の増分 0 は誤り)
 */
inline std::uint32_t refillIncrement(std::uint32_t granted, std::uint32_t consumed) {
  const std::uint32_t inc = consumed > granted ? consumed - granted : 0u;
  return inc > kMaxIncrement ? kMaxIncrement : inc;
}

}  // namespace Http2WindowUpdate

#endif  // WEBSOCKETCLIENT_HTTP2WINDOWUPDATE_H_
