//*****************************************************************************
//
//! \file ntp_client.c
//! \brief NTP Client Implementation for W5500
//! \version 1.0.0
//! \date 2024/10/18
//! \author Assistant
//! \details Simple NTP client implementation for time synchronization using W5500
//
//*****************************************************************************

#include "ntp_client.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "socket.h"

// Internal helper functions (not for public use)
static int8_t _ntp_build_request(ntp_packet_t* packet);
static int8_t _ntp_parse_response(const uint8_t* buffer, uint16_t length, ntp_response_t* response);
static int8_t _ntp_send_request(void);
static int8_t _ntp_receive_response(ntp_response_t* response);
static uint32_t _ntp_get_current_time(void);
static void _ntp_swap_endian_32(uint32_t* value);
static void _ntp_swap_endian_ntp_time(ntp_time_t* ntp_time);

// External HAL functions (assuming STM32 HAL)
extern uint32_t HAL_GetTick(void);

// FreeRTOS functions
extern void osDelay(uint32_t ticks);

// Global NTP configuration
static ntp_config_t g_ntp_config = {0};
static bool g_ntp_initialized = false;
static uint16_t g_source_port = 0;

// Static buffers to avoid stack usage (each 48 bytes)
static ntp_packet_t ntp_request_packet;
static uint8_t ntp_response_buffer[NTP_PACKET_SIZE];

// Error strings for debugging
static const char* ntp_error_strings[] = {
    "Success",                      // NTP_OK
    "Invalid parameter",            // NTP_ERROR_INVALID_PARAM
    "Socket operation failed",      // NTP_ERROR_SOCKET_FAIL
    "Operation timed out",          // NTP_ERROR_TIMEOUT
    "Invalid NTP response",         // NTP_ERROR_INVALID_RESPONSE
    "NTP server error",             // NTP_ERROR_SERVER_ERROR
    "Network error",                // NTP_ERROR_NETWORK
    "Server not synchronized"       // NTP_ERROR_UNSYNCHRONIZED
};

/**
 * @brief Initialize NTP client
 */
int8_t ntp_init(ntp_config_t* config)
{
    if (config == NULL) {
        return NTP_ERROR_INVALID_PARAM;
    }
    
    // Copy configuration
    memcpy(&g_ntp_config, config, sizeof(ntp_config_t));
    
    // Validate socket number
    if (g_ntp_config.socket_num >= _WIZCHIP_SOCK_NUM_) {
        return NTP_ERROR_INVALID_PARAM;
    }
    
    // Set default values if not specified
    if (g_ntp_config.timeout_ms == 0) {
        g_ntp_config.timeout_ms = NTP_TIMEOUT_MS;
    }
    if (g_ntp_config.max_retries == 0) {
        g_ntp_config.max_retries = NTP_MAX_RETRIES;
    }
    if (g_ntp_config.version == 0) {
        g_ntp_config.version = NTP_VERSION_4;
    }
    
    // Generate random source port (1024-50000)
    g_source_port = 40000 + (HAL_GetTick() % 10000); // Random port between 40000-49999
    
    // Initialize socket for UDP with provided buffers
    int8_t result = socket(g_ntp_config.socket_num, SOCKET_PROTOCOL_UDP, g_source_port,
                          g_ntp_config.tx_buf, g_ntp_config.tx_buf_len,
                          g_ntp_config.rx_buf, g_ntp_config.rx_buf_len, NULL);
    if (result != SOCK_OK) {
        return NTP_ERROR_SOCKET_FAIL;
    }
    
    g_ntp_initialized = true;
    return NTP_OK;
}

/**
 * @brief Swap endianness of 32-bit value
 */
static void _ntp_swap_endian_32(uint32_t* value)
{
    uint32_t temp = *value;
    *value = ((temp & 0xFF000000) >> 24) |
             ((temp & 0x00FF0000) >> 8)  |
             ((temp & 0x0000FF00) << 8)  |
             ((temp & 0x000000FF) << 24);
}

/**
 * @brief Swap endianness of NTP time structure
 */
