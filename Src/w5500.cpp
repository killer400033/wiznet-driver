#include <cstring>
#include "socket.h"
#include "cmsis_os.h"
#include "main.h"
#include "w5500_macros.h"
#include "w5500.hpp"
#include "w5500_driver.h"

// Forward declarations
static void kickStartCommands(void);
static void dmaTXCompleteCallback(void);
static void dmaRXCompleteCallback(void);
static void handleSPIFailure(void);
static inline void generateSetRegCmd(command_t* cmd, uint8_t sn, uint32_t addr, const uint8_t* data, uint16_t len);
static inline void generateGetRegCmd(command_t* cmd, uint8_t sn, uint32_t addr, uint8_t* buffer, uint16_t len);
static inline void generateSetBufCmd(command_t* cmd, uint8_t sn, uint32_t addr, uint8_t* buffer, uint16_t len);
static inline void generateGetBufCmd(command_t* cmd, uint8_t sn, uint32_t addr, uint8_t* buffer, uint16_t len);

// Variables used for logging and debugging
volatile uint32_t enqueueFailsInISR = 0;
volatile uint32_t commandRetries = 0;
volatile uint32_t spiErrCount = 0;

static Queue<command_t, COMMAND_QUEUE_SIZE> command_queue;
command_t running_cmd = IDLE_COMMAND; // Starts off IDLE
common_regs_t common_regs = {0};

socket_t sockets[_WIZCHIP_SOCK_NUM_] = {0};

static SPI_HandleTypeDef* wiznet_hspi = NULL;
static GPIO_TypeDef* wiznet_cs_port = NULL;
static uint16_t wiznet_cs_pin = 0;

/**
 * @brief Configure W5500 hardware (SPI, CS pin, and timer)
 * @warning Must be called before initWizchip()
 * @param hspi SPI handle for W5500 communication
 * @param cs_port GPIO port for CS pin (e.g., GPIOA)
 * @param cs_pin GPIO pin for CS (e.g., GPIO_PIN_4)
 * @param htim Timer handle for periodic socket polling
 * @return SOCK_OK (0) on success, SOCKERR_INVALID_PARAM if parameters are invalid
 */
int setWiznetHardware(SPI_HandleTypeDef* hspi, GPIO_TypeDef* cs_port, uint16_t cs_pin, TIM_HandleTypeDef* htim) {
    if (hspi == NULL || cs_port == NULL || htim == NULL) {
        return SOCKERR_INVALID_PARAM;
    }

    wiznet_hspi = hspi;
    wiznet_cs_port = cs_port;
    wiznet_cs_pin = cs_pin;
    
    // Initialize CS pin to deselected (high)
    HAL_GPIO_WritePin(wiznet_cs_port, wiznet_cs_pin, GPIO_PIN_SET);
    
    // Configure timer (550MHz)
    #define WIZNET_TIM_CLK 275000000
    #define WIZNET_POLL_RATE 50
    
    htim->Instance->PSC = 55000-1;
    htim->Instance->ARR = (WIZNET_TIM_CLK / 55000) / WIZNET_POLL_RATE - 1;
    htim->Instance->CNT = 0;
    
    // Generate update event to load the prescaler value
    htim->Instance->EGR = TIM_EGR_UG;
    
    // Start timer with update (overflow) interrupt
    HAL_TIM_Base_Start_IT(htim);
    
    return SOCK_OK;
}

