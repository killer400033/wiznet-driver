//*****************************************************************************
//
//! \file dns_client.c
//! \brief DNS Client Implementation for W5500
//! \version 1.0.0
//! \date 2024/10/18
//! \author Assistant
//! \details DNS client implementation for domain name resolution using W5500
//
//*****************************************************************************

#include "dns_client.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include "cmsis_os.h"
#include "socket.h"

// Internal helper functions (not for public use)
static int8_t _dns_build_query(const char* hostname, uint16_t qtype, uint8_t* buffer, uint16_t* length);
static int8_t _dns_parse_response(const uint8_t* buffer, uint16_t length, dns_response_t* response);
static int8_t _dns_encode_name(const char* hostname, uint8_t* buffer, uint16_t* length);
static int8_t _dns_decode_name(const uint8_t* buffer, uint16_t buffer_len, uint16_t* offset, char* name, uint16_t name_len);
static uint16_t _dns_generate_id(void);
static bool _dns_is_valid_hostname(const char* hostname);
static int8_t _dns_query(const char* hostname, uint16_t qtype, dns_response_t* response);

// External HAL functions (assuming STM32 HAL)
extern uint32_t HAL_GetTick(void);

// Global DNS configuration
static dns_config_t g_dns_config = {0};

static bool g_dns_initialized = false;
static uint16_t g_transaction_id = 1;
static uint16_t g_source_port = 0;

// Query and response buffers
static uint8_t query_buffer[DNS_MAX_PACKET_SIZE];
static uint8_t response_buffer[DNS_MAX_PACKET_SIZE];

// Large working buffers to avoid stack overflow
static char cname_buf[DNS_MAX_NAME_LENGTH + 1];
static char current_name[DNS_MAX_NAME_LENGTH + 1];
static dns_response_t dns_response;

// Error strings for debugging
static const char* dns_error_strings[] = {
    "Success",                      // DNS_OK
    "Invalid parameter",            // DNS_ERROR_INVALID_PARAM
    "Socket operation failed",      // DNS_ERROR_SOCKET_FAIL
    "Operation timed out",          // DNS_ERROR_TIMEOUT
    "Invalid DNS format",           // DNS_ERROR_FORMAT
    "DNS server error",             // DNS_ERROR_SERVER_ERROR
    "Domain name not found",        // DNS_ERROR_NAME_NOT_FOUND
    "Buffer too small",             // DNS_ERROR_BUFFER_TOO_SMALL
    "Network error"                 // DNS_ERROR_NETWORK
};

/**
 * @brief Initialize DNS client
 */
int8_t dns_init(dns_config_t* config)
{
    if (config == NULL) {
        return DNS_ERROR_INVALID_PARAM;
    }
    
    // Copy configuration
    memcpy(&g_dns_config, config, sizeof(dns_config_t));
    
    // Validate socket number
    if (g_dns_config.socket_num >= _WIZCHIP_SOCK_NUM_) {
        return DNS_ERROR_INVALID_PARAM;
    }
    
    // Initialize socket for UDP with random source port
    g_source_port = 50000 + (HAL_GetTick() % 10000); // Random port between 50000-59999
    int result = socket(g_dns_config.socket_num, SOCKET_PROTOCOL_UDP, g_source_port,
                       g_dns_config.tx_buf, g_dns_config.tx_buf_len,
                       g_dns_config.rx_buf, g_dns_config.rx_buf_len, NULL);
    if (result != SOCK_OK) {
        return DNS_ERROR_SOCKET_FAIL;
    }
    
    g_dns_initialized = true;
    return DNS_OK;
}

/**
 * @brief Generate unique transaction ID
 */
static uint16_t _dns_generate_id(void)
{
    return g_transaction_id++;
}

/**
 * @brief Validate hostname format
 */