static void _ntp_swap_endian_ntp_time(ntp_time_t* ntp_time)
{
    _ntp_swap_endian_32(&ntp_time->seconds);
    _ntp_swap_endian_32(&ntp_time->fractional);
}

/**
 * @brief Get current time in NTP format (simplified)
 */
static uint32_t _ntp_get_current_time(void)
{
    // This is a simplified implementation
    // In a real application, you might want to use a more accurate time source
    return HAL_GetTick() / 1000; // Convert milliseconds to seconds
}

/**
 * @brief Build NTP request packet
 */
static int8_t _ntp_build_request(ntp_packet_t* packet)
{
    if (packet == NULL) {
        return NTP_ERROR_INVALID_PARAM;
    }
    
    memset(packet, 0, sizeof(ntp_packet_t));
    
    // Set leap indicator (0), version (4), and mode (3 = client)
    packet->leap_version_mode = (0 << 6) | (g_ntp_config.version << 3) | NTP_MODE_CLIENT;
    
    // Set stratum to 0 (unspecified)
    packet->stratum = NTP_STRATUM_UNSPECIFIED;
    
    // Set poll interval (4 = 16 seconds)
    packet->poll = 4;
    
    // Set precision (-6 = 15.6 ms)
    packet->precision = 0xFA;
    
    // Set root delay and dispersion to 0
    packet->root_delay = 0;
    packet->root_dispersion = 0;
    
    // Set reference identifier to 0
    packet->reference_id = 0;
    
    // Set reference timestamp to 0
    packet->reference_timestamp_s = 0;
    packet->reference_timestamp_f = 0;
    
    // Set origin timestamp to 0 (will be filled by server)
    packet->origin_timestamp_s = 0;
    packet->origin_timestamp_f = 0;
    
    // Set receive timestamp to 0 (will be filled by server)
    packet->receive_timestamp_s = 0;
    packet->receive_timestamp_f = 0;
    
    // Set transmit timestamp to current time
    uint32_t current_time = _ntp_get_current_time();
    packet->transmit_timestamp_s = current_time + NTP_EPOCH_OFFSET;
    packet->transmit_timestamp_f = 0;
    
    // Convert to network byte order
    _ntp_swap_endian_32(&packet->root_delay);
    _ntp_swap_endian_32(&packet->root_dispersion);
    _ntp_swap_endian_32(&packet->reference_id);
    _ntp_swap_endian_32(&packet->reference_timestamp_s);
    _ntp_swap_endian_32(&packet->reference_timestamp_f);
    _ntp_swap_endian_32(&packet->origin_timestamp_s);
    _ntp_swap_endian_32(&packet->origin_timestamp_f);
    _ntp_swap_endian_32(&packet->receive_timestamp_s);
    _ntp_swap_endian_32(&packet->receive_timestamp_f);
    _ntp_swap_endian_32(&packet->transmit_timestamp_s);
    _ntp_swap_endian_32(&packet->transmit_timestamp_f);
    
    return NTP_OK;
}

/**
 * @brief Parse NTP response packet
 */
static int8_t _ntp_parse_response(const uint8_t* buffer, uint16_t length, ntp_response_t* response)
{
    if (buffer == NULL || response == NULL || length < NTP_PACKET_SIZE) {
        return NTP_ERROR_INVALID_PARAM;
    }
    
    memset(response, 0, sizeof(ntp_response_t));
    
    // Parse header fields
    uint8_t leap_version_mode = buffer[0];
    response->leap_indicator = (leap_version_mode >> 6) & 0x03;
    response->version = (leap_version_mode >> 3) & 0x07;
    response->mode = leap_version_mode & 0x07;
    response->stratum = buffer[1];
    
    // Check if response is valid
    if (response->mode != NTP_MODE_SERVER) {
        return NTP_ERROR_INVALID_RESPONSE;
    }
    
    if (response->stratum == NTP_STRATUM_UNSPECIFIED || 
        response->stratum == NTP_STRATUM_UNSYNCHRONIZED) {
        return NTP_ERROR_UNSYNCHRONIZED;
    }
    
    // Parse timestamps (network byte order)
    response->origin_time.seconds = *(uint32_t*)&buffer[24];
    response->origin_time.fractional = *(uint32_t*)&buffer[28];
    response->receive_time.seconds = *(uint32_t*)&buffer[32];
    response->receive_time.fractional = *(uint32_t*)&buffer[36];
    response->transmit_time.seconds = *(uint32_t*)&buffer[40];
    response->transmit_time.fractional = *(uint32_t*)&buffer[44];
    
    // Convert from network byte order
    _ntp_swap_endian_ntp_time(&response->origin_time);
    _ntp_swap_endian_ntp_time(&response->receive_time);
    _ntp_swap_endian_ntp_time(&response->transmit_time);
    
    response->is_valid = true;
    return NTP_OK;
}

