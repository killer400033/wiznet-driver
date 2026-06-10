//*****************************************************************************
//
//! \file udp_client.h
//! \brief Simple UDP Client Header File for W5500
//! \version 1.0.0
//! \date 2024/11/28
//! \details Simple UDP client implementation for sending binary packets using W5500
//
//*****************************************************************************

#ifndef _UDP_CLIENT_H_
#define _UDP_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// UDP Client Error Codes
#define UDP_OK                      0           ///< Success
#define UDP_ERROR_INVALID_PARAM     -1         ///< Invalid parameter
#define UDP_ERROR_SOCKET_FAIL       -2         ///< Socket operation failed
#define UDP_ERROR_NOT_INITIALIZED   -3         ///< Client not initialized
#define UDP_ERROR_SEND_FAIL         -4         ///< Send operation failed

/**
 * @brief UDP Client Configuration Structure
 * @details Configuration parameters for UDP client
 */
typedef struct {
    uint8_t socket_num;             ///< Socket number to use (0-7)
    uint16_t source_port;           ///< Source port for UDP socket
    uint8_t dest_ip[4];             ///< Destination IP address
    uint16_t dest_port;             ///< Destination port
    uint8_t* tx_buf;                ///< Transmit buffer for socket
    uint16_t tx_buf_len;            ///< Transmit buffer length
    uint8_t* rx_buf;                ///< Receive buffer for socket
    uint16_t rx_buf_len;            ///< Receive buffer length
} udp_client_config_t;

/**
 * @brief Initialize UDP client
 * @param config Pointer to UDP client configuration structure
 * @return UDP_OK on success, error code on failure
 */
int8_t udp_client_init(udp_client_config_t* config);

/**
 * @brief Send binary packet via UDP
 * @param data Pointer to binary data to send
 * @param len Length of data to send
 * @return UDP_OK on success, error code on failure
 */
int8_t udp_client_send(uint8_t* data, uint16_t len);

/**
 * @brief Set destination IP address
 * @param ip Pointer to IP address
 * @return UDP_OK on success, error code on failure
 */
int8_t udp_client_set_dest_ip(const uint8_t* ip);

/**
 * @brief Close UDP client and free resources
 * @return UDP_OK on success, error code on failure
 */
int8_t udp_client_close(void);

/**
 * @brief Get string representation of UDP error code
 * @param error_code UDP error code
 * @return String description of error
 */
const char* udp_client_get_error_string(int8_t error_code);

#ifdef __cplusplus
}
#endif

#endif // _UDP_CLIENT_H_

