//#define DEBUGGING

#include "global.h"
#include "WebSocketClient.h"

#include "WebSocketClientSha1.h"
#include "WebSocketClientBase64.h"

static void hexdump(const void *mem, uint32_t len, uint8_t cols = 16) {
	const uint8_t* src = (const uint8_t*) mem;
	Serial.printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
	for(uint32_t i = 0; i < len; i++) {
		if(i % cols == 0) {
			Serial.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
		}
		Serial.printf("%02X ", *src);
		src++;
	}
	Serial.printf("\n");
}

Http2Frame::StreamIdentifier WebSocketClient::handshake_h2(Client &client, bool socketio, std::uint32_t timeoutMsec) {
    socket_client = &client;
    assert(!socketio); // Not implemented..
    // If there is a connected client->
    if (socket_client->connected()) {
        // Check request and look for websocket handshake
        log_v(F("Client connected"));
        if (setting_h2(timeoutMsec)) {
            log_v(F("Websocket established"));
            Http2Frame::StreamIdentifier streamId = genNewStreamId();
            if (connect_h2(streamId, timeoutMsec)) {
                return streamId;
            } else {
                return 0;
            }
        } else {
            // Might just need to break until out of socket_client loop.
            log_v(F("Invalid handshake"));
            // disconnectStream_h2();
            return 0;
        }
    } else {
        return 0;
    }
}

static bool waitForResponse(Client* socket_client, std::uint32_t startMillis, std::uint32_t timeoutMsec) {
    Serial.print(F("Waiting"));
    while (!socket_client->available()) {
        if (!socket_client->connected()) {
            Serial.println();
            Serial.println("Connection diffused");
            return false;
        } else if ((millis() - startMillis) > timeoutMsec) {
            socket_client->stop();
            Serial.println();
            Serial.println("Connection timeout");
            return false;
        }
        delay(100);
        Serial.print(".");
    }
    Serial.println();
    return true;
}

static int waitForByte(Client* socket_client, std::uint32_t startMillis = 0, std::uint32_t timeoutMsec = 0) {
    while (!socket_client->available()) {
        if (!socket_client->connected()) {
            Serial.println();
            Serial.println("Connection diffused");
            return -1;
        } else if (timeoutMsec > 0) {
            if ((millis() - startMillis) > timeoutMsec) {
                socket_client->stop();
                Serial.println();
                Serial.println("Connection timeout");
                return -1;
            }
            delay(1);
        } else {
            return -1;
        }
    }
    return socket_client->read();
}

bool WebSocketClient::setting_h2(std::uint32_t timeoutMsec) {
    if (h2Status.connected) {
        return true;  // already connected
    }
    h2TempBuffer= "";
    int bite;
    uint32_t recvMillis = millis();

    h2Status.init();

    recvMillis = millis();    
    socket_client->print(F("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"));  // HTTP/2 connection preface
    log_d("Sent HTTP/2 preface");
    // if server supports HTTP/2, it should respond with SETTINGS frame

    // send Settings
    Http2Frame::SettingsFramePayload framePayload {
        Http2Frame::SettingFrameField(Http2Frame::SETTINGS_HEADER_TABLE_SIZE, 0),
        Http2Frame::SettingFrameField(Http2Frame::SETTINGS_ENABLE_PUSH, 0),
        Http2Frame::SettingFrameField(Http2Frame::SETTINGS_MAX_FRAME_SIZE, h2Status.clientMaxFrameSize),
        Http2Frame::SettingFrameField(Http2Frame::SETTINGS_INITIAL_WINDOW_SIZE, h2Status.clientInitialWindowSize),
        Http2Frame::SettingFrameField(Http2Frame::SETTINGS_ENABLE_CONNECT_PROTOCOL, 1)  // RFC 8441
    };
    // hexdump(framePayload.toBytes(), framePayload.bytesSize());
    const Http2Frame settingsFrame(Http2Frame::FRAME_TYPE_SETTINGS, Http2Frame::FRAME_FLAG_NONE, 0, framePayload.bytesSize(), framePayload.toBytes());
    socket_client->write(settingsFrame.toBytes(), settingsFrame.bytesSize());
    log_d("Sent SETTINGS frame[%u]: %d bytes", 0, settingsFrame.bytesSize());

    if (!waitForResponse(socket_client, recvMillis, timeoutMsec)) {
        log_d("connection error waiting for server response to preface");
        return false;
    }

    recvMillis = millis();
    while (true) {
        bite = waitForByte(socket_client, recvMillis, timeoutMsec);  // read byte by byte until timeout or end of headers
        if (bite == -1) {
            log_d("connection error waiting for server response to preface");
            return false;
        }
#ifdef DEBUGGING
        Serial.print((char)bite);
#endif
        h2TempBuffer += (char)bite;

        if ((char)bite == '\n') {
            // HTTP/1.1 400 Bad Request
            String tempH1 = h2TempBuffer;
            tempH1.trim();
            if (tempH1.length()==0) {
                // end of headers
                break;
            }
            if (tempH1.startsWith("HTTP/1")) {
                h2TempBuffer = "";
                // if server not support HTTP/2, it should respond with HTTP/1.1 400 Bad Request or similar
                return false;
            }
        } else {
            _handle_h2(&h2TempBuffer);
        }      
        if (h2Status.settingsReceived && h2Status.settingsSent) {
            if (h2Status.enableConnectProtocol) { 
                h2Status.connected = true;
                return true;  // success
            } else {
                log_d("Server does not support extended CONNECT protocol");
                return false;
            }
        }
    }
    return false;
}