/**
 * @brief Initialize W5500 chip with network configuration
 * @warning **MUST NOT be called from interrupt context!** This function uses blocking operations, osDelay(), and taskENTER_CRITICAL().
 * @warning Must call setWiznetHardware() before calling this function
 * @param ip_address IP address (4 bytes)
 * @param subnet_mask Subnet mask (4 bytes)
 * @param gateway_ip Gateway IP address (4 bytes)
 * @param rx_buf_sizes RX buffer sizes for all 8 sockets (in KB)
 * @param tx_buf_sizes TX buffer sizes for all 8 sockets (in KB)
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int initWizchip(uint8_t* ip_address, uint8_t* subnet_mask, uint8_t* gateway_ip,
                const uint8_t* rx_buf_sizes, const uint8_t* tx_buf_sizes) {
    // Verify hardware has been configured
    if (wiznet_hspi == NULL || wiznet_cs_port == NULL) {
        return SOCKERR_INVALID_PARAM;
    }

    const uint8_t mac_addr[] = MAC_ADDRESS;
    const uint16_t intlevel = INT_LEVEL;
    const uint16_t rtr = RETRY_TIME * 10;
    const uint8_t rcr = RETRY_COUNT;

    queueInit(&command_queue);
    
    // Set callback to kick-start queue processing when pushing to empty queue
    queueSetFirstPushCallback(&command_queue, kickStartCommands);

    // Write reset command
    common_regs.MR = MR_RST;
    if (!enqueueSetReg(0xFF, W5500_MR, &common_regs.MR, 1)) {
        return SOCKERR_QUEUE_FULL;
    }

    // Poll for reset to complete
    int ret = pollRegNoIT(0xFF, W5500_MR, &common_regs.MR, 0x00);
    if (ret != SOCK_OK) {
        return ret;
    }
    
    // Write Network Information
    memcpy(common_regs.GAR, gateway_ip, 4);
    if (!enqueueSetReg(0xFF, W5500_GAR, common_regs.GAR, 4)) {
        return SOCKERR_QUEUE_FULL;
    }
    memcpy(common_regs.SUBR, subnet_mask, 4);
    if (!enqueueSetReg(0xFF, W5500_SUBR, common_regs.SUBR, 4)) {
        return SOCKERR_QUEUE_FULL;
    }
    memcpy(common_regs.SHAR, mac_addr, 6);
    if (!enqueueSetReg(0xFF, W5500_SHAR, common_regs.SHAR, 6)) {
        return SOCKERR_QUEUE_FULL;
    }
    memcpy(common_regs.SIPR, ip_address, 4);
    if (!enqueueSetReg(0xFF, W5500_SIPR, common_regs.SIPR, 4)) {
        return SOCKERR_QUEUE_FULL;
    }
    
    // Write INTLEVEL
    common_regs.INTLEVEL[0] = (intlevel >> 8) & 0xFF;
    common_regs.INTLEVEL[1] = intlevel & 0xFF;
    if (!enqueueSetReg(0xFF, W5500_INTLEVEL, common_regs.INTLEVEL, 2)) {
        return SOCKERR_QUEUE_FULL;
    }

    // Enable interrupts for all sockets
    common_regs.SIMR = 0xFF;
    if (!enqueueSetReg(0xFF, W5500_SIMR, &common_regs.SIMR, 1)) {
        return SOCKERR_QUEUE_FULL;
    }

    // Disable all common interrupts
    common_regs.IMR = 0x00;
    if (!enqueueSetReg(0xFF, W5500_IMR, &common_regs.IMR, 1)) {
        return SOCKERR_QUEUE_FULL;
    }
    
    // Write RTR
    common_regs.RTR[0] = (rtr >> 8) & 0xFF;
    common_regs.RTR[1] = rtr & 0xFF;
    if (!enqueueSetReg(0xFF, W5500_RTR, common_regs.RTR, 2)) {
        return SOCKERR_QUEUE_FULL;
    }
    
    // Write RCR
    common_regs.RCR = rcr;
    if (!enqueueSetReg(0xFF, W5500_RCR, &common_regs.RCR, 1)) {
        return SOCKERR_QUEUE_FULL;
    }

    // Write buffer sizes for all 8 sockets
    for (uint8_t i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
        // Write Sn_RXBUF_SIZE (RX Buffer Size) - 1 byte
        sockets[i].registers.RXBUF_SIZE = rx_buf_sizes[i];
        if (!enqueueSetReg(i, Sn_RXBUF_SIZE(i), &sockets[i].registers.RXBUF_SIZE, 1)) {
            return SOCKERR_QUEUE_FULL;
        }
        
        // Write Sn_TXBUF_SIZE (TX Buffer Size) - 1 byte
        sockets[i].registers.TXBUF_SIZE = tx_buf_sizes[i];
        if (!enqueueSetReg(i, Sn_TXBUF_SIZE(i), &sockets[i].registers.TXBUF_SIZE, 1)) {
            return SOCKERR_QUEUE_FULL;
        }
    }

    return SOCK_OK;
}

// **DO NOT CALL FROM INTERRUPT CONTEXT!**
bool enqueueSetReg(uint8_t sn, uint32_t addr, const uint8_t* data, uint16_t len) {
    if (len > COMMAND_BUFFER_SIZE - 3) {
        return false;
    }

    command_t cmd;
    generateSetRegCmd(&cmd, sn, addr, data, len);
    
    taskENTER_CRITICAL();
    bool success = queuePushBack(&command_queue, cmd);
    taskEXIT_CRITICAL();

    return success;
}

// **DO NOT CALL FROM INTERRUPT CONTEXT!**
bool enqueueGetReg(uint8_t sn, uint32_t addr, uint8_t* buffer, uint16_t len) {
    if (len > COMMAND_BUFFER_SIZE - 3) {
        return false;
    }

    command_t cmd;
    generateGetRegCmd(&cmd, sn, addr, buffer, len);
    
    taskENTER_CRITICAL();
    bool success = queuePushBack(&command_queue, cmd);
    taskEXIT_CRITICAL();

    return success;
}

/**
 * @brief Poll a single byte register until it matches a value or timeout occurs
 * this is used for changes that **DO NOT** generate interrupts on the WIZNET
 * @return SOCK_OK on success, SOCKERR_TIMEOUT on timeout, SOCKERR_QUEUE_FULL on register read error
 */
