//*****************************************************************************
//
//! \file ntp_client.h
//! \brief NTP Client Header File for W5500
//! \version 1.0.0
//! \date 2024/10/18
//! \author Assistant
//! \details Simple NTP client implementation for time synchronization using W5500
//
//*****************************************************************************

#ifndef _NTP_CLIENT_H_
#define _NTP_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// NTP Constants
#define NTP_PORT                    123         ///< Standard NTP port
#define NTP_PACKET_SIZE             48          ///< NTP packet size in bytes
#define NTP_TIMEOUT_MS              5000        ///< Default timeout in milliseconds
#define NTP_MAX_RETRIES             3           ///< Maximum number of retries
#define NTP_EPOCH_OFFSET            2208988800UL ///< Seconds between 1900 and 1970

// NTP Leap Indicator values
#define NTP_LEAP_NO_WARNING         0           ///< No warning
#define NTP_LEAP_LAST_MINUTE_61    1           ///< Last minute has 61 seconds
#define NTP_LEAP_LAST_MINUTE_59    2           ///< Last minute has 59 seconds
#define NTP_LEAP_ALARM_CONDITION   3           ///< Alarm condition (clock not synchronized)

// NTP Version values
#define NTP_VERSION_4               4           ///< NTP version 4

// NTP Mode values
#define NTP_MODE_RESERVED           0           ///< Reserved
#define NTP_MODE_SYMMETRIC_ACTIVE   1           ///< Symmetric active
#define NTP_MODE_SYMMETRIC_PASSIVE  2           ///< Symmetric passive
#define NTP_MODE_CLIENT             3           ///< Client
#define NTP_MODE_SERVER             4           ///< Server
#define NTP_MODE_BROADCAST          5           ///< Broadcast
#define NTP_MODE_CONTROL_MESSAGE    6           ///< Control message
#define NTP_MODE_PRIVATE            7           ///< Private use

// NTP Stratum values
#define NTP_STRATUM_UNSPECIFIED     0           ///< Unspecified or invalid
#define NTP_STRATUM_PRIMARY         1           ///< Primary reference (e.g., GPS)
#define NTP_STRATUM_SECONDARY_MIN   2           ///< Secondary reference (min)
#define NTP_STRATUM_SECONDARY_MAX   15          ///< Secondary reference (max)
#define NTP_STRATUM_UNSYNCHRONIZED  16          ///< Unsynchronized

// NTP Error Codes
#define NTP_OK                      0           ///< Success
#define NTP_ERROR_INVALID_PARAM     -1         ///< Invalid parameter
#define NTP_ERROR_SOCKET_FAIL       -2         ///< Socket operation failed
#define NTP_ERROR_TIMEOUT           -3         ///< Operation timed out
#define NTP_ERROR_INVALID_RESPONSE  -4         ///< Invalid NTP response
#define NTP_ERROR_SERVER_ERROR      -5         ///< NTP server error
#define NTP_ERROR_NETWORK           -6         ///< Network error
#define NTP_ERROR_UNSYNCHRONIZED   -7         ///< Server not synchronized

/**
 * @brief NTP Packet Structure
 * @details Standard NTP packet format as per RFC 5905
 */
typedef struct {
    uint8_t leap_version_mode;      ///< Leap indicator (2 bits), version (3 bits), mode (3 bits)
    uint8_t stratum;               ///< Stratum level
    uint8_t poll;                  ///< Poll interval
    uint8_t precision;             ///< Clock precision
    uint32_t root_delay;           ///< Root delay (network path delay)
    uint32_t root_dispersion;     ///< Root dispersion (clock accuracy)
    uint32_t reference_id;         ///< Reference identifier
    uint32_t reference_timestamp_s; ///< Reference timestamp (seconds)
    uint32_t reference_timestamp_f; ///< Reference timestamp (fractional)
    uint32_t origin_timestamp_s;   ///< Origin timestamp (seconds)
    uint32_t origin_timestamp_f;   ///< Origin timestamp (fractional)
    uint32_t receive_timestamp_s;  ///< Receive timestamp (seconds)
    uint32_t receive_timestamp_f;  ///< Receive timestamp (fractional)
    uint32_t transmit_timestamp_s; ///< Transmit timestamp (seconds)
    uint32_t transmit_timestamp_f; ///< Transmit timestamp (fractional)
} ntp_packet_t;

