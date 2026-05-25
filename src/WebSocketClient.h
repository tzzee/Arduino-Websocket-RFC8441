/*
Websocket-Arduino, a websocket implementation for Arduino
Copyright 2016 Brendan Hall

Based on previous implementations by
Copyright 2011 Brendan Hall
and
Copyright 2010 Ben Swanson
and
Copyright 2010 Randall Brewer
and
Copyright 2010 Oliver Smith

Some code and concept based off of Webduino library
Copyright 2009 Ben Combee, Ran Talbott

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

-------------
Now based off
http://www.whatwg.org/specs/web-socket-protocol/

- OLD -
Currently based off of "The Web Socket protocol" draft (v 75):
http://tools.ietf.org/html/draft-hixie-thewebsocketprotocol-75
*/


#ifndef WEBSOCKETCLIENT_H_
#define WEBSOCKETCLIENT_H_

#include <Arduino.h>
#include <Stream.h>
#if defined(__has_include)
#if __has_include("String.h")
#include "String.h"
#elif __has_include(<WString.h>)
#include <WString.h>
#endif
#else
#include "String.h"
#endif
#include "Client.h"

#include "Http1Header.h"
#include "Http2Frame.h"
#include <map>
#include <queue>

// Amount of time (in ms) a user may be connected before getting disconnected
// for timing out (i.e. not sending any data to the server).
#define TIMEOUT_IN_MS 10000

// ACTION_SPACE is how many actions are allowed in a program. Defaults to
// 5 unless overwritten by user.
#ifndef CALLBACK_FUNCTIONS
#define CALLBACK_FUNCTIONS 1
#endif

// Don't allow the client to send big frames of data. This will flood the Arduinos
// memory and might even crash it.
#ifndef MAX_FRAME_LENGTH
#define MAX_FRAME_LENGTH 256
#endif

#define SIZE(array) (sizeof(array) / sizeof(*array))

// WebSocket protocol constants
// First byte
#define WS_FIN            0x80
#define WS_RSV            0b01110000
#define WS_OPCODE_CONT    0x00
#define WS_OPCODE_TEXT    0x01
#define WS_OPCODE_BINARY  0x02
#define WS_OPCODE_CLOSE   0x08
#define WS_OPCODE_PING    0x09
#define WS_OPCODE_PONG    0x0a
// Second byte
#define WS_MASK           0x80
//#define WS_MASK           0x00
#define WS_SIZE16         126
#define WS_SIZE64         127


#define WS_CLOSE_UNSUPPORTED "Unsupported WebSocket opcode"
#define WS_CLOSE_BAD_REQUEST "Bad Request"

typedef int WS_SIZE_T;
const WS_SIZE_T WS_SIZE_T_NONE = (WS_SIZE_T)(-1);
const WS_SIZE_T WS_SIZE_T_HEADER = (WS_SIZE_T)(-2);
class WebSocketClient {
public:
    enum HTTPHandshakeVersion {
        ONLY_HTTP_VERSION_1_1,
        ONLY_HTTP_VERSION_2_0,  // rfc8441
        PREFER_HTTP_VERSION_1_1,
        PREFER_HTTP_VERSION_2_0  // rfc8441
    };
    enum HTTPVersion {
        HTTP_VERSION_1_1,
        HTTP_VERSION_2_0,
        HTTP_VERSION_UNKNOWN
    };
    WebSocketClient(Client &client, const char *host, HTTPHandshakeVersion httpHandshakeVersion=ONLY_HTTP_VERSION_1_1, bool socketio = false);
    // Handle connection requests to validate and process/refuse
    // connections.
    Http2Frame::StreamIdentifier handshake_h1(const char *path, const char *protocol, std::uint32_t timeoutMsec=10000);
    Http2Frame::StreamIdentifier handshake_h2(const char *path, const char *protocol, std::uint32_t timeoutMsec=10000);

    Http2Frame::StreamIdentifier handshake(const char *path, const char *protocol, std::uint32_t timeoutMsec=10000);

    void bye(Http2Frame::StreamIdentifier streamId = 1, bool terminateCode = false);
    void reset();

    // Get data off of the stream
    std::size_t getData(char *data, std::size_t capacity, uint8_t *opcode = NULL, Http2Frame::StreamIdentifier *streamId = NULL, bool enableQueue = true);
    bool getData(String& data, uint8_t *opcode = NULL, Http2Frame::StreamIdentifier *streamId = NULL);

