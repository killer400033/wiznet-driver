#ifndef SOCKET_H
#define SOCKET_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "w5500_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

// Socket protocol types (match Sn_MR register values)
#define SOCKET_PROTOCOL_TCP     0x01
#define SOCKET_PROTOCOL_UDP     0x02

typedef enum {
    SOCKET_CLOSED,
    SOCKET_INIT,
    SOCKET_LISTEN,
    SOCKET_ESTABLISHED,
    SOCKET_CLOSE_WAIT,
    SOCKET_UDP,
    SOCKET_OTHER, // Used for all other socket statuses, (which aren't relevant to the user)
} socket_status_t;

// Socket callback types
typedef enum {
    SOCKET_DISCON_REQ_CALLBACK = 0,
    SOCKET_DISCON_SUC_CALLBACK = 1,
    SOCKET_RECV_CALLBACK = 2,
    SOCKET_TIMEOUT_CALLBACK = 3
} socket_callback_type_t;

// Socket callback function pointer
typedef void (*socket_callback_t)(socket_callback_type_t type, void* data);


// Error codes
#define SOCK_OK                  0   // Success
#define SOCKERR_INVALID_PARAM   -1   // Invalid parameter (socket number, null pointer, etc.)
#define SOCKERR_TIMEOUT         -2   // Operation timed out
#define SOCKERR_QUEUE_FULL      -3   // Command Queue is full
#define SOCKERR_INVALID_STATE   -4   // Socket in invalid state
#define SOCKERR_TCP_TIMEOUT     -5   // TCP timeout
#define SOCKERR_TXBUF_FULL     -6   // TX Buffer is full
#define SOCKERR_PACK_TOO_LRG -7     // Packet is too large for wiznet
#define SOCKERR_RXBUF_TOO_SMALL -8  // RX Buffer is too small

/**
 * @brief Initialize a socket with protocol and port
 * @warning **MUST NOT be called from interrupt context!** Uses blocking operations and taskENTER_CRITICAL().
 * @warning **MUST use tx_buf and rx_buf sizes that are at least 3 bytes larger than the respective WIZNET socket buffer sizes!**
 * @param sn Socket number (0-7)
 * @param protocol TCP (0x01) or UDP (0x02) protocol (use SOCKET_PROTOCOL_TCP or SOCKET_PROTOCOL_UDP)
 * @param port Source port number
 * @param tx_buf Pointer to TX buffer for this socket
 * @param tx_buf_len Size of TX buffer
 * @param rx_buf Pointer to RX buffer for this socket
 * @param rx_buf_len Size of RX buffer
 * @param callback Callback function for socket events (or NULL if not used)
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int socket(uint8_t sn, uint8_t protocol, uint16_t port, uint8_t* tx_buf, uint16_t tx_buf_len, uint8_t* rx_buf, uint16_t rx_buf_len, socket_callback_t callback);

/**
 * @brief Connect to a destination (TCP only - sets destination IP and port, then initiates connection)
 * @warning **MUST NOT be called from interrupt context!** Uses blocking operations and taskENTER_CRITICAL().
 * @param sn Socket number (0-7)
 * @param addr Destination IP address (4 bytes)
 * @param port Destination port number
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int connect(uint8_t sn, uint8_t* addr, uint16_t port);

/**
 * @brief Close a socket
 * @warning **MUST NOT be called from interrupt context!** Uses blocking operations and taskENTER_CRITICAL().
 * @param sn Socket number (0-7)
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int close(uint8_t sn);

/**
 * @brief Disconnect a TCP socket
 * @warning **MUST NOT be called from interrupt context!** Uses blocking operations and taskENTER_CRITICAL().
 * @param sn Socket number (0-7)
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int disconnect(uint8_t sn);

/**
 * @brief Send data on a TCP socket
 * @warning **MUST NOT be called from interrupt context!** Uses taskENTER_CRITICAL() and may block.
 * @param sn Socket number (0-7)
 * @param buf Data buffer to send
 * @param len Length of data to send
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int send(uint8_t sn, uint8_t* buf, uint16_t len);

/**
 * @brief Send data on a UDP socket to a specific destination
 * @warning **MUST NOT be called from interrupt context!** Uses taskENTER_CRITICAL() and may block.
 * @param sn Socket number (0-7)
 * @param buf Data buffer to send
 * @param len Length of data to send
 * @param addr Destination IP address (4 bytes)
 * @param port Destination port number
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int sendto(uint8_t sn, uint8_t* buf, uint16_t len, uint8_t* addr, uint16_t port);

/**
 * @brief Receive data from a TCP socket
 * @warning **MUST NOT be called from interrupt context!** Uses taskENTER_CRITICAL().
 * @param sn Socket number (0-7)
 * @param buf Buffer to store received data
 * @param len Maximum length of data to receive
 * @return Number of bytes received, 0 if no data available
 */
uint32_t recv(uint8_t sn, uint8_t* buf, uint16_t len);

/**
 * @brief Receive data from a UDP socket with source address and port
 * @warning **MUST NOT be called from interrupt context!** Uses taskENTER_CRITICAL().
 * @param sn Socket number (0-7)
 * @param buf Buffer to store received data
 * @param len Maximum length of data to receive
 * @param addr Buffer to store source IP address (4 bytes), can be NULL
 * @param port Buffer to store source port number, can be NULL
 * @return Number of bytes received (data only, excluding header), 0 if no data available
 */
uint32_t recvfrom(uint8_t sn, uint8_t* buf, uint16_t len, uint8_t* addr, uint16_t* port);

/**
 * @brief Get the status of a socket
 * @param sn Socket number (0-7)
 * @return Socket status
 */
socket_status_t getSocketStatus(uint8_t sn);

#ifdef __cplusplus
}
#endif

#endif // SOCKET_H