int pollRegNoIT(uint8_t sn, uint32_t addr, uint8_t* reg, uint16_t val, bool inv, uint16_t timeout) {
    uint32_t start_time = osKernelGetTickCount();
    while (osKernelGetTickCount() - start_time < timeout && (*reg != val) ^ inv) {
        if (!enqueueGetReg(sn, addr, reg, 1)) {
            return SOCKERR_QUEUE_FULL;
        }
        osDelay(100);
    }
    if ((*reg == val) ^ inv) {
        return SOCK_OK;
    }
    return SOCKERR_TIMEOUT;
}

/**
 * @brief Poll a single byte register until it matches a value or timeout occurs
 * this is used for changes that **DO** generate interrupts on the WIZNET
 * @return SOCK_OK on success, SOCKERR_TIMEOUT on timeout
 */
int pollRegWithIT(uint8_t sn, uint32_t addr, uint8_t* reg, uint16_t val, bool inv) {
    uint32_t start_time = osKernelGetTickCount();
    // IT based polling should never timeout, but just in case communication is lost, we will timeout after 60 seconds
    while (osKernelGetTickCount() - start_time < 60000 && (*reg != val) ^ inv) {
        osDelay(100);
    }
    if ((*reg == val) ^ inv) {
        return SOCK_OK;
    }
    return SOCKERR_TIMEOUT;
}

/**
 * @brief WIZNET W5500 interrupt callback
 * Called when the W5500 INT pin asserts (active low)
 * This should be called from HAL_GPIO_EXTI_Callback or similar GPIO interrupt handler
 */
void wiznetInterruptCallback(void)
{
    // Enter critical section for ISR
    uint32_t isrm = taskENTER_CRITICAL_FROM_ISR();
    
    // Create READ_SIR command to read Socket Interrupt Register
    command_t cmd;
    generateGetRegCmd(&cmd, 0xFF, W5500_SIR, &common_regs.SIR, 1);
    cmd.cmd_type = READ_SIR;
    
    // Push to front of queue for immediate processing
    if (!queuePushFront(&command_queue, cmd)) {
        enqueueFailsInISR++;
    }
    
    taskEXIT_CRITICAL_FROM_ISR(isrm);
}

/**
 * DMA transmit/receive complete callback
 * Called when a DMA transmit/receive operation is completed
 */
void wiznetSPITxRxCompleteCallback(void)
{
    // CS deselect (set high)
    HAL_GPIO_WritePin(wiznet_cs_port, wiznet_cs_pin, GPIO_PIN_SET);

    // Process the received data first
    dmaRXCompleteCallback();

    // Then handle the TX completion (queue management)
    dmaTXCompleteCallback();
}

/**
 * DMA transmit complete callback
 * Called when a DMA transmission is completed
 */
void wiznetSPITxCompleteCallback(void)
{
    // CS deselect (set high)
    HAL_GPIO_WritePin(wiznet_cs_port, wiznet_cs_pin, GPIO_PIN_SET);

    dmaTXCompleteCallback();
}

/**
 * SPI Error callback (DMA error, overrun, mode fault, CRC error, etc.)
 * Called when any SPI/DMA error occurs instead of the complete callback
 * This prevents the system from hanging when DMA transfers fail
 */
void wiznetSPIErrorCallback(void)
{    
    handleSPIFailure();
}

/**
 * SPI Abort complete callback
 * Called after an SPI abort operation completes
 */
void wiznetSPIAbortCallback(void)
{    
    handleSPIFailure();
}

// Helper to handle SPI failure: deselect CS, re-enqueue command, set running_cmd to IDLE
static inline void handleSPIFailure(void) {
    HAL_GPIO_WritePin(wiznet_cs_port, wiznet_cs_pin, GPIO_PIN_SET);
    
    uint32_t isrm = taskENTER_CRITICAL_FROM_ISR();
    if (!queuePushFront(&command_queue, running_cmd)) {
        enqueueFailsInISR++;
    }
    taskEXIT_CRITICAL_FROM_ISR(isrm);

    running_cmd.cmd_type = IDLE;
    spiErrCount++;
}