static bool _dns_is_valid_hostname(const char* hostname)
{
    if (hostname == NULL || strlen(hostname) == 0 || strlen(hostname) > DNS_MAX_NAME_LENGTH) {
        return false;
    }
    
    size_t len = strlen(hostname);
    size_t label_len = 0;
    
    for (size_t i = 0; i < len; i++) {
        char c = hostname[i];
        
        if (c == '.') {
            if (label_len == 0 || label_len > DNS_MAX_LABEL_LENGTH) {
                return false;
            }
            label_len = 0;
        } else if (isalnum(c) || c == '-') {
            label_len++;
            if (label_len > DNS_MAX_LABEL_LENGTH) {
                return false;
            }
        } else {
            return false;
        }
    }
    
    return label_len > 0 && label_len <= DNS_MAX_LABEL_LENGTH;
}

/**
 * @brief Encode domain name in DNS format
 */
static int8_t _dns_encode_name(const char* hostname, uint8_t* buffer, uint16_t* length)
{
    if (hostname == NULL || buffer == NULL || length == NULL) {
        return DNS_ERROR_INVALID_PARAM;
    }
    
    if (!_dns_is_valid_hostname(hostname)) {
        return DNS_ERROR_INVALID_PARAM;
    }
    
    size_t hostname_len = strlen(hostname);
    uint16_t pos = 0;
    uint16_t label_start = 0;
    
    for (size_t i = 0; i <= hostname_len; i++) {
        if (hostname[i] == '.' || hostname[i] == '\0') {
            uint8_t label_len = i - label_start;
            if (label_len == 0) {
                return DNS_ERROR_FORMAT;
            }
            
            buffer[pos++] = label_len;
            memcpy(&buffer[pos], &hostname[label_start], label_len);
            pos += label_len;
            label_start = i + 1;
        }
    }
    
    buffer[pos++] = 0; // Null terminator
    *length = pos;
    
    return DNS_OK;
}

/**
 * @brief Decode domain name from DNS format
 */
static int8_t _dns_decode_name(const uint8_t* buffer, uint16_t buffer_len, uint16_t* offset, char* name, uint16_t name_len)
{
    if (buffer == NULL || offset == NULL || name == NULL) {
        return DNS_ERROR_INVALID_PARAM;
    }
    
    uint16_t pos = *offset;
    uint16_t name_pos = 0;
    bool jumped = false;
    uint16_t jumps = 0;
    
    while (pos < buffer_len && jumps < 10) { // Limit jumps to prevent infinite loops
        uint8_t len = buffer[pos];
        
        if (len == 0) {
            // End of name
            if (!jumped) {
                *offset = pos + 1;
            }
            break;
        } else if ((len & 0xC0) == 0xC0) {
            // Compression pointer
            if (!jumped) {
                *offset = pos + 2;
            }
            pos = ((len & 0x3F) << 8) | buffer[pos + 1];
            jumped = true;
            jumps++;
        } else {
            // Regular label
            pos++;
            if (name_pos > 0 && name_pos < name_len - 1) {
                name[name_pos++] = '.';
            }
            
            for (uint8_t i = 0; i < len && pos < buffer_len && name_pos < name_len - 1; i++) {
                name[name_pos++] = buffer[pos++];
            }
        }
    }
    
    name[name_pos] = '\0';
    return DNS_OK;
}

/**
 * @brief Build DNS query packet
 */