    /**
     * @brief WebSocketデータを送信する。
     * @details HTTP/2時はフレームヘッダ分を除いた送信ペイロード長を返す。
     *          送信失敗または短書き込み時は0を返す。
     */
    std::size_t sendData(const char *str, std::size_t size, uint8_t opcode, Http2Frame::StreamIdentifier streamId = 1);
    std::size_t sendData(const String& str, uint8_t opcode, Http2Frame::StreamIdentifier streamId = 1) {
        return sendData(str.c_str(), str.length(), opcode, streamId);
    }

    WS_SIZE_T handleStream(bool enableQueue = true);

    HTTPVersion getHTTPVersion() const {
        return httpVersion;
    }


    void _handle_h2(String *temp);

private:
    Client * const socket_client;
    const char *host;
    const HTTPHandshakeVersion httpHandshakeVersion;
    const bool issocketio;

    HTTPVersion httpVersion;
    

    // socket.io session id
    char sid[32];


    struct Frame{
        bool fin;
        uint8_t opcode;
        WS_SIZE_T length;
        uint8_t mask[4];
        bool hasMask;
    };
    enum FrameNextState {
        WS_FRAME_OPCODE,
        WS_FRAME_LENGTH_8,
        WS_FRAME_LENGTH_16,
        WS_FRAME_LENGTH_64,
        WS_FRAME_MASK,
        WS_FRAME_PAYLOAD,  
    };
    struct ReceivingFrame{
        FrameNextState state;
        Frame frame;
        WS_SIZE_T index;
        unsigned long _startMillis;
    } receivingFrame;

    WS_SIZE_T _handleStream(WebSocketClient::ReceivingFrame *receivingFrame, Http2Frame::StreamIdentifier streamId);


    // Discovers if the client's header is requesting an upgrade to a
    // websocket connection.
    bool analyzeRequest_h1(const char *path, const char *protocol, std::uint32_t timeoutMsec);

    bool setting_h2(std::uint32_t timeoutMsec);
    Http2Frame::StreamIdentifier connect_h2(const char *path, const char *protocol, Http2Frame::StreamIdentifier id, std::uint32_t timeoutMsec);


    // Disconnect user gracefully.
    void disconnectStream_h1(bool terminateCode);
    /**
     * @brief HTTP/2ストリームを切断する。
     * @param streamId 0ならソケット全体を切断し、0以外なら対象ストリームを終了する。
     * @param terminateCode trueのときCloseコードを付けて終了する。
     */
    void disconnectStream_h2(Http2Frame::StreamIdentifier streamId, bool terminateCode);

    String h2TempBuffer;
    Http2Status h2Status;

    struct H2SendingStream {
        uint32_t serverWindowSize;
        uint32_t clientWindowSize;
        uint32_t totalRxSize;
        uint32_t totalTxSize;
        ReceivingFrame receivingFrame;
        H2SendingStream() : serverWindowSize(0), clientWindowSize(0), totalRxSize(0) {
            // Empty
        }
    };

    std::map<Http2Frame::StreamIdentifier, H2SendingStream> h2Stream;

    struct H2BufferedRxData {
        Http2Frame::StreamIdentifier streamId;
        char* data;
        size_t length;
        size_t cursor;
        uint8_t opcode;
    };
    std::queue<H2BufferedRxData> h2BufferedRxDataQueue;
    size_t h2BufferedRxDataQueueBytes = 0U;
    static constexpr size_t H2_BUFFERED_RX_QUEUE_MAX_ITEMS = 16U;
    static constexpr size_t H2_BUFFERED_RX_QUEUE_MAX_BYTES = 16U * 1024U;
    void clearH2BufferedRxDataQueue();
    void trimH2BufferedRxDataQueue();
    bool enqueueH2BufferedRxData(Http2Frame::StreamIdentifier streamId, const char* data, size_t len, uint8_t opcode);

    Http2Frame::StreamIdentifier genNewStreamId() {
        static Http2Frame::StreamIdentifier nextStreamId = 1;
        while (true) {
            Http2Frame::StreamIdentifier id = nextStreamId;
            nextStreamId = (nextStreamId+2) & 0x7FFFFFFF;
            if (h2Stream.find(id) == h2Stream.end()) {
                return id;
            }
        }
    }
};



#endif