/**
 * @brief Get the index of the TX buffer where the next data should be written
 * @param socket The socket to get the index for
 * @param data_length The length of the data to write
 * @return The index of the data buffer where the next data should be written, -1 if there is not enough space
 */
int16_t getTXBufferIndex(socket_t* socket, uint16_t len)
{
    if (queueIsEmpty(&socket->tx_buf_queue)) {
        // Queue is empty - start at beginning if data fits
        return (len <= socket->tx_buf_len) ? 0 : -1;
    }
    
    // Get start and end positions from queue
    int16_t queue_start = queueFront(&socket->tx_buf_queue)->start_index;
    int16_t queue_end = queueBack(&socket->tx_buf_queue)->end_index;
    
    if (queue_end >= queue_start) {
        // End is after beginning: check space at end first, then at beginning
        int16_t space_at_end = socket->tx_buf_len - queue_end;
        int16_t space_at_beginning = queue_start;
        
        if (len <= space_at_end) {
            // Use space at end
            return queue_end;
        } else if (len <= space_at_beginning) {
            // Use space at beginning (wraparound)
            return 0;
        } else {
            // Not enough space
            return -1;
        }
    } else {
        // End is before beginning (already wrapped): only space between end and start
        if (len <= queue_start - queue_end) {
            return queue_end;
        } else {
            // Not enough space
            return -1;
        }
    }
}

/**
 * @brief Get the index of the RX buffer where the next data should be written
 * @param socket The socket to get the index for
 * @param len The length of the data to write
 * @return The index of the RX buffer where the next data should be written, -1 if there is not enough space
 */
int16_t getRXBufferIndex(socket_t* socket, uint16_t len) {
    if (queueIsEmpty(&socket->rx_buf_queue)) {
        // Queue is empty - start at beginning if data fits
        return (len <= socket->rx_buf_len) ? 0 : -1;
    }
    
    // Get start and end positions from queue
    int16_t queue_start = queueFront(&socket->rx_buf_queue)->start_index;
    int16_t queue_end = queueBack(&socket->rx_buf_queue)->end_index;
    
    if (queue_end >= queue_start) {
        // End is after beginning: check space at end first, then at beginning
        int16_t space_at_end = socket->rx_buf_len - queue_end;
        int16_t space_at_beginning = queue_start;
        
        if (len <= space_at_end) {
            // Use space at end
            return queue_end;
        } else if (len <= space_at_beginning) {
            // Use space at beginning (wraparound)
            return 0;
        } else {
            // Not enough space
            return -1;
        }
    } else {
        // End is before beginning (already wrapped): only space between end and start
        if (len <= queue_start - queue_end) {
            return queue_end;
        } else {
            // Not enough space
            return -1;
        }
    }
}

static void kickStartCommands(void) {
    if (running_cmd.cmd_type != IDLE) return;
    dmaTXCompleteCallback();
}