Http2Frame::StreamIdentifier WebSocketClient::connect_h2(Http2Frame::StreamIdentifier id, std::uint32_t timeoutMsec) {
    // send extended CONNECT frame
    if (!h2Status.connected) {
        return false;  // not connected
    }
    int bite;
    bool foundupgrade = false;
    bool foundsid = false;
    unsigned long intkey[2];
    String serverKey;
    char keyStart[17];
    char b64Key[25];
    String key = "------------------------";
    uint32_t recvMillis = millis();

#ifdef DEBUGGING
    Serial.println(F("Sending websocket upgrade headers"));
#endif

#ifndef ARDUINO_ARCH_ESP32
        randomSeed(analogRead(0));
#endif

    for (int i=0; i<16; ++i) {
        keyStart[i] = (char)random(1, 256);
    }
    base64_encode(b64Key, keyStart, 16);
    for (int i=0; i<24; ++i) {
        key[i] = b64Key[i];
    }

    recvMillis = millis();    
    // send ACK
    Http2Frame::HeadersFramePayload framePayload {
        Http2Frame::HeaderFrameField(":method", "CONNECT"),
        Http2Frame::HeaderFrameField(":scheme", "ws"),
        Http2Frame::HeaderFrameField(":path", path),
        Http2Frame::HeaderFrameField(":authority", host),
        Http2Frame::HeaderFrameField(":protocol", "websocket"),
        Http2Frame::HeaderFrameField("sec-websocket-protocol", protocol),
        Http2Frame::HeaderFrameField("sec-websocket-version", "13"),
        // Http2Frame::HeaderFrameField("sec-websocket-key", key.c_str()),
    };
    // hexdump(framePayload.toBytes(), framePayload.bytesSize());
    const Http2Frame headersFrame(Http2Frame::FRAME_TYPE_HEADERS, Http2Frame::FRAME_FLAG_END_HEADERS /*no END_STREAM*/, id, framePayload.bytesSize(), framePayload.toBytes());
    const size_t r = socket_client->write(headersFrame.toBytes(), headersFrame.bytesSize());
    log_d("Sent HEADERS frame[%u]: %d bytes", id, r);

    if (!waitForResponse(socket_client, recvMillis, timeoutMsec)) {
        log_d("connection error waiting for server response to preface");
        return 0;
    }

    recvMillis = millis();
    while (true) {
        bite = waitForByte(socket_client, recvMillis, timeoutMsec);  // read byte by byte until timeout or end of headers
        if (bite == -1) {
            log_d("connection error waiting for server response to preface");
            return 0;
        }
        Serial.print((char)bite);
        h2TempBuffer += (char)bite;
        _handle_h2(&h2TempBuffer);

        if (h2Stream.find(id) != h2Stream.end()) {
            log_d("WebSocket over HTTP/2 established");
            return id;  // success
        }
    }
    log_d("WebSocket over HTTP/2 connection failed");
    return 0;
}

bool WebSocketClient::handshake_h1(Client &client, bool socketio, std::uint32_t timeoutMsec) {

    socket_client = &client;
    issocketio = socketio;
    strcpy(sid, "");

    // If there is a connected client->
    if (socket_client->connected()) {
        // Check request and look for websocket handshake
#ifdef DEBUGGING
            Serial.println(F("Client connected"));
#endif
        if (issocketio && strlen(sid) == 0) {
            analyzeRequest_h1(timeoutMsec);
        }

        if (analyzeRequest_h1(timeoutMsec)) {
#ifdef DEBUGGING
                Serial.println(F("Websocket established"));
#endif

                return true;

        } else {
            // Might just need to break until out of socket_client loop.
#ifdef DEBUGGING
            Serial.println(F("Invalid handshake"));
#endif
            disconnectStream_h1();

            return false;
        }
    } else {
        return false;
    }
}