/**
 * @brief Send NTP request
 */
static int8_t _ntp_send_request(void)
{
    int8_t result = _ntp_build_request(&ntp_request_packet);
    if (result != NTP_OK) {
        return result;
    }
    
    // Send request using new API
    int32_t sent = sendto(g_ntp_config.socket_num, (uint8_t*)&ntp_request_packet, 
                         NTP_PACKET_SIZE, g_ntp_config.ntp_server, NTP_PORT);
    if (sent != SOCK_OK) {
        return NTP_ERROR_NETWORK;
    }
    
    return NTP_OK;
}

/**
 * @brief Receive NTP response
 */
static int8_t _ntp_receive_response(ntp_response_t* response)
{
    uint8_t peer_ip[4];
    uint16_t peer_port;
    
    // Wait for response with timeout
    uint32_t start_time = HAL_GetTick();
    
    while ((HAL_GetTick() - start_time) < g_ntp_config.timeout_ms) {
        // Try to receive data using new API
        int32_t recv_len = recvfrom(g_ntp_config.socket_num, ntp_response_buffer, 
                                   NTP_PACKET_SIZE, peer_ip, &peer_port);
        
        if (recv_len > 0 && peer_port == NTP_PORT && memcmp(peer_ip, g_ntp_config.ntp_server, 4) == 0) {
            // Check if response came from the expected server and port
        		int8_t result = _ntp_parse_response(ntp_response_buffer, NTP_PACKET_SIZE, response);
        		if (result == NTP_OK) {
        				return NTP_OK;
        		}
        }
        
        osDelay(100); // Small delay to prevent busy waiting
    }
    
    return NTP_ERROR_TIMEOUT;
}

/**
 * @brief Get current time from NTP server
 */
int8_t ntp_get_time(uint32_t* timestamp)
{
    if (!g_ntp_initialized) {
        return NTP_ERROR_SOCKET_FAIL;
    }
    
    if (timestamp == NULL) {
        return NTP_ERROR_INVALID_PARAM;
    }

    // Check if socket is open
    if (getSocketStatus(g_ntp_config.socket_num) != SOCKET_UDP) {
        int result = socket(g_ntp_config.socket_num, SOCKET_PROTOCOL_UDP, g_source_port,
            g_ntp_config.tx_buf, g_ntp_config.tx_buf_len,
            g_ntp_config.rx_buf, g_ntp_config.rx_buf_len, NULL);
        
        if (result != SOCK_OK) {
            return NTP_ERROR_SOCKET_FAIL;
        }
    }
    
    ntp_response_t response;
    int8_t result = ntp_get_response(&response);
    
    if (result != NTP_OK) {
        return result;
    }
    
    // Convert NTP time to Unix timestamp
    *timestamp = ntp_to_unix_timestamp(&response.transmit_time);
    
    return NTP_OK;
}

/**
 * @brief Get detailed NTP response
 */