static int8_t _dns_build_query(const char* hostname, uint16_t qtype, uint8_t* buffer, uint16_t* length)
{
    if (hostname == NULL || buffer == NULL || length == NULL) {
        return DNS_ERROR_INVALID_PARAM;
    }
    
    uint16_t pos = 0;
    
    // DNS Header
    dns_header_t header = {0};
    header.id = _dns_generate_id();
    header.flags = 0x0100; // Standard query with recursion desired
    header.qdcount = 1;
    header.ancount = 0;
    header.nscount = 0;
    header.arcount = 0;
    
    // Convert to network byte order and copy to buffer
    buffer[pos++] = (header.id >> 8) & 0xFF;
    buffer[pos++] = header.id & 0xFF;
    buffer[pos++] = (header.flags >> 8) & 0xFF;
    buffer[pos++] = header.flags & 0xFF;
    buffer[pos++] = (header.qdcount >> 8) & 0xFF;
    buffer[pos++] = header.qdcount & 0xFF;
    buffer[pos++] = (header.ancount >> 8) & 0xFF;
    buffer[pos++] = header.ancount & 0xFF;
    buffer[pos++] = (header.nscount >> 8) & 0xFF;
    buffer[pos++] = header.nscount & 0xFF;
    buffer[pos++] = (header.arcount >> 8) & 0xFF;
    buffer[pos++] = header.arcount & 0xFF;
    
    // Question section
    uint16_t name_len;
    int8_t result = _dns_encode_name(hostname, &buffer[pos], &name_len);
    if (result != DNS_OK) {
        return result;
    }
    pos += name_len;
    
    // QTYPE
    buffer[pos++] = (qtype >> 8) & 0xFF;
    buffer[pos++] = qtype & 0xFF;
    
    // QCLASS (IN = 1)
    buffer[pos++] = 0x00;
    buffer[pos++] = 0x01;
    
    *length = pos;
    return DNS_OK;
}

/**
 * @brief Skip DNS name in packet (optimized helper)
 */
static int8_t _dns_skip_name(const uint8_t* buffer, uint16_t length, uint16_t* pos)
{
    if (buffer == NULL || pos == NULL || *pos >= length) {
        return DNS_ERROR_INVALID_PARAM;
    }
    
    while (*pos < length) {
        uint8_t len = buffer[*pos];
        
        // Check for compression pointer
        if ((len & 0xC0) == 0xC0) {
            // Compression pointer - skip 2 bytes
            *pos += 2;
            return DNS_OK;
        }
        
        // Check for end of name
        if (len == 0) {
            *pos += 1;
            return DNS_OK;
        }
        
        // Skip label length + label data
        *pos += 1 + len;
        
        if (*pos >= length) {
            return DNS_ERROR_FORMAT;
        }
    }
    
    return DNS_ERROR_FORMAT;
}

/**
 * @brief Parse DNS response packet (optimized for A records)
 */
static int8_t _dns_parse_response(const uint8_t* buffer, uint16_t length, dns_response_t* response)
{
    if (buffer == NULL || response == NULL || length < DNS_HEADER_SIZE) {
        return DNS_ERROR_INVALID_PARAM;
    }
    
    memset(response, 0, sizeof(dns_response_t));
    uint16_t pos = 0;
    
    // Parse header
    response->header.id = (buffer[pos] << 8) | buffer[pos + 1];
    pos += 2;
    response->header.flags = (buffer[pos] << 8) | buffer[pos + 1];
    pos += 2;
    response->header.qdcount = (buffer[pos] << 8) | buffer[pos + 1];
    pos += 2;
    response->header.ancount = (buffer[pos] << 8) | buffer[pos + 1];
    pos += 2;
    response->header.nscount = (buffer[pos] << 8) | buffer[pos + 1];
    pos += 2;
    response->header.arcount = (buffer[pos] << 8) | buffer[pos + 1];
    pos += 2;
    
    // Check response code
    uint8_t rcode = response->header.flags & 0x0F;
    if (rcode != DNS_RCODE_NO_ERROR) {
        switch (rcode) {
            case DNS_RCODE_NAME_ERROR:
                return DNS_ERROR_NAME_NOT_FOUND;
            case DNS_RCODE_SERVER_FAILURE:
                return DNS_ERROR_SERVER_ERROR;
            default:
                return DNS_ERROR_SERVER_ERROR;
        }
    }
    
    // Skip question section
    if (response->header.qdcount > 0) {
        // Skip question name
        int8_t result = _dns_skip_name(buffer, length, &pos);
        if (result != DNS_OK) {
            return result;
        }
        
        // Skip qtype and qclass (4 bytes)
        if (pos + 4 > length) {
            return DNS_ERROR_FORMAT;
        }
        pos += 4;
    }
    
    // Parse answer records - look for first A record and first CNAME
    response->has_answer = false;
    response->has_cname = false;
    for (uint16_t i = 0; i < response->header.ancount && pos < length; i++) {
        // Skip name
        int8_t result = _dns_skip_name(buffer, length, &pos);
        if (result != DNS_OK) {
            return result;
        }
        
        if (pos + 10 > length) {
            return DNS_ERROR_FORMAT;
        }
        
        // Parse type, class, TTL, and data length
        uint16_t type = (buffer[pos] << 8) | buffer[pos + 1];
        pos += 2;
        //uint16_t rr_class = (buffer[pos] << 8) | buffer[pos + 1];
        pos += 2;
        uint32_t ttl = (buffer[pos] << 24) | (buffer[pos + 1] << 16) | (buffer[pos + 2] << 8) | buffer[pos + 3];
        pos += 4;
        uint16_t rdlength = (buffer[pos] << 8) | buffer[pos + 1];
        pos += 2;
        
        // Check if we have enough data
        if (pos + rdlength > length) {
            return DNS_ERROR_FORMAT;
        }
        
        // If this is an A record and we haven't found one yet
        if (type == DNS_TYPE_A && rdlength == 4 && !response->has_answer) {
            response->answer.type = type;
            response->answer.ttl = ttl;
            memcpy(response->answer.ip_addr, &buffer[pos], 4);
            response->has_answer = true;
        } else if (type == DNS_TYPE_CNAME && !response->has_cname) {
            // Decode the CNAME target from RDATA (using static buffer to avoid stack overflow)
            uint16_t name_offset = pos;
            int8_t dec_res = _dns_decode_name(buffer, length, &name_offset, cname_buf, sizeof(cname_buf));
            if (dec_res == DNS_OK) {
                response->cname.type = type;
                response->cname.ttl = ttl;
                // Ensure null-terminated copy
                strncpy(response->cname.cname, cname_buf, sizeof(response->cname.cname) - 1);
                response->cname.cname[sizeof(response->cname.cname) - 1] = '\0';
                response->has_cname = true;
            }
        }
        
        // Skip resource data
        pos += rdlength;
    }
    
    return DNS_OK;
}

