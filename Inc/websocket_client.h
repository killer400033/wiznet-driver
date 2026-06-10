#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// WebSocket constants
#define WS_KEY_LEN 16
#define WS_ACCEPT_LEN 28
#define WS_MAX_HEADER_LEN 14

// WebSocket error codes
#define WS_OK                    0   // Success
#define WS_ERR_INVALID_PARAM    -1   // Invalid parameter
#define WS_ERR_NOT_INITIALIZED  -2   // Client not initialized
#define WS_ERR_WRONG_STATE      -3   // Wrong state for operation
#define WS_ERR_SOCKET_FAIL      -4   // Socket operation failed
#define WS_ERR_CONNECT_FAIL     -5   // Connection failed
#define WS_ERR_HANDSHAKE_FAIL   -6   // WebSocket handshake failed
#define WS_ERR_TIMEOUT          -7   // Operation timed out
#define WS_ERR_BUFFER_TOO_SMALL -8   // Buffer too small
#define WS_ERR_DISCONNECTED     -9   // Connection closed
#define WS_ERR_INVALID_FRAME    -10  // Invalid WebSocket frame

// WebSocket opcodes
typedef enum {
    WS_OPCODE_CONTINUATION = 0x0,
    WS_OPCODE_TEXT = 0x1,
    WS_OPCODE_BINARY = 0x2,
    WS_OPCODE_CLOSE = 0x8,
    WS_OPCODE_PING = 0x9,
    WS_OPCODE_PONG = 0xA
} ws_opcode_t;

// WebSocket client state
typedef enum {
    WS_STATE_DISCONNECTED,
    WS_STATE_CONNECTED,
    WS_STATE_CLOSING,
} ws_state_t;

// WebSocket client configuration
typedef struct {
    uint8_t socket_num;      // Socket number to use
    uint8_t host[4];         // Server IP address
    uint16_t port;           // Server port
    char path[128];          // WebSocket path
    uint8_t* tx_buf;         // TX buffer for socket
    uint16_t tx_buf_len;     // TX buffer length
    uint8_t* rx_buf;         // RX buffer for socket
    uint16_t rx_buf_len;     // RX buffer length
} ws_config_t;

/**
 * @brief Initialize WebSocket client
 * @param config Configuration structure with connection parameters and buffers
 * @return WS_OK on success, negative error code on failure
 */
int8_t ws_client_init(ws_config_t* config);

/**
 * @brief Connect to WebSocket server
 * @return WS_OK on success, negative error code on failure
 */
int8_t ws_client_connect(void);

/**
 * @brief Send binary frame
 * @param buffer Buffer containing payload data with WS_MAX_HEADER_LEN bytes reserved at the front
 * @param payload_len Length of payload data (NOT including header space)
 * @return Total frame length (header + payload) on success, negative error code on failure
 * @note Buffer must have WS_MAX_HEADER_LEN bytes of free space before the payload data
 * @note The function will write the WebSocket frame header and mask the payload in-place
 */
int16_t ws_client_send_binary(uint8_t* buffer, uint16_t payload_len);

/**
 * @brief Send text frame
 * @param buffer Buffer containing payload data with WS_MAX_HEADER_LEN bytes reserved at the front
 * @param payload_len Length of payload data (NOT including header space)
 * @return Total frame length (header + payload) on success, negative error code on failure
 * @note Buffer must have WS_MAX_HEADER_LEN bytes of free space before the payload data
 * @note The function will write the WebSocket frame header and mask the payload in-place
 */
int16_t ws_client_send_text(uint8_t* buffer, uint16_t payload_len);

/**
 * @brief Process WebSocket connection and receive data
 * @param buffer Buffer to store received payload (or NULL to just process state)
 * @param len Buffer size
 * @param opcode Pointer to store received opcode (or NULL if buffer is NULL)
 * @return Length of received data on success, 0 if no data, negative error code on failure
 * @note Call this regularly to process connection state changes and receive frames
 * @note Can be called with buffer=NULL to just update state without receiving data
 */
int16_t ws_client_process(uint8_t* buffer, uint16_t len, ws_opcode_t* opcode);

/**
 * @brief Send ping frame
 * @return WS_OK on success, negative error code on failure
 */
int8_t ws_client_ping(void);

/**
 * @brief Close WebSocket connection
 * @return WS_OK on success, negative error code on failure
 */
int8_t ws_client_close(void);

/**
 * @brief Get current WebSocket state
 * @return Current state
 */
ws_state_t ws_client_get_state(void);

/**
 * @brief External random number generator (must be implemented by user)
 * @return Random 32-bit unsigned integer
 * @note This function must be provided by the application
 * @note Should return a different value each time for proper masking
 */
extern uint32_t ws_rand(void);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_CLIENT_H