static void dmaTXCompleteCallback(void) {
    // Ensure only we access queue
    uint32_t isrm = taskENTER_CRITICAL_FROM_ISR();

    bool success = queuePopFront(&command_queue, &running_cmd);

    if (!success) {
        running_cmd = IDLE_COMMAND;
    }
    
    taskEXIT_CRITICAL_FROM_ISR(isrm);

    if (!success) return;

    // CS select (set low)
    HAL_GPIO_WritePin(wiznet_cs_port, wiznet_cs_pin, GPIO_PIN_RESET);

    HAL_StatusTypeDef status;

    switch (running_cmd.cmd_type) {
        case WRITE_REG:
            // Check if this is a Sn_CR (Command Register) write
            if (running_cmd.inline_buf[0] == 0x00 && running_cmd.inline_buf[1] == 0x01 && 
                ((running_cmd.inline_buf[2] >> 2) & 0b111) == 0b011 && running_cmd.sn < _WIZCHIP_SOCK_NUM_) {
                command_t read_cmd;
                generateGetRegCmd(&read_cmd, running_cmd.sn, Sn_CR(running_cmd.sn), &sockets[running_cmd.sn].registers.CR, 1);
                
                if (sockets[running_cmd.sn].registers.CR != 0x00) {
                    commandRetries++;
                    // CR register is busy, send read command to get latest CR value instead
                    status = HAL_SPI_TransmitReceive_DMA(wiznet_hspi, read_cmd.inline_buf, read_cmd.inline_buf, read_cmd.len);
                    if (status != HAL_OK) {
                        handleSPIFailure();
                        return;
                    }
                    
                    // Re-enqueue the original write command to front
                    uint32_t isrm = taskENTER_CRITICAL_FROM_ISR();
                    if (!queuePushFront(&command_queue, running_cmd)) {
                        enqueueFailsInISR++;
                    }
                    taskEXIT_CRITICAL_FROM_ISR(isrm);
                }
                else {
                    // CR register is free, send write command
                    status = HAL_SPI_Transmit_DMA(wiznet_hspi, running_cmd.inline_buf, running_cmd.len);
                    if (status != HAL_OK) {
                        handleSPIFailure();
                        return;
                    }

                    // Enqueue read command to front to update CR as soon as possible
                    uint32_t isrm = taskENTER_CRITICAL_FROM_ISR();
                    if (!queuePushFront(&command_queue, read_cmd)) {
                        enqueueFailsInISR++;
                    }
                    taskEXIT_CRITICAL_FROM_ISR(isrm);
                }
            }
            else {
                // Normal write command, just send it
                status = HAL_SPI_Transmit_DMA(wiznet_hspi, running_cmd.inline_buf, running_cmd.len);
                if (status != HAL_OK) {
                    handleSPIFailure();
                    return;
                }
            }
            break;
        case WRITE_BUF:
            status = HAL_SPI_Transmit_DMA(wiznet_hspi, running_cmd.ptr, running_cmd.len);
            if (status != HAL_OK) {
                handleSPIFailure();
                return;
            }
            break;
        case READ_REG:
            status = HAL_SPI_TransmitReceive_DMA(wiznet_hspi, running_cmd.inline_buf, running_cmd.inline_buf, running_cmd.len);
            if (status != HAL_OK) {
                handleSPIFailure();
                return;
            }
            break;
        case READ_BUF:
            status = HAL_SPI_TransmitReceive_DMA(wiznet_hspi, running_cmd.ptr, running_cmd.ptr, running_cmd.len);
            if (status != HAL_OK) {
                handleSPIFailure();
                return;
            }
            break;
        case READ_SIR:
            status = HAL_SPI_TransmitReceive_DMA(wiznet_hspi, running_cmd.inline_buf, running_cmd.inline_buf, running_cmd.len);
            if (status != HAL_OK) {
                handleSPIFailure();
                return;
            }
            break;
        case READ_SOC:
            status = HAL_SPI_TransmitReceive_DMA(wiznet_hspi, running_cmd.ptr, running_cmd.ptr, running_cmd.len);
            if (status != HAL_OK) {
                handleSPIFailure();
                return;
            }
            break;
        case CHECK_RCV:
            status = HAL_SPI_TransmitReceive_DMA(wiznet_hspi, running_cmd.inline_buf, running_cmd.inline_buf, running_cmd.len);
            if (status != HAL_OK) {
                handleSPIFailure();
                return;
            }
            break;
        case CHECK_TX_FSR:
            status = HAL_SPI_TransmitReceive_DMA(wiznet_hspi, running_cmd.inline_buf, running_cmd.inline_buf, running_cmd.len);
            if (status != HAL_OK) {
                handleSPIFailure();
                return;
            }
            break;
        default:
            // CS deselect (set high) - for unknown command types
            HAL_GPIO_WritePin(wiznet_cs_port, wiznet_cs_pin, GPIO_PIN_SET);
            break;
    }
}

