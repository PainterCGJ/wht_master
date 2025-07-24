#include "cmsis_os.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "elog.h"
#include "port.h"


// Forward declaration for UDP communication
extern int UDP_SendData(const uint8_t *data, uint16_t len, const char *ip_addr,
                        uint16_t port);

#define FRAME_LEN_MAX 127

/* Default communication configuration. We use here EVK1000's default mode (mode
 * 3). */
static dwt_config_t config = {
    5,               /* Channel number. */
    DWT_PRF_64M,     /* Pulse repetition frequency. */
    DWT_PLEN_128,    /* Preamble length. Used in TX only. */
    DWT_PAC32,       /* Preamble acquisition chunk size. Used in RX only. */
    9,               /* TX preamble code. Used in TX only. */
    9,               /* RX preamble code. Used in RX only. */
    1,               /* 0 to use standard SFD, 1 to use non-standard SFD. */
    DWT_BR_6M8,      /* Data rate. */
    DWT_PHRMODE_STD, /* PHY header mode. */
    (1025 + 64 - 32) /* SFD timeout (preamble length + 1 + SFD length - PAC
                        size). Used in RX only. */
};

static uint8 rx_buffer[FRAME_LEN_MAX];

/* Hold copy of status register state here for reference so that it can be
 * examined at a debug breakpoint. */
static uint32 status_reg = 0;

/* Hold copy of frame length of frame received (if good) so that it can be
 * examined at a debug breakpoint. */
static uint16 frame_len = 0;

static osThreadId_t uwbTaskHandle;

static void uwb_task(void *argument) {
    static const char *TAG = "uwb_task";
    reset_DW1000();
    port_set_dw1000_slowrate();
    if (dwt_initialise(DWT_LOADNONE) == DWT_ERROR) {
        while (1) {
            elog_e(TAG, "dwt_initialise failed");
            osDelay(1000);
        };
    }
    port_set_dw1000_fastrate();

    /* Configure DW1000. */
    dwt_configure(&config);
    elog_i(TAG, "dwt_configure success");

    uint32_t device_id = dwt_readdevid();
    elog_i(TAG, "device_id: %08X", device_id);

    /* Infinite loop */
    for (;;) {
        int i;

        /* TESTING BREAKPOINT LOCATION #1 */

        /* Clear local RX buffer to avoid having leftovers from previous
         * receptions This is not necessary but is included here to aid reading
         * the RX buffer. This is a good place to put a breakpoint. Here (after
         * first time through the loop) the local status register will be set
         * for last event and if a good receive has happened the data buffer
         * will have the data in it, and frame_len will be set to the length of
         * the RX frame. */
        for (i = 0; i < FRAME_LEN_MAX; i++) {
            rx_buffer[i] = 0;
        }

        /* Activate reception immediately. See NOTE 3 below. */
        dwt_rxenable(DWT_START_RX_IMMEDIATE);

        /* Poll until a frame is properly received or an error/timeout occurs.
         * See NOTE 4 below. STATUS register is 5 bytes long but, as the event
         * we are looking at is in the first byte of the register, we can use
         * this simplest API function to access it. */
        while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
                 (SYS_STATUS_RXFCG | SYS_STATUS_ALL_RX_ERR))) {
        };

        if (status_reg & SYS_STATUS_RXFCG) {
            /* A frame has been received, copy it to our local buffer. */
            frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;
            elog_i(TAG, "frame_len: %d", frame_len);
            if (frame_len <= FRAME_LEN_MAX) {
                dwt_readrxdata(rx_buffer, frame_len, 0);

                UDP_SendData(rx_buffer, frame_len, "192.168.0.103", 9000);
            }

            /* Clear good RX frame event in the DW1000 status register. */
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG);
        } else {
            /* Clear RX error events in the DW1000 status register. */
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
            elog_e(TAG, "RX error");
        }

        osDelay(100);
    }
}

void UWB_Task_Init(void) {
    const osThreadAttr_t uwbTask_attributes = {
        .name = "uwbTask",
        .stack_size = 512 * 4,
        .priority = (osPriority_t)osPriorityNormal,
    };
    uwbTaskHandle = osThreadNew(uwb_task, NULL, &uwbTask_attributes);
}
