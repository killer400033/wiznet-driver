#include "websocket_client.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "socket.h"
#include "cmsis_os.h"
#include "log_handler.h"

// Global WebSocket client state
static ws_config_t g_ws_config = {0};
static ws_state_t g_ws_state = WS_STATE_DISCONNECTED;
static char g_sec_websocket_key[25] = {0};
static bool g_ws_initialized = false;

// Event flags set by socket callbacks (ISR context)
static volatile bool g_timeout_flag = false;
static volatile bool g_disconnect_req_flag = false;
static volatile bool g_disconnect_suc_flag = false;
static uint32_t g_close_time = 0;

// Working buffers
static uint8_t ws_rx_temp[2048];  // Temporary buffer for receiving frames (Make sure this is large enough for largest packet from server, otherwise it won't receive anything)
static uint8_t ws_tx_temp[64];   // Temporary buffer for ping/pong/close frames

// Helper functions
static void ws_generate_key(char *key);
static void ws_base64_encode(const uint8_t *input, uint16_t input_len, char *output);
static int8_t ws_parse_handshake_response(const char *response);
static uint16_t ws_create_frame_header(uint8_t *buffer, ws_opcode_t opcode, uint16_t payload_len, bool mask, uint8_t *masking_key);
static void ws_mask_payload(uint8_t *payload, uint16_t len, const uint8_t *mask);
static void ws_socket_callback(socket_callback_type_t type, void* data);
static int16_t ws_process_internal(uint8_t* buffer, uint16_t len, ws_opcode_t* opcode);
static inline void ws_generate_masking_key(uint8_t *masking_key);

// Base64 encoding table
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Generate 4-byte masking key from 32-bit random value
 */
static inline void ws_generate_masking_key(uint8_t *masking_key) {
    uint32_t rand_val = ws_rand();
    masking_key[0] = (rand_val >> 24) & 0xFF;
    masking_key[1] = (rand_val >> 16) & 0xFF;
    masking_key[2] = (rand_val >> 8) & 0xFF;
    masking_key[3] = rand_val & 0xFF;
}

/**
 * @brief Initialize WebSocket client
 */
int8_t ws_client_init(ws_config_t* config) {
    if (config == NULL) {
        return WS_ERR_INVALID_PARAM;
    }
    
    // Validate parameters
    if (config->socket_num >= _WIZCHIP_SOCK_NUM_ || 
        config->tx_buf == NULL || config->rx_buf == NULL ||
        config->tx_buf_len == 0 || config->rx_buf_len == 0) {
        return WS_ERR_INVALID_PARAM;
    }
    
    // Copy configuration
    memcpy(&g_ws_config, config, sizeof(ws_config_t));
    
    // Clear flags
    g_timeout_flag = false;
    g_disconnect_req_flag = false;
    g_disconnect_suc_flag = false;
    
    g_ws_state = WS_STATE_DISCONNECTED;
    g_ws_initialized = true;
    
    return WS_OK;
}

/**
 * @brief Connect to WebSocket server
 */