/**
 * @brief NTP Time Structure
 * @details Represents time in seconds and fractional seconds since NTP epoch (1900)
 */
typedef struct {
    uint32_t seconds;              ///< Seconds since NTP epoch (1900)
    uint32_t fractional;           ///< Fractional seconds (2^32 units = 1 second)
} ntp_time_t;

/**
 * @brief NTP Client Configuration Structure
 * @details Configuration parameters for NTP client
 */
typedef struct {
    uint8_t ntp_server[4];         ///< NTP server IP address
    uint16_t timeout_ms;            ///< Timeout in milliseconds
    uint8_t max_retries;            ///< Maximum number of retries
    uint8_t socket_num;             ///< Socket number to use for NTP queries
    uint8_t version;                ///< NTP version (default: 4)
    uint8_t* tx_buf;                ///< Transmit buffer for socket
    uint16_t tx_buf_len;            ///< Transmit buffer length
    uint8_t* rx_buf;                ///< Receive buffer for socket
    uint16_t rx_buf_len;            ///< Receive buffer length
} ntp_config_t;

/**
 * @brief NTP Response Structure
 * @details Contains parsed NTP response data
 */
typedef struct {
    ntp_time_t transmit_time;       ///< Server transmit time
    ntp_time_t receive_time;        ///< Server receive time
    ntp_time_t origin_time;         ///< Client origin time
    uint8_t stratum;                ///< Server stratum level
    uint8_t leap_indicator;         ///< Leap indicator
    uint8_t version;                ///< NTP version
    uint8_t mode;                   ///< NTP mode
    bool is_valid;                  ///< True if response is valid
} ntp_response_t;

/**
 * @brief Initialize NTP client
 * @param config Pointer to NTP configuration structure
 * @return NTP_OK on success, error code on failure
 */
int8_t ntp_init(ntp_config_t* config);

/**
 * @brief Get current time from NTP server
 * @param timestamp Pointer to store Unix timestamp (seconds since 1970)
 * @return NTP_OK on success, error code on failure
 */
int8_t ntp_get_time(uint32_t* timestamp);

/**
 * @brief Get detailed NTP response
 * @param response Pointer to store NTP response data
 * @return NTP_OK on success, error code on failure
 */
int8_t ntp_get_response(ntp_response_t* response);

/**
 * @brief Set NTP server address
 * @param ntp_server NTP server IP address (4 bytes)
 * @return NTP_OK on success, error code on failure
 */
int8_t ntp_set_server(const uint8_t* ntp_server);

/**
 * @brief Get current NTP server address
 * @param ntp_server Buffer to store NTP server IP address (4 bytes)
 * @return NTP_OK on success, error code on failure
 */
int8_t ntp_get_server(uint8_t* ntp_server);

/**
 * @brief Set NTP timeout
 * @param timeout_ms Timeout in milliseconds
 * @return NTP_OK on success, error code on failure
 */
int8_t ntp_set_timeout(uint16_t timeout_ms);

/**
 * @brief Get NTP timeout
 * @return Current timeout in milliseconds
 */
uint16_t ntp_get_timeout(void);

/**
 * @brief Set maximum number of retries
 * @param max_retries Maximum number of retries
 * @return NTP_OK on success, error code on failure
 */
int8_t ntp_set_max_retries(uint8_t max_retries);

/**
 * @brief Get maximum number of retries
 * @return Current maximum number of retries
 */
uint8_t ntp_get_max_retries(void);

/**
 * @brief Close NTP client and free resources
 * @return NTP_OK on success, error code on failure
 */
int8_t ntp_close(void);

/**
 * @brief Get string representation of NTP error code
 * @param error_code NTP error code
 * @return String description of error
 */
const char* ntp_get_error_string(int8_t error_code);

/**
 * @brief Convert NTP time to Unix timestamp
 * @param ntp_time NTP time structure
 * @return Unix timestamp (seconds since 1970)
 */
uint32_t ntp_to_unix_timestamp(const ntp_time_t* ntp_time);

/**
 * @brief Convert Unix timestamp to NTP time
 * @param unix_timestamp Unix timestamp (seconds since 1970)
 * @param ntp_time Pointer to store NTP time
 */
void ntp_from_unix_timestamp(uint32_t unix_timestamp, ntp_time_t* ntp_time);

#ifdef __cplusplus
}
#endif

#endif // _NTP_CLIENT_H_
