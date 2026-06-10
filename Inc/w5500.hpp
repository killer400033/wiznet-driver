#ifndef W5500_HPP
#define W5500_HPP

#include <stdint.h>
#include <stdbool.h>
#include "queue.hpp"

#define COMMAND_QUEUE_SIZE 1000
#define SOCKET_QUEUE_SIZE 15
#define COMMAND_BUFFER_SIZE 10
#define MAX_SOCK_NUM _WIZCHIP_SOCK_NUM_  // Maximum number of sockets

#define MAC_ADDRESS { 0x00, 0x08, 0xDC, 0xAB, 0xCD, 0xEF }
#define INT_LEVEL 0x0000
#define RETRY_TIME 1000 // Retry time in milliseconds
#define RETRY_COUNT 5 // Retry count
#define KEEP_ALIVE_TIMER 15 // Keep-alive timer in seconds (set to 0 to disable)

// Helper macros to convert 2-byte big-endian arrays to uint16_t
#define GET_U16(arr) ((uint16_t)((arr)[0] << 8) | (arr)[1])
#define SET_U16(arr, val) do { (arr)[0] = ((val) >> 8) & 0xFF; (arr)[1] = (val) & 0xFF; } while(0)

typedef enum {
    WRITE_REG = 0, // Write max 4 bytes to WIZNET
    WRITE_BUF = 1, // Write buffer to WIZNET
    READ_REG = 2, // Read max 4 bytes from WIZNET
    READ_BUF = 3, // Read buffer from WIZNET
    READ_SIR = 4, // Read SIR register from WIZNET
    READ_SOC = 5, // Read full socket registers from WIZNET
    CHECK_RCV = 6, // Read RX_RSR and RX_RD and enqueue RECV command if there is data to receive
    CHECK_TX_FSR = 7, // Read TX_FSR and enqueue SEND command if there is space in the WIZNET TX buffer
    IDLE = 8, // Nothing is running, queue is empty
} command_type_t;

typedef struct {
    volatile command_type_t cmd_type;
    uint8_t sn;
    uint8_t inline_buf[COMMAND_BUFFER_SIZE];
    uint8_t *ptr;
    uint16_t len;
} command_t;

// Constant for idle command state
#define IDLE_COMMAND {IDLE, 0, {0}, nullptr, 0}

typedef struct
{
    uint8_t MR;          // 0x0000 Mode

    uint8_t GAR[4];      // 0x0001–0x0004 Gateway IP
    uint8_t SUBR[4];     // 0x0005–0x0008 Subnet mask
    uint8_t SHAR[6];     // 0x0009–0x000E Source MAC
    uint8_t SIPR[4];     // 0x000F–0x0012 Source IP

    uint8_t INTLEVEL[2]; // 0x0013–0x0014 Interrupt Low Level Timer (INTLEVEL0/1)
    uint8_t IR;          // 0x0015 Interrupt
    uint8_t IMR;         // 0x0016 Interrupt Mask
    uint8_t SIR;         // 0x0017 Socket Interrupt
    uint8_t SIMR;        // 0x0018 Socket Interrupt Mask

    uint8_t RTR[2];      // 0x0019–0x001A Retry Time
    uint8_t RCR;         // 0x001B Retry Count
} common_regs_t;