int8_t ws_client_connect(void) {
    if (!g_ws_initialized) {
        return WS_ERR_NOT_INITIALIZED;
    }
    
    // Clear event flags
    g_timeout_flag = false;
    g_disconnect_req_flag = false;
    g_disconnect_suc_flag = false;
    
    // Check socket status - if not closed, close it first
    socket_status_t status = getSocketStatus(g_ws_config.socket_num);
    if (status != SOCKET_CLOSED) {
        close(g_ws_config.socket_num);
    }
    
    // Create TCP socket with callback
    uint16_t source_port = 30000 + (HAL_GetTick() % 10000); // Random port between 30000-39999
    int8_t result = socket(g_ws_config.socket_num, SOCKET_PROTOCOL_TCP, source_port, 
                          g_ws_config.tx_buf, g_ws_config.tx_buf_len, 
                          g_ws_config.rx_buf, g_ws_config.rx_buf_len, ws_socket_callback);
    if (result != SOCK_OK) {
        g_ws_state = WS_STATE_DISCONNECTED;
        return WS_ERR_SOCKET_FAIL;
    }
    
    result = connect(g_ws_config.socket_num, g_ws_config.host, g_ws_config.port);
    if (result != SOCK_OK) {
        g_ws_state = WS_STATE_DISCONNECTED;
        close(g_ws_config.socket_num);
        return WS_ERR_CONNECT_FAIL;
    }
    
    // Generate WebSocket key
    ws_generate_key(g_sec_websocket_key);
    
    // Build and send WebSocket handshake
    char handshake[512];
    snprintf(handshake, sizeof(handshake),
        "GET %s HTTP/1.1\r\n"
        "Host: %d.%d.%d.%d:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        g_ws_config.path, 
        g_ws_config.host[0], g_ws_config.host[1], 
        g_ws_config.host[2], g_ws_config.host[3], 
        g_ws_config.port, g_sec_websocket_key);
    
    int32_t sent = send(g_ws_config.socket_num, (uint8_t*)handshake, strlen(handshake));
    if (sent != SOCK_OK) {
        g_ws_state = WS_STATE_DISCONNECTED;
        close(g_ws_config.socket_num);
        return WS_ERR_SOCKET_FAIL;
    }
    
    // Wait for handshake response (with timeout)
    uint32_t start_time = HAL_GetTick();
    uint16_t response_len = 0;
    
    while ((HAL_GetTick() - start_time) < 6000) {
        uint32_t received = recv(g_ws_config.socket_num, ws_rx_temp + response_len, 
                                sizeof(ws_rx_temp) - response_len);
        if (received > 0) {
            response_len += received;
            ws_rx_temp[response_len] = '\0';
            
            // Check if we have complete response (ends with \r\n\r\n)
            if (strstr((char*)ws_rx_temp, "\r\n\r\n") != NULL) {
                if (ws_parse_handshake_response((char*)ws_rx_temp) == WS_OK) {
                    g_ws_state = WS_STATE_CONNECTED;
                    return WS_OK;
                } else {
                    g_ws_state = WS_STATE_DISCONNECTED;
                    close(g_ws_config.socket_num);
                    return WS_ERR_HANDSHAKE_FAIL;
                }
            }
        }
        osDelay(100);
    }
    
    // Timeout
    g_ws_state = WS_STATE_DISCONNECTED;
    close(g_ws_config.socket_num);
    return WS_ERR_TIMEOUT;
}

/**
 * @brief Send binary frame
 */
int16_t ws_client_send_binary(uint8_t* buffer, uint16_t payload_len) {
    if (!g_ws_initialized) {
        return WS_ERR_NOT_INITIALIZED;
    }
    
    if (buffer == NULL) {
        return WS_ERR_INVALID_PARAM;
    }
    
    if (g_ws_state != WS_STATE_CONNECTED) {
        return WS_ERR_WRONG_STATE;
    }
    
    // Generate masking key (uses full 32-bit random value)
    uint8_t masking_key[4];
    ws_generate_masking_key(masking_key);
    
    // Calculate header size
    uint16_t header_len = 2;  // Base header (FIN+opcode, MASK+len)
    if (payload_len >= 126) {
        header_len += 2;  // Extended 16-bit length
    }
    header_len += 4;  // Masking key
    
    // Check if we have enough space
    if (header_len > WS_MAX_HEADER_LEN) {
        return WS_ERR_INVALID_PARAM;
    }
    
    // Write header at the beginning of buffer (before payload)
    uint8_t *header_start = buffer - header_len;
    uint16_t actual_header_len = ws_create_frame_header(header_start, WS_OPCODE_BINARY, 
                                                         payload_len, true, masking_key);
    
    // Mask the payload in-place
    ws_mask_payload(buffer, payload_len, masking_key);
    
    // Send frame (header + masked payload)
    uint16_t total_len = actual_header_len + payload_len;
    int32_t sent = send(g_ws_config.socket_num, header_start, total_len);
    
    return (sent == SOCK_OK) ? total_len : WS_ERR_SOCKET_FAIL;
}

/**
 * @brief Send text frame
 */
