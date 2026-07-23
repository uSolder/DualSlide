*
 * spi.h
 *
 * Hardware-independent SPI interface contract.
 *
 * This file defines the API implemented by each target-specific SPI driver.
 * It must not include MCU-specific headers or expose peripheral registers.
 */

#ifndef TARGET_INTERFACE_SPI_H
#define TARGET_INTERFACE_SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

typedef struct SPI_Bus SPI_Bus;
typedef struct SPI_Device SPI_Device;

/**
 * @brief SPI clock polarity and phase configuration.
 */
typedef enum
{
    SPI_MODE_0 = 0, /* CPOL = 0, CPHA = 0 */
    SPI_MODE_1,     /* CPOL = 0, CPHA = 1 */
    SPI_MODE_2,     /* CPOL = 1, CPHA = 0 */
    SPI_MODE_3      /* CPOL = 1, CPHA = 1 */
} SPI_Mode;

/**
 * @brief SPI bit transmission order.
 */
typedef enum
{
    SPI_BIT_ORDER_MSB_FIRST = 0,
    SPI_BIT_ORDER_LSB_FIRST
} SPI_BitOrder;

/**
 * @brief SPI frame size.
 *
 * The target implementation may support only a subset of these sizes.
 */
typedef enum
{
    SPI_FRAME_SIZE_8_BIT  = 8,
    SPI_FRAME_SIZE_9_BIT  = 9,
    SPI_FRAME_SIZE_16_BIT = 16
} SPI_FrameSize;

/**
 * @brief SPI transfer direction supported by a device.
 */
typedef enum
{
    SPI_DIRECTION_FULL_DUPLEX = 0,
    SPI_DIRECTION_TX_ONLY,
    SPI_DIRECTION_RX_ONLY
} SPI_Direction;

/**
 * @brief SPI chip-select active polarity.
 */
typedef enum
{
    SPI_CHIP_SELECT_ACTIVE_LOW = 0,
    SPI_CHIP_SELECT_ACTIVE_HIGH
} SPI_ChipSelectPolarity;

/**
 * @brief Result returned by SPI operations.
 */
typedef enum
{
    SPI_RESULT_OK = 0,
    SPI_RESULT_INVALID_ARGUMENT,
    SPI_RESULT_NOT_INITIALIZED,
    SPI_RESULT_UNSUPPORTED,
    SPI_RESULT_BUSY,
    SPI_RESULT_TIMEOUT,
    SPI_RESULT_IO_ERROR
} SPI_Result;

/**
 * @brief Completion callback for asynchronous transfers.
 *
 * The callback may execute from interrupt context.
 *
 * @param device  Device associated with the transfer.
 * @param result  Final transfer result.
 * @param context User-provided callback context.
 */
typedef void (*SPI_TransferCallback)(
    SPI_Device *device,
    SPI_Result result,
    void *context);

/**
 * @brief Configuration applied when communicating with one SPI device.
 */
typedef struct
{
    uint32_t frequency_hz;
    uint32_t timeout_ms;

    SPI_Mode mode;
    SPI_BitOrder bit_order;
    SPI_FrameSize frame_size;
    SPI_Direction direction;

    SPI_ChipSelectPolarity chip_select_polarity;

    /**
     * When true, the driver automatically asserts chip select before a
     * transfer and releases it afterward.
     *
     * When false, the caller controls chip select using SPI_Select() and
     * SPI_Deselect().
     */
    bool automatic_chip_select;
} SPI_DeviceConfig;

/**
 * @brief Description of one blocking SPI transfer.
 *
 * The unit represented by count is determined by the configured frame size:
 *
 * - 8-bit frame:  count is the number of uint8_t frames.
 * - 9-bit frame:  count is the number of uint16_t frames.
 * - 16-bit frame: count is the number of uint16_t frames.
 *
 * For full-duplex transfers, tx_data and rx_data may both be non-null.
 * For transmit-only transfers, rx_data may be null.
 * For receive-only transfers, tx_data may be null.
 */
typedef struct
{
    const void *tx_data;
    void *rx_data;
    size_t count;
} SPI_Transfer;

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize an SPI bus.
 *
 * The bus object and its hardware mapping are defined by the target-specific
 * implementation or board layer.
 *
 * @param bus SPI bus to initialize.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_BusInit(SPI_Bus *bus);

