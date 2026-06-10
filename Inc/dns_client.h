//*****************************************************************************
//
//! \file dns_client.h
//! \brief DNS Client Header File for W5500
//! \version 1.0.0
//! \date 2024/10/18
//! \author Assistant
//! \details DNS client implementation for domain name resolution using W5500
//
//*****************************************************************************

#ifndef _DNS_CLIENT_H_
#define _DNS_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// DNS Constants
#define DNS_PORT                    53          ///< Standard DNS port
#define DNS_MAX_NAME_LENGTH         253         ///< Maximum domain name length
#define DNS_MAX_LABEL_LENGTH        63          ///< Maximum label length in domain name
#define DNS_MAX_PACKET_SIZE         512         ///< Maximum DNS packet size
#define DNS_HEADER_SIZE             12          ///< DNS header size in bytes
#define DNS_DEFAULT_TIMEOUT         5000        ///< Default timeout in milliseconds
#define DNS_MAX_RETRIES             3           ///< Maximum number of retries

// DNS Message Types
#define DNS_TYPE_A                  1           ///< IPv4 address record
#define DNS_TYPE_NS                 2           ///< Name server record
#define DNS_TYPE_CNAME              5           ///< Canonical name record
#define DNS_TYPE_SOA                6           ///< Start of authority record
#define DNS_TYPE_PTR                12          ///< Pointer record
#define DNS_TYPE_MX                 15          ///< Mail exchange record
#define DNS_TYPE_TXT                16          ///< Text record
#define DNS_TYPE_AAAA               28          ///< IPv6 address record

// DNS Classes
#define DNS_CLASS_IN                1           ///< Internet class

// DNS Response Codes
#define DNS_RCODE_NO_ERROR          0           ///< No error
#define DNS_RCODE_FORMAT_ERROR      1           ///< Format error
#define DNS_RCODE_SERVER_FAILURE    2           ///< Server failure
#define DNS_RCODE_NAME_ERROR        3           ///< Name error (NXDOMAIN)
#define DNS_RCODE_NOT_IMPLEMENTED   4           ///< Not implemented
#define DNS_RCODE_REFUSED           5           ///< Refused

// DNS Error Codes
#define DNS_OK                      0           ///< Success
#define DNS_ERROR_INVALID_PARAM     -1          ///< Invalid parameter
#define DNS_ERROR_SOCKET_FAIL       -2          ///< Socket operation failed
#define DNS_ERROR_TIMEOUT           -3          ///< Operation timed out
#define DNS_ERROR_FORMAT            -4          ///< Invalid DNS format
#define DNS_ERROR_SERVER_ERROR      -5          ///< DNS server error
#define DNS_ERROR_NAME_NOT_FOUND    -6          ///< Domain name not found
#define DNS_ERROR_BUFFER_TOO_SMALL  -7          ///< Buffer too small
#define DNS_ERROR_NETWORK           -8          ///< Network error

/**
 * @brief DNS Header Structure
 * @details Standard DNS message header as per RFC 1035
 */
typedef struct {
    uint16_t id;            ///< Transaction ID
    uint16_t flags;         ///< Flags (QR, Opcode, AA, TC, RD, RA, Z, RCODE)
    uint16_t qdcount;       ///< Number of questions
    uint16_t ancount;       ///< Number of answer RRs
    uint16_t nscount;       ///< Number of authority RRs
    uint16_t arcount;       ///< Number of additional RRs
} dns_header_t;

/**
 * @brief DNS Question Structure
 * @details Question section of DNS message
 */
typedef struct {
    char name[DNS_MAX_NAME_LENGTH + 1];     ///< Domain name (null-terminated)
    uint16_t qtype;                         ///< Question type
    uint16_t qclass;                        ///< Question class
} dns_question_t;

/**
 * @brief Minimal DNS A Record Structure
 * @details Optimized for A record responses only
 */
