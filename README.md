# WIZnet W5500 Driver

Asynchronous, interrupt-driven driver for the [WIZnet W5500](https://www.wiznet.io/product-item/w5500/) hardwired TCP/IP Ethernet controller. Built for **STM32** microcontrollers running **FreeRTOS** (CMSIS-RTOS), using the **STM32 HAL** SPI + DMA interface.

## What It Does

The W5500 is a standalone Ethernet chip with a hardware TCP/IP stack and 8 independent sockets. This driver handles all communication between the STM32 and the W5500 over SPI, and exposes a BSD-style socket API (TCP and UDP) plus higher-level protocol clients on top.

**Socket layer** — `socket`, `connect`, `send`, `recv`, `sendto`, `recvfrom`, `disconnect`, `close`

**Existing Application-layer clients:**

| Client | Description |
|--------|-------------|
| **DNS** | Resolves domain names to IPv4 addresses (A records, CNAME following) |
| **NTP** | Synchronises time from an NTP server (RFC 5905, v4) |
| **WebSocket** | Client-side WebSocket over TCP (binary frames, ping/pong, close handshake) |
| **UDP** | Convenience wrapper for fire-and-forget UDP packet sending |

## How the Driver Works

![Driver Flow Diagram](/img/flow-diagram.png)

All communication between the application layer and the W5500 chip flows through the architecture shown above. The key idea is that the application never talks to SPI directly — everything goes through queues and buffers that are drained asynchronously via DMA, reducing CPU load and abstracting all the complex SPI commands away from the user.

### The Command Queue

The **Command Queue** is the central piece that ties everything together. It is a circular buffer (1000 slots) of command structs, and it serialises all SPI access to the W5500. Only one SPI transaction is ever in-flight at a time (`running_cmd`). All SPI commands are sent using DMA transfers. When a DMA transfer completes, the interrupt callback:

1. Processes the response of the just-completed command (e.g. copies received register data, updates socket state, fires application callbacks).
2. Pops the next command from the front of the command queue.
3. Asserts chip-select and kicks off the next DMA transfer.

This means the SPI bus stays fully utilised without ever blocking an RTOS task.

Commands enter the queue from three sources:

- **RTOS tasks** (the socket API) — push to the *back* at normal priority. Uses `taskENTER_CRITICAL()` for thread safety.
- **W5500 INT pin** (GPIO EXTI) — pushes a register read to the *front* at high priority so interrupt events are handled before queued bulk transfers.
- **Hardware timer ISR** (~50 Hz) — periodically polls each established socket's TX free-space register to trigger pending sends, and re-reads the interrupt register as a safety net for any missed INT edges.

### Interrupt-Driven Event Handling

When the W5500 asserts its INT pin, the driver reads the Socket Interrupt Register (SIR) to determine which sockets have pending events, then reads each flagged socket's full register block. Based on the flags:

| Flag | Action |
|------|--------|
| **RECV** | Calls `receivePendingData()` to DMA transfer incoming data into the Receive Buffer |
| **SENDOK** | Pops the completed segment from the Send Queue, calls `sendPendingData()` for the next |
| **DISCON** | Fires a disconnect callback to the application layer |
| **TIMEOUT** | Clears the Send Queue (TCP) or retries the next segment (UDP), fires a timeout callback to application layer |

After processing, the interrupt flags are cleared by writing them back to the socket's IR register.

### Sending Data

When the application calls `send()` or `sendto()`, the driver:

1. Copies the payload into the socket's **Send Buffer** — a ring buffer in STM32 RAM.
2. Pushes a segment descriptor onto the **Send Queue**, recording where in the buffer the data lives (and for UDP, the destination address/port).
3. Calls `sendPendingData()`, which peeks the front of the Send Queue and enqueues a sequence of SPI commands into the **Command Queue**: write the payload into the W5500's TX memory, update the write pointer, and issue the SEND command.

If a send is already in progress, the new segment simply waits in the Send Queue. When the W5500 finishes transmitting and fires a SENDOK interrupt, the completed segment is popped and `sendPendingData()` is called again to chain the next one.

### Receiving Data

On the receive side, the flow is driven by the W5500 rather than the application:

1. The W5500 receives data and asserts its INT pin.
2. The driver reads the interrupt registers to identify which socket has data, then enqueues SPI commands to DMA the data from the W5500's RX memory into the socket's **Receive Buffer**.
3. A segment descriptor is pushed onto the **Receive Queue** so the application knows what data is available.
4. The application's callback fires, and it can call `recv()` or `recvfrom()` to consume segments from the Receive Queue / Receive Buffer.

### Error Recovery

- **SPI errors** (DMA error, overrun, mode fault): the failed command is re-enqueued to the front of the queue and retried automatically.
- **Timer safety net**: the timer ISR detects if `running_cmd` is idle while the queue is non-empty (possible after SPI error recovery) and restarts processing.

## Project Structure

```
Inc/
  w5500.hpp            Core types, command queue, socket structs
  w5500_driver.h       Public driver API (init, hardware config, ISR callbacks)
  w5500_macros.h       W5500 register map (derived from WIZnet ioLibrary)
  socket.h             Socket API
  queue.hpp            Generic circular queue (C++ template, supports push-front/back)
  dns_client.h         DNS client
  ntp_client.h         NTP client
  udp_client.h         UDP client
  websocket_client.h   WebSocket client

Src/
  w5500.cpp            Core driver — command queue, DMA callbacks, interrupt handling
  socket.cpp           Socket API implementation
  dns_client.c         DNS resolver
  ntp_client.c         NTP time sync
  udp_client.c         UDP sender
  websocket_client.c   WebSocket client (handshake, framing, masking)
```