int8_t ntp_get_response(ntp_response_t* response)
{
    if (!g_ntp_initialized) {
        return NTP_ERROR_SOCKET_FAIL;
    }
    
    if (response == NULL) {
        return NTP_ERROR_INVALID_PARAM;
    }

    // Check if socket is open
    if (getSocketStatus(g_ntp_config.socket_num) != SOCKET_UDP) {
        int result = socket(g_ntp_config.socket_num, SOCKET_PROTOCOL_UDP, g_source_port,
            g_ntp_config.tx_buf, g_ntp_config.tx_buf_len,
            g_ntp_config.rx_buf, g_ntp_config.rx_buf_len, NULL);
        
        if (result != SOCK_OK) {
            return NTP_ERROR_SOCKET_FAIL;
        }
    }
    
    // Send request with retries
    for (uint8_t retry = 0; retry <= g_ntp_config.max_retries; retry++) {
        // Send request
        int8_t result = _ntp_send_request();
        if (result != NTP_OK) {
            if (retry == g_ntp_config.max_retries) {
                return result;
            }
            continue;
        }
        
        // Receive response
        result = _ntp_receive_response(response);
        if (result == NTP_OK) {
            return NTP_OK;
        }
        
        if (retry < g_ntp_config.max_retries) {
            osDelay(100); // Wait before retry
        }
    }
    
    return NTP_ERROR_TIMEOUT;
}

/**
 * @brief Set NTP server address
 */
int8_t ntp_set_server(const uint8_t* ntp_server)
{
    if (ntp_server == NULL) {
        return NTP_ERROR_INVALID_PARAM;
    }
    
    memcpy(g_ntp_config.ntp_server, ntp_server, 4);
    return NTP_OK;
}

/**
 * @brief Get current NTP server address
 */
int8_t ntp_get_server(uint8_t* ntp_server)
{
    if (ntp_server == NULL) {
        return NTP_ERROR_INVALID_PARAM;
    }
    
    memcpy(ntp_server, g_ntp_config.ntp_server, 4);
    return NTP_OK;
}

/**
 * @brief Set NTP timeout
 */
int8_t ntp_set_timeout(uint16_t timeout_ms)
{
    if (timeout_ms == 0) {
        return NTP_ERROR_INVALID_PARAM;
    }
    
    g_ntp_config.timeout_ms = timeout_ms;
    return NTP_OK;
}

/**
 * @brief Get NTP timeout
 */
uint16_t ntp_get_timeout(void)
{
    return g_ntp_config.timeout_ms;
}

/**
 * @brief Set maximum number of retries
 */
int8_t ntp_set_max_retries(uint8_t max_retries)
{
    g_ntp_config.max_retries = max_retries;
    return NTP_OK;
}

/**
 * @brief Get maximum number of retries
 */
uint8_t ntp_get_max_retries(void)
{
    return g_ntp_config.max_retries;
}

/**
 * @brief Close NTP client and free resources
 */
int8_t ntp_close(void)
{
    if (close(g_ntp_config.socket_num) != SOCK_OK) {
    	return NTP_ERROR_SOCKET_FAIL;
    }

    return NTP_OK;
}

/**
 * @brief Get string representation of NTP error code
 */
const char* ntp_get_error_string(int8_t error_code)
{
    int index = -error_code;
    
    if (index >= 0 && index < (int)(sizeof(ntp_error_strings) / sizeof(ntp_error_strings[0]))) {
        return ntp_error_strings[index];
    }
    
    return "Unknown error";
}

/**
 * @brief Convert NTP time to Unix timestamp
 */
uint32_t ntp_to_unix_timestamp(const ntp_time_t* ntp_time)
{
    if (ntp_time == NULL) {
        return 0;
    }
    
    // NTP epoch is 1900, Unix epoch is 1970
    // Subtract the offset to convert to Unix timestamp
    if (ntp_time->seconds >= NTP_EPOCH_OFFSET) {
        return ntp_time->seconds - NTP_EPOCH_OFFSET;
    }
    
    return 0;
}

/**
 * @brief Convert Unix timestamp to NTP time
 */
void ntp_from_unix_timestamp(uint32_t unix_timestamp, ntp_time_t* ntp_time)
{
    if (ntp_time == NULL) {
        return;
    }
    
    ntp_time->seconds = unix_timestamp + NTP_EPOCH_OFFSET;
    ntp_time->fractional = 0;
}