static void dmaRXCompleteCallback(void) {
    socket_t* socket = NULL;
    if (running_cmd.sn < _WIZCHIP_SOCK_NUM_) {
        socket = &sockets[running_cmd.sn];
    }
    switch (running_cmd.cmd_type) {
        case READ_REG:
            memcpy(running_cmd.ptr, &(running_cmd.inline_buf[3]), running_cmd.len - 3);
            break;
        
        case READ_BUF: {
            if (socket == NULL) break;

            // Add metadata to rx queue
            buffer_segment_t segment;
            segment.start_index = running_cmd.ptr - socket->rx_buf;
            segment.end_index = segment.start_index + running_cmd.len;

            uint32_t isrm = taskENTER_CRITICAL_FROM_ISR();
            if (!queuePushBack(&socket->rx_buf_queue, segment)) {
                enqueueFailsInISR++;
            }
            taskEXIT_CRITICAL_FROM_ISR(isrm);

            // Data put into buffer, inform upper layer
            if (socket->callback != NULL) {
                uint16_t len = running_cmd.len - 3;
                socket->callback(SOCKET_RECV_CALLBACK, &len);
            }
            break;
        }
        // Called when interrupt is generated, and we read SIR register
        case READ_SIR: {
            memcpy(running_cmd.ptr, &(running_cmd.inline_buf[3]), running_cmd.len - 3);
            command_t cmd;
            for (uint8_t i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
                if ((common_regs.SIR >> i) & 0x01) {
                    generateGetBufCmd(&cmd, i, Sn_MR(i), (uint8_t*)&(sockets[i].registers), sizeof(sockets[i].registers));
                    cmd.cmd_type = READ_SOC;
                    
                    uint32_t isrm = taskENTER_CRITICAL_FROM_ISR();
                    if (!queuePushFront(&command_queue, cmd)) {
                        enqueueFailsInISR++;
                    }
                    taskEXIT_CRITICAL_FROM_ISR(isrm);
                }
            }
            break;
        }
        // Called when interrupt is generated, and we read socket regs for sockets that have interrupts
        case READ_SOC: {
            if (socket == NULL) break;
            
            if (socket->registers.IR & Sn_IR_CON) {
                // Connection established
                // No action needed
            }

            if (socket->registers.IR & Sn_IR_DISCON) {
                if (socket->registers.SR == SOCK_CLOSE_WAIT) {
                    // Disconnect request from peer, inform upper layer
                    if (socket->callback != NULL) {
                        socket->callback(SOCKET_DISCON_REQ_CALLBACK, NULL);
                    }
                }
                else {
                    // Disconnect request from us successful
                    if (socket->callback != NULL) {
                        socket->callback(SOCKET_DISCON_SUC_CALLBACK, NULL);
                    }
                }
            }

            if (socket->registers.IR & Sn_IR_RECV) {
                // Data received
                uint32_t isrm = taskENTER_CRITICAL_FROM_ISR();
                receivePendingData(running_cmd.sn);
                taskEXIT_CRITICAL_FROM_ISR(isrm);
            }

            if (socket->registers.IR & Sn_IR_TIMEOUT) {
                uint32_t isrm_timeout = taskENTER_CRITICAL_FROM_ISR();

                socket->is_sending = false;

                if (socket->registers.MR & Sn_MR_TCP) {
                    // In TCP mode, clear tx buffer just incase after timeout
                    queueClear(&socket->tx_buf_queue);
                }
                else {
                    // In UDP/MACRAW mode, the socket remains open, so we will send the next pending data
                    buffer_segment_t segment;
                    queuePopFront(&socket->tx_buf_queue, &segment);
                    sendPendingData(running_cmd.sn);
                }

                taskEXIT_CRITICAL_FROM_ISR(isrm_timeout);

                // Timeout occurred, inform upper layer
                if (socket->callback != NULL) {
                    socket->callback(SOCKET_TIMEOUT_CALLBACK, NULL);
                }
            }
            
            if (socket->registers.IR & Sn_IR_SENDOK) {
                // Send OK, socket is ready to send data
                uint32_t isrm_send = taskENTER_CRITICAL_FROM_ISR();
                buffer_segment_t segment;
                queuePopFront(&socket->tx_buf_queue, &segment); // Remove segment that was just sent

                socket->is_sending = false;
                sendPendingData(running_cmd.sn);
                taskEXIT_CRITICAL_FROM_ISR(isrm_send);
            }

            // Clear the interrupt register by writing the same value back
            command_t cmd;
            generateSetRegCmd(&cmd, running_cmd.sn, Sn_IR(running_cmd.sn), &socket->registers.IR, 1);
            
            uint32_t isrm = taskENTER_CRITICAL_FROM_ISR();
            if (!queuePushFront(&command_queue, cmd)) {
                enqueueFailsInISR++;
            }
            taskEXIT_CRITICAL_FROM_ISR(isrm);
            break;
        }
        case CHECK_RCV: {
            if (socket == NULL) break;
            
            // Receive process completed, check again if there is more data to receive
            memcpy(running_cmd.ptr, &(running_cmd.inline_buf[3]), running_cmd.len - 3);

            uint32_t isrm = taskENTER_CRITICAL_FROM_ISR();
            sockets[running_cmd.sn].is_receiving = false;
            receivePendingData(running_cmd.sn);
            taskEXIT_CRITICAL_FROM_ISR(isrm);
            break;
        }
        case CHECK_TX_FSR: {
            if (socket == NULL) break;

            memcpy(running_cmd.ptr, &(running_cmd.inline_buf[3]), running_cmd.len - 3);

            uint32_t isrm = taskENTER_CRITICAL_FROM_ISR();
            sendPendingData(running_cmd.sn);
            taskEXIT_CRITICAL_FROM_ISR(isrm);
            break;
        }
        default:
            break;
    }
}