/**
 * @brief Deinitialize an SPI bus.
 *
 * @param bus SPI bus to deinitialize.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_BusDeinit(SPI_Bus *bus);

/**
 * @brief Initialize an SPI device attached to an initialized bus.
 *
 * @param device SPI device to initialize.
 * @param config Device communication configuration.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_DeviceInit(
    SPI_Device *device,
    const SPI_DeviceConfig *config);

/**
 * @brief Deinitialize an SPI device.
 *
 * @param device SPI device to deinitialize.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_DeviceDeinit(SPI_Device *device);

/* -------------------------------------------------------------------------- */
/* Bus ownership                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Acquire exclusive ownership of an SPI bus.
 *
 * This function allows several operations to be performed without another
 * device taking control of the shared bus between them.
 *
 * It may map to an RTOS mutex, scheduler lock, interrupt-safe lock, or another
 * target-specific synchronization mechanism.
 *
 * @param device    Device requesting ownership.
 * @param timeout_ms Maximum time to wait for the bus.
 *
 * @return SPI_RESULT_OK when the bus is acquired.
 */
SPI_Result SPI_Acquire(
    SPI_Device *device,
    uint32_t timeout_ms);

/**
 * @brief Release ownership of an SPI bus.
 *
 * @param device Device that owns the bus.
 */
void SPI_Release(SPI_Device *device);

/* -------------------------------------------------------------------------- */
/* Chip select                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Assert the device chip-select signal.
 *
 * This should normally be used only when automatic chip select is disabled.
 *
 * @param device SPI device to select.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_Select(SPI_Device *device);

/**
 * @brief Deassert the device chip-select signal.
 *
 * @param device SPI device to deselect.
 */
void SPI_Deselect(SPI_Device *device);

/* -------------------------------------------------------------------------- */
/* Blocking transfers                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Perform a blocking SPI transfer.
 *
 * When automatic chip select is enabled, chip select is asserted before the
 * transfer and released afterward.
 *
 * @param device   SPI device.
 * @param transfer Transfer description.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_TransferBlocking(
    SPI_Device *device,
    const SPI_Transfer *transfer);

/**
 * @brief Perform a blocking transmit-only operation.
 *
 * @param device SPI device.
 * @param data   Frame buffer.
 * @param count  Number of frames.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_Write(
    SPI_Device *device,
    const void *data,
    size_t count);

/**
 * @brief Perform a blocking receive-only operation.
 *
 * The target driver transmits its configured idle value while receiving when
 * required by the SPI peripheral.
 *
 * @param device SPI device.
 * @param data   Receive frame buffer.
 * @param count  Number of frames.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_Read(
    SPI_Device *device,
    void *data,
    size_t count);

/**
 * @brief Perform a blocking full-duplex operation.
 *
 * @param device SPI device.
 * @param tx_data Transmit frame buffer.
 * @param rx_data Receive frame buffer.
 * @param count   Number of frames.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_Exchange(
    SPI_Device *device,
    const void *tx_data,
    void *rx_data,
    size_t count);

/* -------------------------------------------------------------------------- */
/* Asynchronous transfers                                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Start an asynchronous SPI transfer.
 *
 * The transfer buffers must remain valid until the callback executes or the
 * operation is cancelled.
 *
 * The target may implement this using DMA, interrupts, or another mechanism.
 * Targets without asynchronous SPI support return SPI_RESULT_UNSUPPORTED.
 *
 * @param device   SPI device.
 * @param transfer Transfer description.
 * @param callback Completion callback.
 * @param context  User-provided callback context.
 *
 * @return SPI_RESULT_OK if the transfer was started.
 */
SPI_Result SPI_TransferAsync(
    SPI_Device *device,
    const SPI_Transfer *transfer,
    SPI_TransferCallback callback,
    void *context);

/**
 * @brief Cancel an active asynchronous transfer.
 *
 * @param device SPI device.
 *
 * @return SPI_RESULT_OK if the transfer was cancelled.
 */
SPI_Result SPI_CancelTransfer(SPI_Device *device);

/* -------------------------------------------------------------------------- */
/* Status                                                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Determine whether a device currently has an active transfer.
 *
 * @param device SPI device.
 *
 * @return true when a transfer is active.
 */
bool SPI_IsBusy(const SPI_Device *device);

/**
 * @brief Retrieve the most recent SPI operation result.
 *
 * @param device SPI device.
 *
 * @return Most recent operation result.
 */
SPI_Result SPI_GetLastResult(const SPI_Device *device);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_INTERFACE_SPI_H */