#pragma pack(push, 1)
typedef struct {
    // SPI header bytes (for TransmitReceive operations)
    uint8_t  _spi_addr_high;  // Address high byte
    uint8_t  _spi_addr_low;   // Address low byte  
    uint8_t  _spi_control;    // Control byte (block select + R/W)
    
    // W5500 Socket Register Map (starts at 0x0000 in socket register block)
    uint8_t  MR;              // 0x0000  Sn_MR
    uint8_t  CR;              // 0x0001  Sn_CR
    uint8_t  IR;              // 0x0002  Sn_IR
    uint8_t  SR;              // 0x0003  Sn_SR

    uint8_t  PORT[2];         // 0x0004–0x0005  Sn_PORT0/1 (big-endian)
    uint8_t  DHAR[6];         // 0x0006–0x000B  Sn_DHAR0..5
    uint8_t  DIPR[4];         // 0x000C–0x000F  Sn_DIPR0..3
    uint8_t  DPORT[2];        // 0x0010–0x0011  Sn_DPORT0/1
    uint8_t  MSSR[2];         // 0x0012–0x0013  Sn_MSSR0/1

    uint8_t  _res_0014;       // 0x0014  Reserved

    uint8_t  TOS;             // 0x0015  Sn_TOS
    uint8_t  TTL;             // 0x0016  Sn_TTL

    uint8_t  _res_0017_001D[7]; // 0x0017–0x001D  Reserved

    uint8_t  RXBUF_SIZE;      // 0x001E  Sn_RXBUF_SIZE
    uint8_t  TXBUF_SIZE;      // 0x001F  Sn_TXBUF_SIZE

    uint8_t  TX_FSR[2];       // 0x0020–0x0021  Sn_TX_FSR0/1
    uint8_t  TX_RD[2];        // 0x0022–0x0023  Sn_TX_RD0/1
    uint8_t  TX_WR[2];        // 0x0024–0x0025  Sn_TX_WR0/1
    uint8_t  RX_RSR[2];       // 0x0026–0x0027  Sn_RX_RSR0/1
    uint8_t  RX_RD[2];        // 0x0028–0x0029  Sn_RX_RD0/1
    uint8_t  RX_WR[2];        // 0x002A–0x002B  Sn_RX_WR0/1

    uint8_t  IMR;             // 0x002C  Sn_IMR
    uint8_t  FRAG[2];         // 0x002D–0x002E  Sn_FRAG0/1
    uint8_t  KPALVTR;         // 0x002F  Sn_KPALVTR
} socket_regs_t;
#pragma pack(pop)

typedef struct {
    uint16_t start_index;  // Start index of data segment in buffer
    uint16_t end_index;    // End index of data segment in buffer (exclusive)

    // Used for UDP packets
    uint8_t addr[4];         // Destination IP address
    uint16_t port;         // Destination port number
} buffer_segment_t;

typedef struct socket_t {
    socket_regs_t registers;  // Socket registers
    
    uint8_t* tx_buf;
    uint16_t tx_buf_len;
    Queue<buffer_segment_t, SOCKET_QUEUE_SIZE> tx_buf_queue;

    uint8_t* rx_buf;
    uint16_t rx_buf_len;
    Queue<buffer_segment_t, SOCKET_QUEUE_SIZE> rx_buf_queue;

    socket_callback_t callback; // Callback function
    bool is_sending;          // Is currently sending data
    bool is_receiving;        // Is currently receiving data
} socket_t;

extern socket_t sockets[_WIZCHIP_SOCK_NUM_];

// W5500 low-level functions
bool enqueueSetReg(uint8_t sn, uint32_t addr, const uint8_t* data, uint16_t len);
bool enqueueGetReg(uint8_t sn, uint32_t addr, uint8_t* buffer, uint16_t len);
int pollRegNoIT(uint8_t sn, uint32_t addr, uint8_t* reg, uint16_t val, bool inv=false, uint16_t timeout=2000);
int pollRegWithIT(uint8_t sn, uint32_t addr, uint8_t* reg, uint16_t val, bool inv=false);
int16_t getTXBufferIndex(socket_t* socket, uint16_t len);
int16_t getRXBufferIndex(socket_t* socket, uint16_t len);

/**
 * @brief Initiate sending of pending data on a socket
 * @warning **MUST BE CALLED INSIDE A CRITICAL SECTION!**
 * @param sn Socket number (0-7)
 */
void sendPendingData(uint8_t sn);

/**
 * @brief Initiate receiving of pending data on a socket
 * @warning **MUST BE CALLED INSIDE A CRITICAL SECTION!**
 * @param sn Socket number (0-7)
 */
void receivePendingData(uint8_t sn);

// External declarations
extern command_t running_cmd;
extern common_regs_t common_regs;

#endif // W5500_HPP