// **MUST BE CALLED INSIDE A CRITICAL SECTION**
void sendPendingData(uint8_t sn) {
    if (sockets[sn].is_sending) return;
    if (sockets[sn].registers.SR != SOCK_ESTABLISHED && sockets[sn].registers.SR != SOCK_CLOSE_WAIT && sockets[sn].registers.SR != SOCK_UDP) return;

    buffer_segment_t segment;
    // Just peek front, will be popped when send is complete
    if (queuePeekFront(&sockets[sn].tx_buf_queue, &segment)) {
        command_t cmd;
        uint32_t tx_wr = GET_U16(sockets[sn].registers.TX_WR);
        uint32_t addr = ((uint32_t)tx_wr << 8) + (WIZCHIP_TXBUF_BLOCK(sn) << 3);
        uint32_t len = segment.end_index - segment.start_index;

        // Check if there is enough space in the WIZNET TX buffer
        if (len - 3 > GET_U16(sockets[sn].registers.TX_FSR)) {
            return;
        }
        // If UDP, set destination address and port
        if (sockets[sn].registers.MR & Sn_MR_UDP) {
            generateSetRegCmd(&cmd, sn, Sn_DIPR(sn), segment.addr, 4);
            if (!queuePushBack(&command_queue, cmd)) {
                enqueueFailsInISR++;
            }
            uint8_t port_bytes[2];
            SET_U16(port_bytes, segment.port);
            generateSetRegCmd(&cmd, sn, Sn_DPORT(sn), port_bytes, 2);
            if (!queuePushBack(&command_queue, cmd)) {
                enqueueFailsInISR++;
            }
        }
        
        generateSetBufCmd(&cmd, sn, addr, sockets[sn].tx_buf + segment.start_index, len);
        if (!queuePushBack(&command_queue, cmd)) {
            enqueueFailsInISR++;
        }

        uint8_t new_tx_wr_bytes[2];
        SET_U16(new_tx_wr_bytes, tx_wr + len - 3);
        generateSetRegCmd(&cmd, sn, Sn_TX_WR(sn), new_tx_wr_bytes, 2);
        if (!queuePushBack(&command_queue, cmd)) {
            enqueueFailsInISR++;
        }

        uint8_t send_cmd = Sn_CR_SEND;
        generateSetRegCmd(&cmd, sn, Sn_CR(sn), &send_cmd, 1);
        if (!queuePushBack(&command_queue, cmd)) {
            enqueueFailsInISR++;
        }

        sockets[sn].is_sending = true;
    }
}

// **MUST BE CALLED INSIDE A CRITICAL SECTION**
void receivePendingData(uint8_t sn) {
    if (sockets[sn].is_receiving) return;
    if (sockets[sn].registers.SR != SOCK_ESTABLISHED && sockets[sn].registers.SR != SOCK_CLOSE_WAIT && sockets[sn].registers.SR != SOCK_UDP) return;

    uint16_t rx_rsr = GET_U16(sockets[sn].registers.RX_RSR);
    if (rx_rsr > 0) {
        command_t cmd;

        int32_t rx_buf_index = getRXBufferIndex(&sockets[sn], rx_rsr + 3);
        if (rx_buf_index != -1) {
            uint16_t rx_rd = GET_U16(sockets[sn].registers.RX_RD);
            uint32_t ptr = rx_rd;
            uint32_t addr = ((uint32_t)ptr << 8) + (WIZCHIP_RXBUF_BLOCK(sn) << 3);
            generateGetBufCmd(&cmd, sn, addr, sockets[sn].rx_buf + rx_buf_index, rx_rsr + 3);
            if (!queuePushBack(&command_queue, cmd)) {
                enqueueFailsInISR++;
            }

            uint8_t rx_rd_bytes[2];
            SET_U16(rx_rd_bytes, rx_rd + rx_rsr);
            generateSetRegCmd(&cmd, sn, Sn_RX_RD(sn), rx_rd_bytes, 2);
            if (!queuePushBack(&command_queue, cmd)) {
                enqueueFailsInISR++;
            }

            uint8_t recv_cmd = Sn_CR_RECV;
            generateSetRegCmd(&cmd, sn, Sn_CR(sn), &recv_cmd, 1);
            if (!queuePushBack(&command_queue, cmd)) {
                enqueueFailsInISR++;
            }
            if (!queuePushBack(&command_queue, cmd)) {
                enqueueFailsInISR++;
            }

            generateGetRegCmd(&cmd, sn, Sn_RX_RSR(sn), (uint8_t*)sockets[sn].registers.RX_RSR, 4);
            cmd.cmd_type = CHECK_RCV;
            if (!queuePushBack(&command_queue, cmd)) {
                enqueueFailsInISR++;
            }

            sockets[sn].is_receiving = true;
        }
    }
}