/**
 * @brief Send DNS query and receive response
 */
static int8_t _dns_query(const char* hostname, uint16_t qtype, dns_response_t* response)
{
    if (!g_dns_initialized) {
        return DNS_ERROR_SOCKET_FAIL;
    }
    
    if (hostname == NULL || response == NULL) {
        return DNS_ERROR_INVALID_PARAM;
    }
    
    uint16_t query_length;
    
    // Build query
    int8_t result = _dns_build_query(hostname, qtype, query_buffer, &query_length);
    if (result != DNS_OK) {
        return result;
    }
    
    // Send query with retries
    for (uint8_t retry = 0; retry <= g_dns_config.max_retries; retry++) {
        // Send query
        int sent_result = sendto(g_dns_config.socket_num, query_buffer, query_length, 
                                g_dns_config.dns_server, DNS_PORT);
        if (sent_result != SOCK_OK) {
            if (retry == g_dns_config.max_retries) {
                return DNS_ERROR_NETWORK;
            }
            continue;
        }
        
        // Wait for response with timeout
        uint32_t start_time = HAL_GetTick();
        
        while ((HAL_GetTick() - start_time) < g_dns_config.timeout_ms) {
            uint8_t peer_ip[4];
            uint16_t peer_port;
            
            // Try to receive response
            uint32_t recv_len = recvfrom(g_dns_config.socket_num, response_buffer, 
                                        sizeof(response_buffer), peer_ip, &peer_port);
            
            if (recv_len > 0 && peer_port == DNS_PORT && memcmp(peer_ip, g_dns_config.dns_server, 4) == 0) {
                // Parse response
                result = _dns_parse_response(response_buffer, recv_len, response);
                if (result == DNS_OK) {
                    return DNS_OK;
                }
            }
            
            osDelay(100); // Small delay to prevent busy waiting
        }
    }
    
    return DNS_ERROR_TIMEOUT;
}

/**
 * @brief Resolve domain name to IPv4 address
 */