int16_t ws_client_send_text(uint8_t* buffer, uint16_t payload_len) {
    if (!g_ws_initialized) {
        return WS_ERR_NOT_INITIALIZED;
    }

    if (buffer == NULL) {
        return WS_ERR_INVALID_PARAM;
    }

    if (g_ws_state != WS_STATE_CONNECTED) {
        return WS_ERR_WRONG_STATE;
    }

    uint8_t masking_key[4];
    ws_generate_masking_key(masking_key);

    uint16_t header_len = 2;
    if (payload_len >= 126) {
        header_len += 2;
    }
    header_len += 4;

    if (header_len > WS_MAX_HEADER_LEN) {
        return WS_ERR_INVALID_PARAM;
    }

    uint8_t *header_start = buffer - header_len;
    uint16_t actual_header_len = ws_create_frame_header(header_start, WS_OPCODE_TEXT,
                                                         payload_len, true, masking_key);

    ws_mask_payload(buffer, payload_len, masking_key);

    uint16_t total_len = actual_header_len + payload_len;
    int32_t sent = send(g_ws_config.socket_num, header_start, total_len);

    return (sent == SOCK_OK) ? total_len : WS_ERR_SOCKET_FAIL;
}

/**
 * @brief Process WebSocket connection and receive data
 */
int16_t ws_client_process(uint8_t* buffer, uint16_t len, ws_opcode_t* opcode) {
    if (!g_ws_initialized) {
        return WS_ERR_NOT_INITIALIZED;
    }

    // Check socket status
    socket_status_t status = getSocketStatus(g_ws_config.socket_num);
    if (status != SOCKET_ESTABLISHED) {
        g_ws_state = WS_STATE_DISCONNECTED;
        return WS_ERR_DISCONNECTED;
    }
    
    // Process event flags from callbacks
    if (g_timeout_flag) {
        g_timeout_flag = false;
        close(g_ws_config.socket_num);
        g_ws_state = WS_STATE_DISCONNECTED;
        return WS_ERR_TIMEOUT;
    }
    
    if (g_disconnect_req_flag) {
        g_disconnect_req_flag = false;
        // Disconnect requested at TCP level - close socket
        disconnect(g_ws_config.socket_num);
        g_ws_state = WS_STATE_DISCONNECTED;
        return WS_ERR_DISCONNECTED;
    }
    
    if (g_disconnect_suc_flag) {
        g_disconnect_suc_flag = false;
        // TCP disconnect completed successfully
        close(g_ws_config.socket_num);
        g_ws_state = WS_STATE_DISCONNECTED;
        return WS_ERR_DISCONNECTED;
    }
    
    // Check current state
    if (g_ws_state == WS_STATE_CLOSING) {
        // Check timeout waiting for close frame acknowledgment
        if (osKernelGetTickCount() - g_close_time > 5000) {
            // Timeout waiting for close acknowledgment - force close
            close(g_ws_config.socket_num);
            g_ws_state = WS_STATE_DISCONNECTED;
            return WS_ERR_DISCONNECTED;
        }
        // Still waiting for CLOSE frame response, just update state
        return WS_OK;
    }
    
    // Check current state
    if (g_ws_state != WS_STATE_CONNECTED) {
        return WS_ERR_WRONG_STATE;
    }

    // Process incoming data
    return ws_process_internal(buffer, len, opcode);
}

/**
 * @brief Internal function to process received frames
 */
