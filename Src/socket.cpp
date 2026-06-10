#include <cstring>
#include "socket.h"
#include "cmsis_os.h"
#include "main.h"
#include "w5500.hpp"
#include "queue.hpp"

/**
 * @brief Initialize a socket with protocol and port
 * @warning **MUST use tx_buf and rx_buf sizes that are at least 3 bytes larger than the respective WIZNET socket buffer sizes!**
 * @warning **MUST NOT be called from interrupt context!** This function uses blocking operations and taskENTER_CRITICAL().
 * @param sn Socket number (0-7)
 * @param protocol TCP or UDP protocol
 * @param port Source port number
 * @param tx_buf Pointer to TX buffer for this socket
 * @param tx_buf_len Size of TX buffer
 * @param rx_buf Pointer to RX buffer for this socket
 * @param rx_buf_len Size of RX buffer
 * @param callback Callback function for socket events
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int socket(uint8_t sn, uint8_t protocol, uint16_t port, uint8_t* tx_buf, uint16_t tx_buf_len, uint8_t* rx_buf, uint16_t rx_buf_len, socket_callback_t callback) {
    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_INVALID_PARAM;
    }

    if (rx_buf_len < sockets[sn].registers.RXBUF_SIZE * 1024 + 3) {
        return SOCKERR_RXBUF_TOO_SMALL;
    }

    // Clear socket send and receive queues
    taskENTER_CRITICAL();
    queueClear(&sockets[sn].tx_buf_queue);
    queueClear(&sockets[sn].rx_buf_queue);

    // Reset states
    sockets[sn].is_sending = false;
    sockets[sn].is_receiving = false;
    taskEXIT_CRITICAL();

    // Store callback
    sockets[sn].callback = callback;
    
    // Store buffer configuration
    sockets[sn].tx_buf = tx_buf;
    sockets[sn].tx_buf_len = tx_buf_len;
    sockets[sn].rx_buf = rx_buf;
    sockets[sn].rx_buf_len = rx_buf_len;
    
    // Write Sn_MR (Mode Register) - TCP or UDP
    sockets[sn].registers.MR = (uint8_t)protocol;
    if (protocol == SOCKET_PROTOCOL_TCP) {
        sockets[sn].registers.MR |= Sn_MR_ND; // No Delayed ACK(TCP)
    }
    if (!enqueueSetReg(sn, Sn_MR(sn), &sockets[sn].registers.MR, 1)) {
        return SOCKERR_QUEUE_FULL;
    }
    
    // Write Sn_PORT (Source Port) - 2 bytes, big-endian
    sockets[sn].registers.PORT[0] = (port >> 8) & 0xFF;
    sockets[sn].registers.PORT[1] = port & 0xFF;
    if (!enqueueSetReg(sn, Sn_PORT(sn), sockets[sn].registers.PORT, 2)) {
        return SOCKERR_QUEUE_FULL;
    }

    // Write Sn_IMR (Interrupt Mask Register) - 0xFF
    sockets[sn].registers.IMR = 0xFF;
    if (!enqueueSetReg(sn, Sn_IMR(sn), &sockets[sn].registers.IMR, 1)) {
        return SOCKERR_QUEUE_FULL;
    }

    // Write Sn_CR (Command Register) - OPEN
    uint8_t open_cmd = Sn_CR_OPEN;
    if (!enqueueSetReg(sn, Sn_CR(sn), &open_cmd, 1)) {
        return SOCKERR_QUEUE_FULL;
    }

    // Get TX_RSR, TX_WR and TX_RD
    if (!enqueueGetReg(sn, Sn_TX_FSR(sn), (uint8_t*)&sockets[sn].registers.TX_FSR, 6)) {
        return SOCKERR_QUEUE_FULL;
    }
    // Get RX_RSR, RX_WR and RX_RD
    if (!enqueueGetReg(sn, Sn_RX_RSR(sn), (uint8_t*)&sockets[sn].registers.RX_RSR, 6)) {
        return SOCKERR_QUEUE_FULL;
    }

    int ret;
    if (protocol == SOCKET_PROTOCOL_TCP) {
        // Poll for socket to be in init state (TCP)
        ret = pollRegNoIT(sn, Sn_SR(sn), &sockets[sn].registers.SR, SOCK_INIT);
        if (ret != SOCK_OK) {
            return ret;
        }
    }
    else if (protocol == SOCKET_PROTOCOL_UDP) {
        // Poll for socket to be in udp state (UDP)
        ret = pollRegNoIT(sn, Sn_SR(sn), &sockets[sn].registers.SR, SOCK_UDP);
        if (ret != SOCK_OK) {
            return ret;
        }
    }
    
    return SOCK_OK;
}

/**
 * @brief Close a socket
 * @warning **MUST NOT be called from interrupt context!** This function uses blocking operations and taskENTER_CRITICAL().
 * @param sn Socket number (0-7)
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int close(uint8_t sn) {
    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_INVALID_PARAM;
    }
    
    // Write Sn_CR (Command Register) - CLOSE
    uint8_t close_cmd = Sn_CR_CLOSE;
    if (!enqueueSetReg(sn, Sn_CR(sn), &close_cmd, 1)) {
        return SOCKERR_QUEUE_FULL;
    }

    // Poll for socket to be in closed state
    int ret = pollRegNoIT(sn, Sn_SR(sn), &sockets[sn].registers.SR, SOCK_CLOSED);
    if (ret != SOCK_OK) {
        return ret;
    }
    return SOCK_OK;
}

/**
 * @brief Connect to a destination (TCP only - sets destination IP and port, then initiates connection)
 * @warning **MUST NOT be called from interrupt context!** This function uses blocking operations and taskENTER_CRITICAL().
 * @param sn Socket number (0-7)
 * @param addr Destination IP address (4 bytes)
 * @param port Destination port number
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int connect(uint8_t sn, uint8_t* addr, uint16_t port) {
    if (sn >= _WIZCHIP_SOCK_NUM_ || addr == NULL) {
        return SOCKERR_INVALID_PARAM;
    }

    if (sockets[sn].registers.SR != SOCK_INIT) {
        return SOCKERR_INVALID_STATE;
    }

    // Write Sn_DIPR (Destination IP Address) - 4 bytes
    memcpy(sockets[sn].registers.DIPR, addr, 4);
    if (!enqueueSetReg(sn, Sn_DIPR(sn), addr, 4)) {
        return SOCKERR_QUEUE_FULL;
    }
    
    // Write Sn_DPORT (Destination Port) - 2 bytes, big-endian
    uint8_t dport_bytes[2];
    dport_bytes[0] = (port >> 8) & 0xFF;
    dport_bytes[1] = port & 0xFF;
    if (!enqueueSetReg(sn, Sn_DPORT(sn), dport_bytes, 2)) {
        return SOCKERR_QUEUE_FULL;
    }

    // Write Sn_CR (Command Register) - CONNECT
    uint8_t connect_cmd = Sn_CR_CONNECT;
    if (!enqueueSetReg(sn, Sn_CR(sn), &connect_cmd, 1)) {
        return SOCKERR_QUEUE_FULL;
    }

    // Poll for socket to exit SOCK_INIT state
    int ret = pollRegWithIT(sn, Sn_SR(sn), &sockets[sn].registers.SR, SOCK_INIT, true);
    if (ret != SOCK_OK) {
        return ret;
    }

    // Check if socket is in SOCK_ESTABLISHED state
    if (sockets[sn].registers.SR != SOCK_ESTABLISHED) {
        return SOCKERR_TCP_TIMEOUT;
    }

    return SOCK_OK;
}

/**
 * @brief Disconnect a TCP socket
 * @warning **MUST NOT be called from interrupt context!** This function uses blocking operations and taskENTER_CRITICAL().
 * @param sn Socket number (0-7)
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int disconnect(uint8_t sn) {
    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKERR_INVALID_PARAM;
    }

    if (sockets[sn].registers.SR != SOCK_ESTABLISHED && sockets[sn].registers.SR != SOCK_CLOSE_WAIT) {
        return SOCKERR_INVALID_STATE;
    }
    
    // Write Sn_CR (Command Register) - DISCONNECT
    uint8_t disconnect_cmd = Sn_CR_DISCON;
    if (!enqueueSetReg(sn, Sn_CR(sn), &disconnect_cmd, 1)) {
        return SOCKERR_QUEUE_FULL;
    }
    
    // Poll for socket to be in closed state
    int ret = pollRegNoIT(sn, Sn_SR(sn), &sockets[sn].registers.SR, SOCK_CLOSED);
    if (ret != SOCK_OK) {
        return ret;
    }

    return SOCK_OK;
}

/**
 * @brief Send data on a TCP socket
 * @warning **MUST NOT be called from interrupt context!** This function uses taskENTER_CRITICAL() and may block.
 * @param sn Socket number (0-7)
 * @param buf Data buffer to send
 * @param len Length of data to send
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int send(uint8_t sn, uint8_t* buf, uint16_t len) {
    if (sn >= _WIZCHIP_SOCK_NUM_ || buf == NULL || len == 0) {
        return SOCKERR_INVALID_PARAM;
    }

    if (sockets[sn].registers.SR != SOCK_ESTABLISHED && sockets[sn].registers.SR != SOCK_CLOSE_WAIT) {
        return SOCKERR_INVALID_STATE;
    }

    if (sockets[sn].registers.TXBUF_SIZE * 1024 < len || sockets[sn].tx_buf_len < len + 3) {
        return SOCKERR_PACK_TOO_LRG;
    }

    // Critical section
    // Ensure queue is not modified or DMA write isn't attempted until we finish
    taskENTER_CRITICAL();

    int16_t write_index = getTXBufferIndex(&sockets[sn], len + 3);
    bool success = false;
    
    if (write_index != -1) {        
        // Add segment to queue
        buffer_segment_t new_segment;
        new_segment.start_index = write_index;
        new_segment.end_index = write_index + len + 3;
        success = queuePushBack(&sockets[sn].tx_buf_queue, new_segment);

        if (success) {
            memcpy(sockets[sn].tx_buf + write_index + 3, buf, len);
        }
    }

    sendPendingData(sn);

    taskEXIT_CRITICAL();

    if (write_index == -1) {
        return SOCKERR_TXBUF_FULL;
    }

    if (!success) {
        return SOCKERR_QUEUE_FULL;
    }
    return SOCK_OK;
}

/**
 * @brief Send data on a UDP socket to a specific destination
 * @warning **MUST NOT be called from interrupt context!** This function uses taskENTER_CRITICAL() and may block.
 * @param sn Socket number (0-7)
 * @param buf Data buffer to send
 * @param len Length of data to send
 * @param addr Destination IP address (4 bytes)
 * @param port Destination port number
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int sendto(uint8_t sn, uint8_t * buf, uint16_t len, uint8_t *addr, uint16_t port) {
    if (sn >= _WIZCHIP_SOCK_NUM_ || buf == NULL || len == 0 || addr == NULL || port == 0) {
        return SOCKERR_INVALID_PARAM;
    }

    if (sockets[sn].registers.SR != SOCK_UDP) {
        return SOCKERR_INVALID_STATE;
    }

    if (sockets[sn].registers.TXBUF_SIZE * 1024 < len || sockets[sn].tx_buf_len < len + 3) {
        return SOCKERR_PACK_TOO_LRG;
    }
    
    // Critical section
    // Ensure queue is not modified or DMA write isn't attempted until we finish
    taskENTER_CRITICAL();

    int16_t write_index = getTXBufferIndex(&sockets[sn], len + 3);
    bool success = false;
    
    if (write_index != -1) {
        // Add segment to queue
        buffer_segment_t new_segment;
        new_segment.start_index = write_index;
        new_segment.end_index = write_index + len + 3;
        memcpy(new_segment.addr, addr, 4);
        new_segment.port = port;
        success = queuePushBack(&sockets[sn].tx_buf_queue, new_segment);

        if (success) {
            memcpy(sockets[sn].tx_buf + write_index + 3, buf, len);
        }
    }

    sendPendingData(sn);

    taskEXIT_CRITICAL();

    if (write_index == -1) {
        return SOCKERR_TXBUF_FULL;
    }
    
    if (!success) {
        return SOCKERR_QUEUE_FULL;
    }
    return SOCK_OK;
}

/**
 * @brief Receive data from a TCP socket
 * @param sn Socket number (0-7)
 * @param buf Buffer to store received data
 * @param len Maximum length of data to receive
 * @return Number of bytes received, 0 if no data available
 */