int8_t dns_resolve_a(const char* hostname, uint8_t* ip_addr, uint32_t* ttl)
{
    if (hostname == NULL || ip_addr == NULL) {
        return DNS_ERROR_INVALID_PARAM;
    }

    // Check if socket is open
    if (getSocketStatus(g_dns_config.socket_num) != SOCKET_UDP) {
        int result = socket(g_dns_config.socket_num, SOCKET_PROTOCOL_UDP, g_source_port,
            g_dns_config.tx_buf, g_dns_config.tx_buf_len,
            g_dns_config.rx_buf, g_dns_config.rx_buf_len, NULL);
        
        if (result != SOCK_OK) {
            return DNS_ERROR_SOCKET_FAIL;
        }
    }
    
    // Follow CNAME chains up to a reasonable depth to avoid loops
    // Use static buffer to avoid stack overflow
    strncpy(current_name, hostname, sizeof(current_name) - 1);
    current_name[sizeof(current_name) - 1] = '\0';
    
    const uint8_t max_depth = 5;
    for (uint8_t depth = 0; depth < max_depth; depth++) {
        int8_t result = _dns_query(current_name, DNS_TYPE_A, &dns_response);
        if (result != DNS_OK) {
            return result;
        }
        
        if (dns_response.has_answer && dns_response.answer.type == DNS_TYPE_A) {
            memcpy(ip_addr, dns_response.answer.ip_addr, 4);
            if (ttl != NULL) {
                *ttl = dns_response.answer.ttl;
            }
            return DNS_OK;
        }
        
        if (dns_response.has_cname && dns_response.cname.cname[0] != '\0') {
            // Continue resolution with CNAME target
            strncpy(current_name, dns_response.cname.cname, sizeof(current_name) - 1);
            current_name[sizeof(current_name) - 1] = '\0';
            continue;
        }
        
        // No A and no CNAME to follow
        return DNS_ERROR_NAME_NOT_FOUND;
    }
    
    // Exceeded CNAME chain depth
    return DNS_ERROR_NAME_NOT_FOUND;
}

/**
 * @brief Set DNS server address
 */
int8_t dns_set_server(const uint8_t* dns_server)
{
    if (dns_server == NULL) {
        return DNS_ERROR_INVALID_PARAM;
    }
    
    memcpy(g_dns_config.dns_server, dns_server, 4);
    return DNS_OK;
}

/**
 * @brief Get current DNS server address
 */
int8_t dns_get_server(uint8_t* dns_server)
{
    if (dns_server == NULL) {
        return DNS_ERROR_INVALID_PARAM;
    }
    
    memcpy(dns_server, g_dns_config.dns_server, 4);
    return DNS_OK;
}

/**
 * @brief Set DNS timeout
 */
int8_t dns_set_timeout(uint16_t timeout_ms)
{
    if (timeout_ms == 0) {
        return DNS_ERROR_INVALID_PARAM;
    }
    
    g_dns_config.timeout_ms = timeout_ms;
    return DNS_OK;
}

/**
 * @brief Get DNS timeout
 */
uint16_t dns_get_timeout(void)
{
    return g_dns_config.timeout_ms;
}

/**
 * @brief Set maximum number of retries
 */
int8_t dns_set_max_retries(uint8_t max_retries)
{
    g_dns_config.max_retries = max_retries;
    return DNS_OK;
}

/**
 * @brief Get maximum number of retries
 */
uint8_t dns_get_max_retries(void)
{
    return g_dns_config.max_retries;
}

/**
 * @brief Close DNS client and free resources
 */
int8_t dns_close(void)
{
    if (g_dns_initialized) {
        close(g_dns_config.socket_num);
        g_dns_initialized = false;
    }
    
    return DNS_OK;
}

/**
 * @brief Get string representation of DNS error code
 */
const char* dns_get_error_string(int8_t error_code)
{
    int index = -error_code;
    
    if (index >= 0 && index < (int)(sizeof(dns_error_strings) / sizeof(dns_error_strings[0]))) {
        return dns_error_strings[index];
    }
    
    return "Unknown error";
}
