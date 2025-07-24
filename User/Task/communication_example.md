# UDP Communication Task Usage Example

The UDP communication task provides a message queue-based interface for sending and receiving UDP data. This allows other tasks to communicate over the network without directly handling socket operations.

## Key Features

1. **Message Queue Based**: Uses FreeRTOS message queues for thread-safe communication
2. **Non-blocking Operations**: Tasks won't block when sending data
3. **Callback Support**: Optional callback function for immediate data processing
4. **Queue Management**: Functions to check queue status and clear queues

## API Functions

### Initialization
```c
#include "udp_task.h"

// Initialize the UDP communication task (call once during startup)
UDP_Task_Init();
```

### Sending Data
```c
// Send data to a specific IP address and port
uint8_t data[] = "Hello World";
int result = UDP_SendData(data, sizeof(data), "192.168.1.100", 8000);

// Return values:
// 0  - Success
// -1 - Parameter error  
// -2 - Invalid IP address
// -3 - Queue full or timeout
```

### Receiving Data
```c
// Method 1: Polling (non-blocking)
rx_msg_t msg;
if (UDP_ReceiveData(&msg, 0) == 0) {
    // Process received data
    // msg.src_addr contains sender information
    // msg.data contains the actual data
    // msg.data_len contains data length
}

// Method 2: Blocking with timeout
if (UDP_ReceiveData(&msg, 1000) == 0) {  // Wait up to 1 second
    // Process received data
}
```

### Callback Method
```c
// Define callback function
void on_udp_data_received(const rx_msg_t *msg) {
    // Process data immediately when received
    // This runs in the UDP task context, keep processing minimal
}

// Register callback
UDP_SetRxCallback(on_udp_data_received);
```

### Queue Management
```c
// Check queue status
int tx_count = UDP_GetTxQueueCount();  // Messages waiting to be sent
int rx_count = UDP_GetRxQueueCount();  // Messages waiting to be read

// Clear queues if needed
UDP_ClearTxQueue();  // Clear pending send messages
UDP_ClearRxQueue();  // Clear pending receive messages
```

## Integration Example with UWB Task

```c
// In uwb_task.c - when UWB data is received
if (status_reg & SYS_STATUS_RXFCG) {
    frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;
    if (frame_len <= FRAME_LEN_MAX) {
        dwt_readrxdata(rx_buffer, frame_len, 0);
        
        // Forward UWB data via UDP to remote monitoring system
        UDP_SendData(rx_buffer, frame_len, "192.168.1.100", 9000);
    }
}
```

## Configuration

- **UDP_SERVER_PORT**: Default port 8000 (can be changed in udp_task.c)
- **TX_QUEUE_SIZE**: 10 messages (sending queue)
- **RX_QUEUE_SIZE**: 10 messages (receiving queue)
- **UDP_BUFFER_SIZE**: 512 bytes maximum per message

## Message Structures

```c
// Received message structure
typedef struct {
    struct sockaddr_in src_addr;   // Source IP address and port
    uint16_t data_len;             // Length of data
    uint8_t data[UDP_BUFFER_SIZE]; // Actual data
} rx_msg_t;

// Callback function type
typedef void (*udp_rx_callback_t)(const rx_msg_t *msg);
```

## Thread Safety

- All API functions are thread-safe
- Multiple tasks can send data simultaneously
- Only one callback function can be registered
- The callback runs in the UDP task context

## Error Handling

- Functions return error codes for parameter validation
- Queue full conditions are handled gracefully
- Network errors are handled internally
- No system crashes on network failures 