static inline void generateSetRegCmd(command_t* cmd, uint8_t sn, uint32_t addr, const uint8_t* data, uint16_t len) {
    addr |= (_W5500_SPI_WRITE_ | _W5500_SPI_VDM_OP_);

    cmd->cmd_type = WRITE_REG;
    cmd->sn = sn;
    cmd->inline_buf[0] = (addr >> 16) & 0xFF;
    cmd->inline_buf[1] = (addr >> 8) & 0xFF;
    cmd->inline_buf[2] = addr & 0xFF;
    memcpy(&cmd->inline_buf[3], data, len);
    cmd->len = 3 + len;
}

static inline void generateGetRegCmd(command_t* cmd, uint8_t sn, uint32_t addr, uint8_t* buffer, uint16_t len) {
    addr |= (_W5500_SPI_READ_ | _W5500_SPI_VDM_OP_);

    cmd->cmd_type = READ_REG;
    cmd->sn = sn;
    cmd->ptr = buffer;
    cmd->inline_buf[0] = (addr >> 16) & 0xFF;
    cmd->inline_buf[1] = (addr >> 8) & 0xFF;
    cmd->inline_buf[2] = addr & 0xFF;
    cmd->len = 3 + len;
}

static inline void generateSetBufCmd(command_t* cmd, uint8_t sn, uint32_t addr, uint8_t* buffer, uint16_t len) {
    addr |= (_W5500_SPI_WRITE_ | _W5500_SPI_VDM_OP_);

    cmd->cmd_type = WRITE_BUF;
    cmd->sn = sn;
    cmd->ptr = buffer;
    cmd->ptr[0] = (addr >> 16) & 0xFF;
    cmd->ptr[1] = (addr >> 8) & 0xFF;
    cmd->ptr[2] = addr & 0xFF;
    cmd->len = len;
}

static inline void generateGetBufCmd(command_t* cmd, uint8_t sn, uint32_t addr, uint8_t* buffer, uint16_t len) {
    addr |= (_W5500_SPI_READ_ | _W5500_SPI_VDM_OP_);

    cmd->cmd_type = READ_BUF;
    cmd->sn = sn;
    cmd->ptr = buffer;
    cmd->ptr[0] = (addr >> 16) & 0xFF;
    cmd->ptr[1] = (addr >> 8) & 0xFF;
    cmd->ptr[2] = addr & 0xFF;
    cmd->len = len;
}

/**
 * @brief Timer interrupt callback for periodic socket polling
 * Called when the Wiznet timer overflows
 * Checks each socket and enqueues CHECK_TX_FSR command if socket is ESTABLISHED
 */
void Wiznet_Timer_Callback(void)
{
    // If no command is running and queue isn't empty, process the next command in the queue
    // This will only occur in cases where SPI send errors out
    if (running_cmd.cmd_type == IDLE && !queueIsEmpty(&command_queue)) {
        dmaTXCompleteCallback();
    }

    command_t cmd;
    // For TCP sockets in ESTABLISHED state, check TX_FSR to send pending data
    for (uint8_t sn = 0; sn < _WIZCHIP_SOCK_NUM_; sn++) {
        // Check if socket is in ESTABLISHED state
        if (sockets[sn].registers.SR == SOCK_ESTABLISHED) {
            // Update TX_FSR, TX_RD and TX_WR
            generateGetRegCmd(&cmd, sn, Sn_TX_FSR(sn), (uint8_t*)sockets[sn].registers.TX_FSR, 6);
            cmd.cmd_type = CHECK_TX_FSR;
            
            uint32_t isrm = taskENTER_CRITICAL_FROM_ISR();
            if (!queuePushBack(&command_queue, cmd)) {
                enqueueFailsInISR++;
            }
            taskEXIT_CRITICAL_FROM_ISR(isrm);
        }
    }

    // Check Interrupts just incase its missed
    generateGetRegCmd(&cmd, 0xFF, W5500_SIR, &common_regs.SIR, 1);
    cmd.cmd_type = READ_SIR;

    uint32_t isrm = taskENTER_CRITICAL_FROM_ISR();
    if (!queuePushBack(&command_queue, cmd)) {
        enqueueFailsInISR++;
    }
    taskEXIT_CRITICAL_FROM_ISR(isrm);
}