bool WebSocketClient::analyzeRequest_h1(std::uint32_t timeoutMsec) {
    String temp = "";

    int bite;
    bool foundupgrade = false;
    bool foundsid = false;
    unsigned long intkey[2];
    String serverKey;
    char keyStart[17];
    char b64Key[25];
    String key = "------------------------";

    uint32_t recvMillis = millis();

    if (!issocketio || (issocketio && strlen(sid) > 0)) {

#ifdef DEBUGGING
    Serial.println(F("Sending websocket upgrade headers"));
#endif

#ifndef ARDUINO_ARCH_ESP32
        randomSeed(analogRead(0));
#endif

        for (int i=0; i<16; ++i) {
            keyStart[i] = (char)random(1, 256);
        }

        base64_encode(b64Key, keyStart, 16);

        for (int i=0; i<24; ++i) {
            key[i] = b64Key[i];
        }

        socket_client->print(F("GET "));
        socket_client->print(path);
        if (issocketio) {
            socket_client->print(F("socket.io/?EIO=3&transport=websocket&sid="));
            socket_client->print(sid);
        }
        socket_client->print(F(" HTTP/1.1\r\n"));
        socket_client->print(F("Upgrade: websocket\r\n"));
        socket_client->print(F("Connection: Upgrade\r\n"));
        socket_client->print(F("Sec-WebSocket-Key: "));
        socket_client->print(key);
        socket_client->print(CRLF);
        socket_client->print(F("Sec-WebSocket-Protocol: "));
        socket_client->print(protocol);
        socket_client->print(CRLF);
        socket_client->print(F("Sec-WebSocket-Version: 13\r\n"));

#ifdef DEBUGGING
        Serial.println("Printing websocket upgrade headers");
        Serial.print(F("GET "));
        Serial.print(path);
        if (issocketio) {
            socket_client->print(F("socket.io/?EIO=3&transport=websocket&sid="));
            socket_client->print(sid);
        }
        Serial.print(F(" HTTP/1.1\r\n"));
        Serial.print(F("Upgrade: websocket\r\n"));
        Serial.print(F("Connection: Upgrade\r\n"));
        Serial.print(F("Sec-WebSocket-Key: "));
        Serial.print(key);
        Serial.print(CRLF);
        Serial.print(F("Sec-WebSocket-Protocol: "));
        Serial.print(protocol);
        Serial.print(CRLF);
        Serial.print(F("Sec-WebSocket-Version: 13\r\n"));
#endif


    } else {

#ifdef DEBUGGING
    Serial.println(F("Sending socket.io session request headers"));
#endif

        socket_client->print(F("GET "));
        socket_client->print(path);
        socket_client->print(F("socket.io/?EIO=3&transport=polling HTTP/1.1\r\n"));
        socket_client->print(F("Connection: keep-alive\r\n"));
    }

    socket_client->print(F("Host: "));
    socket_client->print(host);
    socket_client->print(CRLF);
    socket_client->print(CRLF);

#ifdef DEBUGGING
    Serial.println(F("Analyzing response headers"));
#endif

    recvMillis = millis();

    Serial.print(F("Waiting"));
    while (!socket_client->available()) {
        if (!socket_client->connected()) {
            Serial.println();
            Serial.println("Connection diffused");
            return false;
        } else if ((millis() - recvMillis) > timeoutMsec) {
            socket_client->stop();
            Serial.println();
            Serial.println("Connection timeout");
            return false;
        }
        delay(100);
        Serial.print(".");
    }
    Serial.println();

    recvMillis = millis();

    while (true) {
        while((bite = socket_client->read()) == -1) {
            if (!socket_client->connected()) {
                Serial.println("Connection diffused");
                return false;
            } else if ((millis() - recvMillis) > socket_client->getTimeout()) {
                socket_client->stop();
                Serial.printf("Read timeout %lu\n", socket_client->getTimeout());
                return false;
            }
            delay(20);
        }
        recvMillis = millis();
        temp += (char)bite;

        if ((char)bite == '\n') {
            temp.trim();
            if (temp.length()==0) {
                // end of headers
                break;
            }
            String tempLC = temp;
            tempLC.toLowerCase(); // uses case-insensitive header string for parsing to ensure to catch response from servers using different upper/lowercase variants of headers
#ifdef DEBUGGING
            Serial.print("Got Header: " + temp);
#endif
            if (!foundupgrade && tempLC.startsWith("upgrade: websocket")) {
                foundupgrade = true;
            } else if (tempLC.startsWith("sec-websocket-accept: ")) {
                serverKey = temp.substring(22);
            } else if (!foundsid && tempLC.startsWith("set-cookie: ")) {
                 foundsid = true;
                 String tempsid;
                 if (temp.indexOf(";") == -1){ // looks for ";" in cookie header, which indicates more than one cookie value
                   tempsid = temp.substring(temp.indexOf("=") + 1);
                 }
                 else {
                   tempsid = temp.substring(temp.indexOf("=") + 1, temp.indexOf(";")); // assumes sid is first cookie value, discards all other values
                 }
                 strcpy(sid, tempsid.c_str());
                 #ifdef DEBUGGING
                    Serial.println("Parsing Set-Cookie...");
                    Serial.println("tempsid: " + tempsid);
                #endif
            }
            temp = "";
        }
    }

    if (issocketio && foundsid && !foundupgrade) {
        return true;
    }

    key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t *hash;
    char result[21];
    char b64Result[30];

    Sha1.init();
    Sha1.print(key);
    hash = Sha1.result();

    for (int i=0; i<20; ++i) {
        result[i] = (char)hash[i];
    }
    result[20] = '\0';

    base64_encode(b64Result, result, 20);

    // if the keys match, good to go
    return serverKey.equals(String(b64Result));
}