uint32_t recv(uint8_t sn, uint8_t * buf, uint16_t len) {
    if (sn >= _WIZCHIP_SOCK_NUM_ || buf == NULL || len == 0) {
        return 0;
    }

    uint32_t total_copied = 0;
    
    taskENTER_CRITICAL();
    
    while (total_copied < len) {
        buffer_segment_t segment;
        
        // Peek at front of queue
        if (!queuePeekFront(&sockets[sn].rx_buf_queue, &segment)) {
            // Queue is empty
            break;
        }
        
        // Calculate actual data length (skip 3 SPI bytes)
        uint16_t data_len = segment.end_index - segment.start_index - 3;
        
        // Check if it fits in remaining buffer space
        if (data_len > (len - total_copied)) {
            // Won't fit, stop here
            break;
        }
        
        // Copy data (skip 3 SPI bytes at start)
        memcpy(buf + total_copied, sockets[sn].rx_buf + segment.start_index + 3, data_len);
        total_copied += data_len;
        
        // Pop the segment from queue
        queuePopFront(&sockets[sn].rx_buf_queue, &segment);
    }
    // Receive any pending data from wiznet now that buffer is more free
    receivePendingData(sn);

    taskEXIT_CRITICAL();
    
    return total_copied;
}

/**
 * @brief Receive data from a UDP socket with source address and port
 * @param sn Socket number (0-7)
 * @param buf Buffer to store received data
 * @param len Maximum length of data to receive
 * @param addr Buffer to store source IP address (4 bytes)
 * @param port Buffer to store source port number
 * @return Number of bytes received (data only, excluding header), 0 if no data available
 */