typedef struct {
    uint16_t type;                          ///< Resource record type (should be DNS_TYPE_A)
    uint32_t ttl;                           ///< Time to live
    uint8_t ip_addr[4];                     ///< IPv4 address (4 bytes)
} dns_a_record_t;

/**
 * @brief Minimal DNS CNAME Record Structure
 * @details Stores the first encountered CNAME target
 */
typedef struct {
    uint16_t type;                          ///< Resource record type (should be DNS_TYPE_CNAME)
    uint32_t ttl;                           ///< Time to live
    char cname[DNS_MAX_NAME_LENGTH + 1];    ///< Canonical name target
} dns_cname_record_t;

/**
 * @brief Minimal DNS Response Structure
 * @details Optimized for single A record lookup
 */
typedef struct {
    dns_header_t header;                    ///< DNS header
    dns_a_record_t answer;                  ///< Single A record answer
    bool has_answer;                        ///< True if answer is valid
    dns_cname_record_t cname;               ///< First CNAME record (if present)
    bool has_cname;                         ///< True if cname is valid
} dns_response_t;

/**
 * @brief DNS Client Configuration Structure
 * @details Configuration parameters for DNS client
 */
typedef struct {
    uint8_t dns_server[4];                  ///< DNS server IP address
    uint16_t timeout_ms;                    ///< Timeout in milliseconds
    uint8_t max_retries;                    ///< Maximum number of retries
    uint8_t socket_num;                     ///< Socket number to use for DNS queries
    uint8_t* tx_buf;                        ///< TX buffer for socket (must be at least DNS_MAX_PACKET_SIZE + 3 bytes)
    uint16_t tx_buf_len;                    ///< TX buffer length
    uint8_t* rx_buf;                        ///< RX buffer for socket (must be at least DNS_MAX_PACKET_SIZE + 3 bytes)
    uint16_t rx_buf_len;                    ///< RX buffer length
} dns_config_t;

/**
 * @brief Initialize DNS client
 * @param config Pointer to DNS configuration structure
 * @return DNS_OK on success, error code on failure
 */
int8_t dns_init(dns_config_t* config);

/**
 * @brief Resolve domain name to IPv4 address
 * @param hostname Domain name to resolve (null-terminated string)
 * @param ip_addr Buffer to store resolved IPv4 address (4 bytes)
 * @param ttl Optional pointer to store TTL of the final A record (can be NULL)
 * @return DNS_OK on success, error code on failure
 */
int8_t dns_resolve_a(const char* hostname, uint8_t* ip_addr, uint32_t* ttl);

/**
 * @brief Set DNS server address
 * @param dns_server DNS server IP address (4 bytes)
 * @return DNS_OK on success, error code on failure
 */
int8_t dns_set_server(const uint8_t* dns_server);

/**
 * @brief Get current DNS server address
 * @param dns_server Buffer to store DNS server IP address (4 bytes)
 * @return DNS_OK on success, error code on failure
 */
int8_t dns_get_server(uint8_t* dns_server);

/**
 * @brief Set DNS timeout
 * @param timeout_ms Timeout in milliseconds
 * @return DNS_OK on success, error code on failure
 */
int8_t dns_set_timeout(uint16_t timeout_ms);

/**
 * @brief Get DNS timeout
 * @return Current timeout in milliseconds
 */
uint16_t dns_get_timeout(void);

/**
 * @brief Set maximum number of retries
 * @param max_retries Maximum number of retries
 * @return DNS_OK on success, error code on failure
 */
int8_t dns_set_max_retries(uint8_t max_retries);

/**
 * @brief Get maximum number of retries
 * @return Current maximum number of retries
 */
uint8_t dns_get_max_retries(void);

/**
 * @brief Close DNS client and free resources
 * @return DNS_OK on success, error code on failure
 */
int8_t dns_close(void);

/**
 * @brief Get string representation of DNS error code
 * @param error_code DNS error code
 * @return String description of error
 */
const char* dns_get_error_string(int8_t error_code);

#ifdef __cplusplus
}
#endif

#endif // _DNS_CLIENT_H_
