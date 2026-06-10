//*****************************************************************************
//
//! \file udp_client.c
//! \brief Simple UDP Client Implementation for W5500
//! \version 1.0.0
//! \date 2024/11/28
//! \details Simple UDP client implementation for sending binary packets using W5500
//
//*****************************************************************************

#include "udp_client.h"
#include "socket.h"
#include <string.h>

// Global UDP client configuration
static udp_client_config_t g_udp_config = {0};
static bool g_udp_initialized = false;

// Error strings for debugging
static const char* udp_error_strings[] = {
    "Success",                      // UDP_OK
    "Invalid parameter",            // UDP_ERROR_INVALID_PARAM
    "Socket operation failed",      // UDP_ERROR_SOCKET_FAIL
    "Client not initialized",       // UDP_ERROR_NOT_INITIALIZED
    "Send operation failed"         // UDP_ERROR_SEND_FAIL
};

/**
 * @brief Initialize UDP client
 */
int8_t udp_client_init(udp_client_config_t* config)
{
    if (config == NULL) {
        return UDP_ERROR_INVALID_PARAM;
    }
    
    // Validate socket number
    if (config->socket_num >= _WIZCHIP_SOCK_NUM_) {
        return UDP_ERROR_INVALID_PARAM;
    }
    
    // Validate buffers
    if (config->tx_buf == NULL || config->rx_buf == NULL ||
        config->tx_buf_len == 0 || config->rx_buf_len == 0) {
        return UDP_ERROR_INVALID_PARAM;
    }
    
    // Copy configuration
    memcpy(&g_udp_config, config, sizeof(udp_client_config_t));
    
    // Initialize socket for UDP
    int result = socket(g_udp_config.socket_num, 
                       SOCKET_PROTOCOL_UDP, 
                       g_udp_config.source_port,
                       g_udp_config.tx_buf, 
                       g_udp_config.tx_buf_len,
                       g_udp_config.rx_buf, 
                       g_udp_config.rx_buf_len, 
                       NULL);
    
    if (result != SOCK_OK) {
        return UDP_ERROR_SOCKET_FAIL;
    }
    
    g_udp_initialized = true;
    return UDP_OK;
}

/**
 * @brief Send binary packet via UDP
 */
int8_t udp_client_send(uint8_t* data, uint16_t len)
{
    if (!g_udp_initialized) {
        return UDP_ERROR_NOT_INITIALIZED;
    }
    
    if (data == NULL || len == 0) {
        return UDP_ERROR_INVALID_PARAM;
    }

    if (getSocketStatus(g_udp_config.socket_num) != SOCKET_UDP) {
        int result = socket(g_udp_config.socket_num, SOCKET_PROTOCOL_UDP, g_udp_config.source_port,
                       g_udp_config.tx_buf, g_udp_config.tx_buf_len,
                       g_udp_config.rx_buf, g_udp_config.rx_buf_len, NULL);
        
        if (result != SOCK_OK) {
            return UDP_ERROR_SOCKET_FAIL;
        }
    }
    
    // Send data using sendto
    int result = sendto(g_udp_config.socket_num, 
                       data, 
                       len, 
                       g_udp_config.dest_ip, 
                       g_udp_config.dest_port);
    
    if (result != SOCK_OK) {
        return UDP_ERROR_SEND_FAIL;
    }
    
    return UDP_OK;
}

/**
 * @brief Set destination IP address
 */
int8_t udp_client_set_dest_ip(const uint8_t* ip)
{
    memcpy(g_udp_config.dest_ip, ip, 4);
    return UDP_OK;
}

/**
 * @brief Close UDP client and free resources
 */
int8_t udp_client_close(void)
{
    if (!g_udp_initialized) {
        return UDP_ERROR_NOT_INITIALIZED;
    }
    
    if (close(g_udp_config.socket_num) != SOCK_OK) {
        return UDP_ERROR_SOCKET_FAIL;
    }
    
    g_udp_initialized = false;
    return UDP_OK;
}

/**
 * @brief Get string representation of UDP error code
 */
const char* udp_client_get_error_string(int8_t error_code)
{
    int index = -error_code;
    
    if (index >= 0 && index < (int)(sizeof(udp_error_strings) / sizeof(udp_error_strings[0]))) {
        return udp_error_strings[index];
    }
    
    return "Unknown error";
}