uint32_t recvfrom(uint8_t sn, uint8_t * buf, uint16_t len, uint8_t * addr, uint16_t *port) {
    if (sn >= _WIZCHIP_SOCK_NUM_ || buf == NULL || len == 0) {
        return 0;
    }

    uint32_t data_copied = 0;
    
    taskENTER_CRITICAL();
    
    buffer_segment_t segment;
    
    // Peek at front of queue
    if (queuePeekFront(&sockets[sn].rx_buf_queue, &segment)) {
        // UDP packet format: [3 SPI bytes][4 addr][2 port][2 len][data]
        // Total header overhead = 3 + 8 = 11 bytes
        
        uint16_t segment_remaining = segment.end_index - segment.start_index;
        
        // Check if we have at least SPI bytes + UDP header (3 + 8 = 11 bytes)
        if (segment_remaining >= 11) {
            uint8_t* segment_start = sockets[sn].rx_buf + segment.start_index + 3;
            
            // Read the actual packet length from metadata (bytes 6-7)
            uint16_t packet_len = (segment_start[6] << 8) | segment_start[7];
            
            // Check if packet data fits in user buffer
            if (packet_len <= len) {
                // Copy source address (4 bytes)
                if (addr != NULL) {
                    memcpy(addr, segment_start, 4);
                }
                
                // Copy source port (2 bytes, big-endian)
                if (port != NULL) {
                    *port = (segment_start[4] << 8) | segment_start[5];
                }
                
                // Copy actual data (starts at offset 8)
                memcpy(buf, segment_start + 8, packet_len);
                data_copied = packet_len;
                
                // Calculate bytes processed: 3 SPI + 8 metadata + packet_len
                uint16_t bytes_processed = 3 + 8 + packet_len;
                
                // Update segment start index
                uint16_t new_start_index = segment.start_index + bytes_processed;
                
                // Pop the current segment
                queuePopFront(&sockets[sn].rx_buf_queue, &segment);
                
                // Check if there's more data in the segment (more UDP packets)
                if (new_start_index + 8 < segment.end_index) {
                    // More packets remain, update and push back to front
                    segment.start_index = new_start_index - 3; // Leave space for "fake" SPI bytes
                    queuePushFront(&sockets[sn].rx_buf_queue, segment);
                }
                // If new_start_index == segment.end_index, segment is fully consumed (already popped)
            }
        }
    }
    // Receive any pending data from wiznet now that buffer is more free
    receivePendingData(sn);
    
    taskEXIT_CRITICAL();
    
    return data_copied;
}

/**
 * @brief Get the status of a socket
 * @param sn Socket number (0-7)
 * @return Socket status
 */
socket_status_t getSocketStatus(uint8_t sn) {
    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return SOCKET_OTHER;
    }
    
    switch (sockets[sn].registers.SR) {
        case SOCK_CLOSED:
            return SOCKET_CLOSED;
        case SOCK_INIT:
            return SOCKET_INIT;
        case SOCK_LISTEN:
            return SOCKET_LISTEN;
        case SOCK_ESTABLISHED:
            return SOCKET_ESTABLISHED;
        case SOCK_CLOSE_WAIT:
            return SOCKET_CLOSE_WAIT;
        case SOCK_UDP:
            return SOCKET_UDP;
    }
    return SOCKET_OTHER;
}