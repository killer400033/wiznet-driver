#ifndef W5500_DRIVER_H
#define W5500_DRIVER_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint32_t enqueueFailsInISR;
extern volatile uint32_t commandRetries;
extern volatile uint32_t spiErrCount;

/**
 * @brief WIZNET W5500 interrupt callback
 * @note Call this function from your GPIO EXTI interrupt handler when W5500 INT pin triggers
 * @warning This is an ISR ONLY function. It queues a command to read the interrupt status.
 */
void wiznetInterruptCallback(void);
/**
 * @brief WIZNET W5500 SPI TX/RX complete callback
 * @note Call this function from your SPI interrupt handler when TX/RX is complete
 * @warning This is an ISR ONLY function. It processes the received data and manages the TX queue.
 */
void wiznetSPITxRxCompleteCallback(void);
/**
 * @brief WIZNET W5500 SPI TX complete callback
 * @note Call this function from your SPI interrupt handler when TX is complete
 * @warning This is an ISR ONLY function. It processes the received data and manages the TX queue.
 */
void wiznetSPITxCompleteCallback(void);

/**
 * @brief WIZNET W5500 SPI Error callback (DMA error, overrun, mode fault, CRC error, etc.)
 * @note Call this function from HAL_SPI_ErrorCallback when the W5500 SPI has an error
 * @warning This is an ISR ONLY function. It deselects CS and retries/skips the failed command.
 */
void wiznetSPIErrorCallback(void);

/**
 * @brief WIZNET W5500 SPI Abort complete callback
 * @note Call this function from HAL_SPI_AbortCpltCallback when an abort completes
 * @warning This is an ISR ONLY function. It deselects CS and continues processing.
 */
void wiznetSPIAbortCallback(void);

/**
 * @brief Configure W5500 hardware (SPI and CS pin)
 * @warning Must be called before initWizchip()
 * @param hspi SPI handle for W5500 communication
 * @param cs_port GPIO port for CS pin (e.g., GPIOA)
 * @param cs_pin GPIO pin for CS (e.g., GPIO_PIN_4)
 * @param htim Timer handle for periodic socket polling
 * @return SOCK_OK (0) on success, SOCKERR_INVALID_PARAM if parameters are invalid
 */
int setWiznetHardware(SPI_HandleTypeDef* hspi, GPIO_TypeDef* cs_port, uint16_t cs_pin, TIM_HandleTypeDef* htim);

/**
 * @brief Initialize W5500 chip with network configuration
 * @warning **MUST NOT be called from interrupt context!** Uses blocking operations, osDelay(), and taskENTER_CRITICAL().
 * @warning Must call setWiznetHardware() before calling this function
 * @param ip_address IP address (4 bytes)
 * @param subnet_mask Subnet mask (4 bytes)
 * @param gateway_ip Gateway IP address (4 bytes)
 * @param rx_buf_sizes RX buffer sizes for all 8 sockets (in KB)
 * @param tx_buf_sizes TX buffer sizes for all 8 sockets (in KB)
 * @return SOCK_OK (0) on success, negative error code otherwise
 */
int initWizchip(uint8_t* ip_address, uint8_t* subnet_mask, uint8_t* gateway_ip, 
                const uint8_t* rx_buf_sizes, const uint8_t* tx_buf_sizes);

// Timer callback for periodic socket polling
void Wiznet_Timer_Callback(void);

#ifdef __cplusplus
}
#endif

#endif // W5500_DRIVER_H