static int16_t ws_process_internal(uint8_t* buffer, uint16_t len, ws_opcode_t* opcode) {
    // Try to receive data
    uint32_t received = recv(g_ws_config.socket_num, ws_rx_temp, sizeof(ws_rx_temp));
    if (received == 0) {
        return WS_OK;  // No data available
    }

    // Parse frame header
    if (received < 2) {
        return WS_ERR_INVALID_FRAME;
    }

    uint16_t pos = 0;
    
    // First byte: FIN + opcode
    //bool fin = (ws_rx_temp[pos] & 0x80) != 0;
    *opcode = (ws_opcode_t)(ws_rx_temp[pos] & 0x0F);
    pos++;
    
    // Second byte: MASK + payload length
    bool mask = (ws_rx_temp[pos] & 0x80) != 0;
    uint16_t payload_len = ws_rx_temp[pos] & 0x7F;
    pos++;
    
    // Extended payload length
    if (payload_len == 126) {
        if (received < pos + 2) return WS_ERR_INVALID_FRAME;
        payload_len = (ws_rx_temp[pos] << 8) | ws_rx_temp[pos + 1];
        pos += 2;
    } else if (payload_len == 127) {
        return WS_ERR_INVALID_FRAME;  // 64-bit length not supported
    }
    
    // Masking key (servers should not mask, but handle it anyway)
    uint8_t masking_key[4] = {0};
    if (mask) {
        if (received < pos + 4) return WS_ERR_INVALID_FRAME;
        memcpy(masking_key, &ws_rx_temp[pos], 4);
        pos += 4;
    }
    
    // Check if we have complete payload
    if (received < pos + payload_len) {
        return WS_ERR_INVALID_FRAME;  // Incomplete frame
    }
    
    // Handle different opcodes
    switch (*opcode) {
        case WS_OPCODE_TEXT:
        case WS_OPCODE_BINARY:
            // If buffer is NULL, don't bother copying the payload
            if (buffer == NULL) return WS_OK;

            if (payload_len > len) {
                return WS_ERR_BUFFER_TOO_SMALL;
            }
            memcpy(buffer, &ws_rx_temp[pos], payload_len);
            if (mask) {
                ws_mask_payload(buffer, payload_len, masking_key);
            }
            return payload_len;
            
        case WS_OPCODE_PING:
            // Respond with pong
            {
                uint8_t pong_masking_key[4];
                ws_generate_masking_key(pong_masking_key);
                uint16_t pong_header_len = ws_create_frame_header(ws_tx_temp, WS_OPCODE_PONG, 
                                                                   payload_len, true, pong_masking_key);
                // Copy and mask payload
                if (payload_len > 0) {
                    memcpy(ws_tx_temp + pong_header_len, &ws_rx_temp[pos], payload_len);
                    ws_mask_payload(ws_tx_temp + pong_header_len, payload_len, pong_masking_key);
                }
                send(g_ws_config.socket_num, ws_tx_temp, pong_header_len + payload_len);
            }
            break;
            
        case WS_OPCODE_PONG:
            break;
            
        case WS_OPCODE_CLOSE:
            // Server sent close frame
            if (g_ws_state == WS_STATE_CONNECTED) {
                // Server initiated close, send acknowledgment
                uint8_t close_masking_key[4];
                ws_generate_masking_key(close_masking_key);
                uint16_t close_header_len = ws_create_frame_header(ws_tx_temp, WS_OPCODE_CLOSE, 
                                                                    0, true, close_masking_key);
                send(g_ws_config.socket_num, ws_tx_temp, close_header_len);
                // Send close frame might not work due to send failing or delay not being enough,
                // but isn't too important as we will close the socket anyway.
                osDelay(2000);

                // Close the underlying TCP connection
                disconnect(g_ws_config.socket_num);
                g_ws_state = WS_STATE_DISCONNECTED;
            }
            else if (g_ws_state == WS_STATE_CLOSING) {
                // We initiated close, server responded - close TCP connection
                disconnect(g_ws_config.socket_num);
                g_ws_state = WS_STATE_DISCONNECTED;
            }
            break;
            
        default:
            break;
    }
    
    return WS_OK;
}

/**
 * @brief Send ping frame
 */
int8_t ws_client_ping(void) {
    if (!g_ws_initialized) {
        return WS_ERR_NOT_INITIALIZED;
    }
    
    if (g_ws_state != WS_STATE_CONNECTED) {
        return WS_ERR_WRONG_STATE;
    }
    
    uint8_t masking_key[4];
    ws_generate_masking_key(masking_key);
    
    uint16_t header_len = ws_create_frame_header(ws_tx_temp, WS_OPCODE_PING, 0, true, masking_key);
    int32_t sent = send(g_ws_config.socket_num, ws_tx_temp, header_len);
    
    return (sent == SOCK_OK) ? WS_OK : WS_ERR_SOCKET_FAIL;
}

/**
 * @brief Close WebSocket connection
 */
