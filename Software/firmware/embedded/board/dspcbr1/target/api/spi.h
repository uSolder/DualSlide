/**
 * @file spi.h
 * @brief Hardware-independent SPI interface contract.
 *
 * This interface separates an SPI connection into:
 *
 * - SPI_Bus_Handle: the physical shared SPI bus.
 * - SPI_Device_Handle: one device connected to that bus.
 *
 * The target-specific implementation translates target pin and peripheral
 * identifiers into the required register, clock, and GPIO configuration.
 */

#ifndef TARGET_INTERFACE_SPI_H
#define TARGET_INTERFACE_SPI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Target identifiers                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Target-defined GPIO pin identifier.
 *
 * Pin constants are provided by the selected target definitions file.
 */
typedef uint8_t SPI_Pin;

/**
 * @brief Target-defined SPI peripheral identifier.
 *
 * Peripheral constants are provided by the selected target definitions file.
 * Targets that do not require an explicit peripheral identifier may ignore
 * this value.
 */
typedef uint8_t SPI_Port;

/**
 * @brief Value used when an SPI signal is not physically connected.
 */
#define SPI_PIN_UNUSED ((SPI_Pin)0xFFU)

/**
 * @brief Value used when the target determines the SPI peripheral.
 */
#define SPI_PORT_AUTO ((SPI_Port)0xFFU)

/* -------------------------------------------------------------------------- */
/* Configuration types                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief SPI clock polarity and phase configuration.
 */
typedef enum
{
    SPI_MODE_0 = 0, /**< CPOL = 0, CPHA = 0. */
    SPI_MODE_1,     /**< CPOL = 0, CPHA = 1. */
    SPI_MODE_2,     /**< CPOL = 1, CPHA = 0. */
    SPI_MODE_3      /**< CPOL = 1, CPHA = 1. */
} SPI_Mode;

/**
 * @brief SPI frame transmission order.
 */
typedef enum
{
    SPI_BIT_ORDER_MSB_FIRST = 0,
    SPI_BIT_ORDER_LSB_FIRST
} SPI_BitOrder;

/**
 * @brief Number of bits contained in each SPI frame.
 *
 * A target may support only a subset of these frame sizes.
 */
typedef enum
{
    SPI_FRAME_SIZE_8_BIT  = 8,
    SPI_FRAME_SIZE_9_BIT  = 9,
    SPI_FRAME_SIZE_16_BIT = 16
} SPI_FrameSize;

/**
 * @brief SPI chip-select active polarity.
 */
typedef enum
{
    SPI_CHIP_SELECT_ACTIVE_LOW = 0,
    SPI_CHIP_SELECT_ACTIVE_HIGH
} SPI_ChipSelectPolarity;

/**
 * @brief Result returned by an SPI operation.
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

/* -------------------------------------------------------------------------- */
/* Handles                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Physical definition of an SPI bus.
 *
 * An SPI bus is physically defined by its shared signal pins and, where
 * required by the target, its peripheral identifier.
 *
 * Unused signal pins must be set to SPI_PIN_UNUSED.
 */
typedef struct
{
    SPI_Pin mosi_pin;
    SPI_Pin miso_pin;
    SPI_Pin sclk_pin;
    SPI_Port port;
} SPI_Bus_Handle;

/**
 * @brief Definition of one device attached to an SPI bus.
 *
 * Device-specific communication settings are stored separately because
 * multiple devices may share the same physical bus while requiring different
 * clock frequencies, modes, frame sizes, and chip-select pins.
 */
typedef struct
{
    SPI_Bus_Handle *bus;

    SPI_Pin chip_select_pin;
    SPI_ChipSelectPolarity chip_select_polarity;

    uint32_t frequency_hz;
    uint32_t timeout_ms;

    SPI_Mode mode;
    SPI_BitOrder bit_order;
    SPI_FrameSize frame_size;
} SPI_Device_Handle;

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize a physical SPI bus.
 *
 * The target implementation validates the selected pins and peripheral,
 * configures the required pin routing, and enables the SPI peripheral.
 *
 * @param bus SPI bus handle.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_BusInit(SPI_Bus_Handle *bus);

/**
 * @brief Initialize a device attached to an SPI bus.
 *
 * The associated bus must already be initialized. The target implementation
 * validates the device configuration and configures the chip-select pin as an
 * inactive GPIO output.
 *
 * @param device SPI device handle.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_DeviceInit(SPI_Device_Handle *device);

/* -------------------------------------------------------------------------- */
/* Blocking transfers                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Transmit SPI frames to a device.
 *
 * The driver applies the device configuration, asserts chip select, transmits
 * the requested frames, and then deasserts chip select.
 *
 * Buffer element size depends on the configured frame size:
 *
 * - 8-bit frames use uint8_t elements.
 * - 9-bit frames use uint16_t elements.
 * - 16-bit frames use uint16_t elements.
 *
 * For 9-bit transfers, only the least significant nine bits of each uint16_t
 * element are transmitted.
 *
 * @param device SPI device handle.
 * @param data   Frames to transmit.
 * @param count  Number of frames to transmit.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_Write(SPI_Device_Handle *device, const void *data, size_t count);

/**
 * @brief Receive SPI frames from a device.
 *
 * The driver applies the device configuration, asserts chip select, receives
 * the requested frames, and then deasserts chip select.
 *
 * The target transmits idle frames when clock generation is required.
 *
 * Buffer element size depends on the configured frame size:
 *
 * - 8-bit frames use uint8_t elements.
 * - 9-bit frames use uint16_t elements.
 * - 16-bit frames use uint16_t elements.
 *
 * @param device SPI device handle.
 * @param data   Destination frame buffer.
 * @param count  Number of frames to receive.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_Read(SPI_Device_Handle *device, void *data, size_t count);

/**
 * @brief Simultaneously transmit and receive SPI frames.
 *
 * The driver applies the device configuration, asserts chip select, exchanges
 * the requested frames, and then deasserts chip select.
 *
 * One frame is received for every frame transmitted.
 *
 * @param device  SPI device handle.
 * @param tx_data Frames to transmit.
 * @param rx_data Destination for received frames.
 * @param count   Number of frames to exchange.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_Result SPI_ReadWrite(SPI_Device_Handle *device, const void *tx_data, void *rx_data, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_INTERFACE_SPI_H */