void WebSocketClient::_handle_h2(String *temp) {
    const Http2Frame frame = Http2Frame::fromBytes((const uint8_t*)temp->c_str(), temp->length());
    if (frame.isValidHeader() && (frame.isValidAll() || frame.getType()==Http2Frame::FRAME_TYPE_DATA)) {
#if 0
        if (!h2Status.settingsSent && frame.getType()!=Http2Frame::FRAME_TYPE_SETTINGS) {
#else
        if (false) {
#endif
            // SETTINGS frame must be received first
            // https://http2.github.io/faq/#how-can-i-avoid-keeping-hpack-state
            uint32_t rstReason = 0;
            Http2Frame rstStreamFrame(Http2Frame::FRAME_TYPE_RST_STREAM, Http2Frame::FRAME_FLAG_NONE, 0, sizeof(rstReason), (uint8_t*)&rstReason);
            socket_client->write(rstStreamFrame.toBytes(), rstStreamFrame.bytesSize());
            log_d("Sent RST_STREAM: SETTINGS not received yet");
        } else {
            // parse frame
            switch (frame.getType()) {
            case Http2Frame::FRAME_TYPE_SETTINGS: {
                // SETTINGS frame received
                // not only at the beginning of the connection
                if (frame.getFlags() == Http2Frame::FRAME_FLAG_ACK) {
                    // ACK of our SETTINGS frame
                    h2Status.settingsSent = true;
                    log_d("Received SETTINGS ACK frame[%u]: %d bytes", frame.getStreamId(), frame.getPayloadLength());
                } else {
                    log_d("Received SETTINGS frame[%u]: %d bytes", frame.getStreamId(), frame.getPayloadLength());
                    const Http2Frame::SettingsFramePayload framePayload = Http2Frame::SettingsFramePayload::fromBytes(frame.getPayload(), frame.getPayloadLength());
                    // hexdump(framePayload.toBytes(), framePayload.bytesSize());
                    for (std::size_t i = 0; i < framePayload.buffer_size;) {
                        const Http2Frame::SettingFrameField field = Http2Frame::SettingFrameField::fromBytes(framePayload.toBytes() + i, &i);
                        if (i == 0) {
                            return;  // error
                        }
                        log_d("Received SETTING: id=%d value=%u", field.id, field.value);
                        switch (field.id) {
                        case Http2Frame::SETTINGS_MAX_CONCURRENT_STREAMS:
                            h2Status.serverMaxConcurrentStreams = field.value;
                            break;
                        case Http2Frame::SETTINGS_ENABLE_CONNECT_PROTOCOL:
                            h2Status.enableConnectProtocol = field.value;
                        break;
                        case Http2Frame::SETTINGS_INITIAL_WINDOW_SIZE:
                            h2Status.serverInitialWindowSize = field.value;
                            break;
                        case Http2Frame::SETTINGS_MAX_FRAME_SIZE:
                            h2Status.serverMaxFrameSize = field.value;
                            break;
                        default:
                            break;
                        }
                    }
                    const Http2Frame settingsAckFrame(Http2Frame::FRAME_TYPE_SETTINGS, Http2Frame::FRAME_FLAG_ACK, 0, 0);
                    h2Status.settingsReceived = (settingsAckFrame.bytesSize() == socket_client->write(settingsAckFrame.toBytes(), settingsAckFrame.bytesSize()));
                    log_d("Sent SETTINGS ACK frame[%u]: %d bytes %d", frame.getStreamId() ,settingsAckFrame.bytesSize(), h2Status.settingsReceived);     
                                
                }
            } break;
            case Http2Frame::FRAME_TYPE_WINDOW_UPDATE: {
                    // WINDOW_UPDATE has no flags
                    if (frame.getStreamId() == 0) {
                        // connection-level window update
                        const std::uint32_t increment = ((std::uint32_t)frame.getPayload()[0] << 24) | ((std::uint32_t)frame.getPayload()[1] << 16) | ((std::uint32_t)frame.getPayload()[2] << 8) | (std::uint32_t)frame.getPayload()[3];
                        h2Status.serverWindowSize += increment;
                        log_d("Received connection-level WINDOW_UPDATE frame[%u]: %d bytes inc=%u, new serverWindowSize=%u", frame.getStreamId(), frame.getPayloadLength(), increment, h2Status.serverWindowSize);
                    } else {
                        // !todo sync
                        // stream-level window update
                        const std::uint32_t increment = ((std::uint32_t)frame.getPayload()[0] << 24) | ((std::uint32_t)frame.getPayload()[1] << 16) | ((std::uint32_t)frame.getPayload()[2] << 8) | (std::uint32_t)frame.getPayload()[3];
                        if (h2Stream.find(frame.getStreamId())!=h2Stream.end()) {
                            h2Stream[frame.getStreamId()].serverWindowSize += increment;
                            log_d("Received stream-level WINDOW_UPDATE frame[%u]: %d bytes inc=%u, new streamserverWindowSize=%u", frame.getStreamId(), frame.getPayloadLength(), increment, h2Stream[frame.getStreamId()].serverWindowSize);
                        }
                    }
            } break;
            case Http2Frame::FRAME_TYPE_PING: {
                // PING frame received
                if (frame.getFlags() != Http2Frame::FRAME_FLAG_ACK) {
                    // respond with PING ACK
                    Http2Frame pingAckFrame(Http2Frame::FRAME_TYPE_PING, Http2Frame::FRAME_FLAG_ACK, 0, 0);
                    socket_client->write(pingAckFrame.toBytes(), pingAckFrame.bytesSize());
                    log_d("Sent PING ACK frame[%u]: %d bytes", frame.getStreamId(), pingAckFrame.bytesSize());
                }
                break;
            } break;
            case Http2Frame::FRAME_TYPE_GOAWAY: {
                // GOAWAY frame received
                h2Status.connected = false;
                socket_client->stop();
                log_d("Received GOAWAY frame[%u]: %d bytes, connection closed by server", frame.getStreamId(), frame.getPayloadLength());
            }
            case Http2Frame::FRAME_TYPE_HEADERS: {
                // HEADERS frame received
                log_d("Received HEADERS frame[%u]: %d bytes", frame.getStreamId(), frame.getPayloadLength());
                const Http2Frame::HeadersFramePayload framePayload = Http2Frame::HeadersFramePayload::fromBytes(frame.getPayload(), frame.getPayloadLength());
                // hexdump(framePayload.toBytes(), framePayload.bytesSize());
                for (std::size_t i = 0; i < framePayload.buffer_size;) {
                    const Http2Frame::HeaderFrameField field = Http2Frame::HeaderFrameField::fromBytes(framePayload.toBytes() + i, &i);
                    if (i == 0) {
                        return;  // error
                    }
                    log_d("Received HEADER: %s: %s", field.name.c_str(), field.value.c_str());
                    if (field.name.equals(":status") && field.value.equals("200")) {
                        log_d("WebSocket upgrade successful");
                        h2Stream.insert(std::pair<Http2Frame::StreamIdentifier, H2SendingStream>(frame.getStreamId(), H2SendingStream()));
                    }
                }
                if (frame.getFlags() == Http2Frame::FRAME_FLAG_END_STREAM) {
                    // no DATA frame will be sent
                    log_d("HEADERS frame has END_STREAM flag");
                    h2Stream.erase(frame.getStreamId());
                }
            } break;
            case Http2Frame::FRAME_TYPE_DATA: {
                // DATA frame received
                // DATA frame may be not fully received yet
                if (h2Stream.find(frame.getStreamId())!=h2Stream.end()) {
                    H2SendingStream& stream = h2Stream[frame.getStreamId()];

                    h2Status.receivingData = Http2Status::ReceivingData(frame.getStreamId(), frame.getPayloadLength());
                    h2Status.totalRxSize += frame.getFrameLength();
                    stream.totalRxSize += frame.getFrameLength();
                    log_d("Received DATA frame[%u]: %u bytes totalRx=%u/%u streamRx=%u/%u", frame.getStreamId(), frame.getPayloadLength(), h2Status.totalRxSize, h2Status.clientInitialWindowSize+h2Status.clientWindowSize, stream.totalRxSize, h2Status.clientInitialWindowSize+stream.clientWindowSize);

                    if (((h2Status.clientInitialWindowSize+h2Status.clientWindowSize)-h2Status.totalRxSize)<h2Status.clientInitialWindowKeepSize) {
                        // send WINDOW_UPDATE to increase window size
                        const uint32_t increment = h2Status.totalRxSize-h2Status.clientWindowSize;
                        Http2Frame windowUpdateFrame(Http2Frame::FRAME_TYPE_WINDOW_UPDATE, Http2Frame::FRAME_FLAG_NONE, 0, sizeof(increment), (uint8_t*)&increment);
                        if (windowUpdateFrame.bytesSize() == socket_client->write(windowUpdateFrame.toBytes(), windowUpdateFrame.bytesSize())) {
                            h2Status.clientWindowSize += increment;
                            log_d("Sent connection-level WINDOW_UPDATE: %u", increment);
                        }
                    }                    
                    if ((h2Status.clientInitialWindowSize+stream.clientWindowSize)-stream.totalRxSize<h2Status.clientInitialWindowKeepSize) {
                        // send WINDOW_UPDATE to increase window size
                        const uint32_t increment = stream.totalRxSize-stream.clientWindowSize;
                        Http2Frame windowUpdateFrame(Http2Frame::FRAME_TYPE_WINDOW_UPDATE, Http2Frame::FRAME_FLAG_NONE, frame.getStreamId(), sizeof(increment), (uint8_t*)&increment);
                        if (windowUpdateFrame.bytesSize() == socket_client->write(windowUpdateFrame.toBytes(), windowUpdateFrame.bytesSize())) {
                            stream.clientWindowSize += increment;
                            log_d("Sent stream-level WINDOW_UPDATE: %u", increment);
                        }
                    }
                    if (frame.getFlags() == Http2Frame::FRAME_FLAG_END_STREAM) {
                        // no more DATA frame will be sent
                        log_d("DATA frame has END_STREAM flag");
                        h2Status.receivingData.endStream = true;
                    }
                }
            } break;
            case Http2Frame::FRAME_TYPE_RST_STREAM: {
                // RST_STREAM frame received
                log_d("Received RST_STREAM frame[%u]: %d bytes", frame.getStreamId(), frame.getPayloadLength());
                if (frame.getStreamId()==0) {
                    h2Status.init();
                } else {
                    h2Stream.erase(frame.getStreamId());
                }
            } break;
            default:
                break;
            }
        }
        *temp = temp->substring(frame.bytesSize());  // remove processed frame from buffer
    }

}

WS_SIZE_T WebSocketClient::handleStream() {
    ReceivingFrame* rf;
    Http2Frame::StreamIdentifier sid = 0;
    if (h2Status.connected) {
        // h2
        if (h2Status.receivingData.streamId != 0) {
            // h2 receiving data
            sid = h2Status.receivingData.streamId;
            rf = &h2Stream[sid].receivingFrame;
        } else {
            // h2
            const int bite = waitForByte(socket_client);  // read byte by byte until timeout or end of headers
            if (bite == -1) {
                return WS_SIZE_T_NONE;
            }
#ifdef DEBUGGING
            Serial.print((char)bite);
#endif
            h2TempBuffer += (char)bite;
            _handle_h2(&h2TempBuffer);
            sid = h2Status.receivingData.streamId;
            if (sid == 0) {
                return WS_SIZE_T_NONE;
            }
            rf = &h2Stream[sid].receivingFrame;
        }
    } else {
        // h1
        rf = &receivingFrame;
    }
    const WS_SIZE_T r = _handleStream(rf, sid);
    if (h2Status.connected && r == WS_SIZE_T_HEADER) {
        if (h2Status.receivingData.subtract(1)) {
            // end stream
            h2Stream.erase(sid);
        }
    }
    return r;
}

WS_SIZE_T WebSocketClient::_handleStream(WebSocketClient::ReceivingFrame *rf, Http2Frame::StreamIdentifier streamId) {
    if (rf->state != WS_FRAME_OPCODE) {
        if ((millis() - rf->_startMillis) > socket_client->getTimeout()) {
            // timeout
            rf->state = WS_FRAME_OPCODE;
            return WS_SIZE_T_NONE;
        }
    }
    if (!socket_client->connected() || !socket_client->available()){
        return WS_SIZE_T_NONE;
    } else if (rf->state == WS_FRAME_PAYLOAD) {
        return rf->frame.length-rf->index;
    }
    const int r = socket_client->read();
    if (r < 0) {
        return WS_SIZE_T_NONE;
    }
    rf->_startMillis = millis();
    switch(rf->state) {
        case WS_FRAME_OPCODE: {
            const uint8_t finOpcode = r;
            if ((finOpcode & WS_RSV) != 0) {
                // MUST be 0 unless an extension is negotiated that defines meanings
                // for non-zero values.  If a nonzero value is received and none of
                // the negotiated extensions defines the meaning of such a nonzero
                // value, the receiving endpoint MUST _Fail the WebSocket
                // Connection_.
                sendData((const char*)WS_CLOSE_BAD_REQUEST, sizeof(WS_CLOSE_BAD_REQUEST)-1, WS_OPCODE_CLOSE, streamId);
                socket_client->stop();
                return WS_SIZE_T_NONE;
            } else {
                rf->frame.fin = ((finOpcode & WS_FIN) != 0);
                rf->frame.opcode = finOpcode&(~WS_FIN);
                switch (rf->frame.opcode) {
                case WS_OPCODE_CONT:
                case WS_OPCODE_TEXT:
                case WS_OPCODE_BINARY:
                case WS_OPCODE_CLOSE:
                case WS_OPCODE_PING:
                case WS_OPCODE_PONG:
                    rf->state = WS_FRAME_LENGTH_8;
                    log_v("opcode: %d %x", rf->frame.fin, rf->frame.opcode);
                    break;
                default:
                    sendData((const char*)WS_CLOSE_UNSUPPORTED, sizeof(WS_CLOSE_UNSUPPORTED)-1, WS_OPCODE_CLOSE, streamId);
                    socket_client->stop();
                    return WS_SIZE_T_NONE;
                    // If an unknown
                    // opcode is received, the receiving endpoint MUST _Fail the
                    // WebSocket Connection_.
                    break;
                }
            }
        } break;
        case WS_FRAME_LENGTH_8: {
            const uint8_t maskLen = r;
            rf->frame.hasMask = (bool)(maskLen & WS_MASK);
            const uint8_t len = maskLen & (~WS_MASK);
            rf->index = 0;
            if (len == WS_SIZE16) {
                rf->frame.length = 0;
                rf->state = WS_FRAME_LENGTH_16;
            } else if (len == WS_SIZE64) {
                rf->frame.length = 0;
                rf->state = WS_FRAME_LENGTH_64;
            } else {
                rf->frame.length = (WS_SIZE_T)len;
                if (rf->frame.hasMask) {
                    rf->state = WS_FRAME_MASK;
                } else {
                    rf->state = WS_FRAME_PAYLOAD;
                }
                log_v("length8: %d %u", rf->frame.hasMask, rf->frame.length);
            }
        } break;
        case WS_FRAME_LENGTH_16: {
            rf->frame.length = (rf->frame.length<<8 | (WS_SIZE_T)r);
            rf->index++;
            if (rf->index == 2) {
                rf->index = 0;
                if (rf->frame.hasMask) {
                    rf->state = WS_FRAME_MASK;
                } else {
                    rf->state = WS_FRAME_PAYLOAD;
                }
                log_v("length16: %d %u", rf->frame.hasMask, rf->frame.length);
            }
        } break;
        case WS_FRAME_LENGTH_64: {
            rf->frame.length = (rf->frame.length<<8 | (WS_SIZE_T)r);
            rf->index++;
            if (rf->index == 8) {
                rf->index = 0;
                if (rf->frame.hasMask) {
                    rf->state = WS_FRAME_MASK;
                } else {
                    rf->state = WS_FRAME_PAYLOAD;
                }
                log_v("length64: %d %u", rf->frame.hasMask, rf->frame.length);
            }
        } break;
        case WS_FRAME_MASK: {
            rf->frame.mask[rf->index++] = r;
            if (rf->index == 4) {
                rf->index = 0;
                rf->state = WS_FRAME_PAYLOAD;
                log_v("mask: %02x %02x %02x %02x", rf->frame.mask[0], rf->frame.mask[1], rf->frame.mask[2], rf->frame.mask[3]);
            }
        }
    }
    return WS_SIZE_T_HEADER;
}

bool WebSocketClient::getData(String& str, uint8_t *opcode, Http2Frame::StreamIdentifier *streamId) {
    const int remain = handleStream();
    log_v("getData remain: %d", remain);
    if (remain == WS_SIZE_T_HEADER) {
      return false;
    } else if (0 <= (int)remain) {
      char data[remain];  // reserve space
      const std::size_t len = getData(data, (std::size_t)remain, opcode, streamId);
      str += data;
      return len == remain;
    }
    return false;
}

std::size_t WebSocketClient::getData(char *data, std::size_t length, uint8_t *opcode, Http2Frame::StreamIdentifier* streamId) {
    const int remain = handleStream();
    ReceivingFrame* rf;
    if (!h2Status.connected) {
        // h1
        rf = &receivingFrame;
    } else {
        // h2
        if (h2Stream.find(h2Status.receivingData.streamId) != h2Stream.end()) {
            rf = &h2Stream[h2Status.receivingData.streamId].receivingFrame;
            if (streamId != NULL) {
                *streamId = h2Status.receivingData.streamId;
            } else {
                log_w("Cannot specify streamId because streamId pointer is NULL");
            }
        } else {
            log_e("Stream ID %u not found", h2Status.receivingData.streamId);
            return 0;
        }
    }
    if (!data || rf->state != WS_FRAME_PAYLOAD) {
        if ((int)remain < 0) {
            return 0;
        } else if (!data) {
            return remain;
        } else if (rf->state != WS_FRAME_PAYLOAD) {
            return 0;
        }
    } else if (!socket_client->connected() || !socket_client->available()){
        log_d("socket not connected or no data available");
        return 0;
    }
    if (opcode != NULL) {
        *opcode = rf->frame.opcode;
    }
    std::size_t len = 0;
    if (h2Status.connected) {
        const std::size_t l = std::min(length, (std::size_t)h2Status.receivingData.remainLength);
        len = socket_client->readBytes(data, l);
        if(h2Status.receivingData.subtract(len)) {
            // end stream
            h2Stream.erase(*streamId);
        }
    } else {
        len = socket_client->readBytes(data, length);
    }
    if (rf->frame.hasMask) {
        // unmask the data
        for (int i=0; i<std::min(len, length); ++i) {
            data[i] = data[i] ^ rf->frame.mask[(rf->index++) % 4];
        }
    } else {
        // no mask
        rf->index += len;
    }
    if (rf->frame.length == rf->index) {
        // end
        rf->state = WS_FRAME_OPCODE;
    }
    rf->_startMillis = millis();
    return length;
}

void WebSocketClient::disconnectStream_h1() {
#ifdef DEBUGGING
    Serial.println(F("Terminating socket"));
#endif
    // Should send 0x8700 to server to tell it I'm quitting here.
    socket_client->write((uint8_t) 0x87);
    socket_client->write((uint8_t) 0x00);

    socket_client->flush();
    delay(10);
    socket_client->stop();
    strcpy(sid, "");
}

std::size_t WebSocketClient::sendData(const char *str, std::size_t size, uint8_t opcode, Http2Frame::StreamIdentifier streamId) {
#ifdef DEBUGGING
    Serial.print(F("Sending data: "));
    Serial.println(str);
#endif
    if (socket_client->connected()) {
        uint8_t mask[4];
        int size_buf = size + 1;
        if (size > 125) {
            size_buf += 3;
        } else {
            size_buf += 1;
        }
        if (WS_MASK > 0) {
            size_buf += 4;
        }
        char buf[size_buf];
        char* p=buf;

        // Opcode; final fragment
        *p++ = (char)(opcode | WS_FIN);

        // NOTE: no support for > 16-bit sized messages
        if (size > 125) {
            *p++ = (char) (WS_SIZE16 | WS_MASK);
            *p++ = (char) (size >> 8);
            *p++ = (char) (size & 0xFF);
        } else {
            *p++ = (char) (size | WS_MASK);
        }

        if (WS_MASK > 0) {
            mask[0] = random(0, 256);
            mask[1] = random(0, 256);
            mask[2] = random(0, 256);
            mask[3] = random(0, 256);

            *p++ = (char) mask[0];
            *p++ = (char) mask[1];
            *p++ = (char) mask[2];
            *p++ = (char) mask[3];

            for (int i=0; i<size; ++i) {
                *p++ = str[i] ^ mask[i % 4];
            }
        } else {
            memcpy(p, str, size); p+=size;
        }
        *p++ = '\0';

        if (!h2Status.connected) {
            const std::size_t r = socket_client->write((uint8_t*)buf, size_buf);
            return r;
        } else {
            if (h2Stream.find(streamId) != h2Stream.end()) {
                // send DATA frame
                const Http2Frame dataFrame(Http2Frame::FRAME_TYPE_DATA, Http2Frame::FRAME_FLAG_NONE, streamId, size_buf, (uint8_t*)buf);
                const std::size_t r = socket_client->write(dataFrame.toBytes(), dataFrame.bytesSize());
                h2Status.totalTxSize+=dataFrame.bytesSize();
                h2Stream[streamId].totalTxSize+=dataFrame.bytesSize();
                log_d("Sent DATA frame[%u], length=%u totalTx=%u/%u streamTX=%U/%U", streamId, dataFrame.bytesSize(), h2Status.totalTxSize, h2Status.serverInitialWindowSize+h2Status.serverWindowSize, h2Stream[streamId].totalTxSize, h2Status.serverInitialWindowSize+h2Stream[streamId].serverWindowSize);
                return r - Http2Frame::Http2FrameHeaderSize;
            } else {
                log_e("Stream ID %u not found", streamId);
            }
        }
    }
    return 0;
}