int8_t ws_client_close(void) {
    if (!g_ws_initialized) {
        return WS_ERR_NOT_INITIALIZED;
    }
    
    if (g_ws_state == WS_STATE_CONNECTED) {
        // Send close frame
        uint8_t masking_key[4];
        ws_generate_masking_key(masking_key);
        
        uint16_t header_len = ws_create_frame_header(ws_tx_temp, WS_OPCODE_CLOSE, 0, true, masking_key);
        send(g_ws_config.socket_num, ws_tx_temp, header_len);
        g_ws_state = WS_STATE_CLOSING;
        g_close_time = osKernelGetTickCount();
    }
    
    return WS_OK;
}

/**
 * @brief Get current WebSocket state
 */
ws_state_t ws_client_get_state(void) {
    return g_ws_state;
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Generate random WebSocket key
 */
static void ws_generate_key(char *key) {
    uint8_t raw_key[WS_KEY_LEN];
    
    // Generate 16 random bytes
    for (int i = 0; i < WS_KEY_LEN; i++) {
        raw_key[i] = (uint8_t)(ws_rand() & 0xFF);
    }
    
    // Base64 encode
    ws_base64_encode(raw_key, WS_KEY_LEN, key);
    key[24] = '\0';
}

/**
 * @brief Base64 encoding
 */
static void ws_base64_encode(const uint8_t *input, uint16_t input_len, char *output) {
    uint16_t i, j;
    
    for (i = 0, j = 0; i < input_len; i += 3, j += 4) {
        uint32_t octet_a = i < input_len ? input[i] : 0;
        uint32_t octet_b = i + 1 < input_len ? input[i + 1] : 0;
        uint32_t octet_c = i + 2 < input_len ? input[i + 2] : 0;
        
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        output[j] = base64_table[(triple >> 18) & 0x3F];
        output[j + 1] = base64_table[(triple >> 12) & 0x3F];
        output[j + 2] = i + 1 < input_len ? base64_table[(triple >> 6) & 0x3F] : '=';
        output[j + 3] = i + 2 < input_len ? base64_table[triple & 0x3F] : '=';
    }
}

/**
 * @brief Socket callback - called from ISR context
 */
static void ws_socket_callback(socket_callback_type_t type, void* data) {
    switch (type) {
        case SOCKET_TIMEOUT_CALLBACK:
            g_timeout_flag = true;
            break;
            
        case SOCKET_DISCON_REQ_CALLBACK:
            g_disconnect_req_flag = true;
            break;
            
        case SOCKET_DISCON_SUC_CALLBACK:
            g_disconnect_suc_flag = true;
            break;
            
        case SOCKET_RECV_CALLBACK:
            // Data available - will be processed in ws_client_process
            break;
            
        default:
            break;
    }
}

/**
 * @brief Parse handshake response
 */
static int8_t ws_parse_handshake_response(const char *response) {
    // Check for "101 Switching Protocols"
    if (strstr(response, "101") == NULL || strstr(response, "Switching Protocols") == NULL) {
        return WS_ERR_HANDSHAKE_FAIL;
    }
    
    // Should also verify Sec-WebSocket-Accept header for production use
    return WS_OK;
}

/**
 * @brief Create WebSocket frame header
 */
static uint16_t ws_create_frame_header(uint8_t *buffer, ws_opcode_t opcode, 
                                       uint16_t payload_len, bool mask, uint8_t *masking_key) {
    uint16_t pos = 0;
    
    // First byte: FIN + opcode
    buffer[pos++] = 0x80 | (opcode & 0x0F);
    
    // Second byte: MASK + payload length
    if (payload_len < 126) {
        buffer[pos++] = (mask ? 0x80 : 0x00) | payload_len;
    } else {
        buffer[pos++] = (mask ? 0x80 : 0x00) | 126;
        buffer[pos++] = (payload_len >> 8) & 0xFF;
        buffer[pos++] = payload_len & 0xFF;
    }
    
    // Masking key (if masked)
    if (mask && masking_key != NULL) {
        memcpy(&buffer[pos], masking_key, 4);
        pos += 4;
    }
    
    return pos;
}

/**
 * @brief Mask/unmask payload
 */
static void ws_mask_payload(uint8_t *payload, uint16_t len, const uint8_t *mask) {
    for (uint16_t i = 0; i < len; i++) {
        payload[i] ^= mask[i % 4];
    }
}
