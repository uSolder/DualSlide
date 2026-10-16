/**
 * @file spi.h
 * @brief Hardware-independent SPI interface contract.
 *
 * This interface separates an SPI connection into:
 *
 * - SPI_BusTypeDef: the physical shared SPI bus.
 * - SPI_DeviceTypeDef: one device connected to that bus.
 *
 * The target-specific implementation translates target pin and peripheral
 * identifiers into the required register, clock, and GPIO configuration.
 */

#ifndef TARGET_API_SPI_H
#define TARGET_API_SPI_H

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
typedef uint8_t SPI_PinTypeDef;

/**
 * @brief Target-defined SPI peripheral identifier.
 *
 * Peripheral constants are provided by the selected target definitions file.
 * Targets that do not require an explicit peripheral identifier may ignore
 * this value.
 */
typedef uint8_t SPI_PortTypeDef;

/**
 * @brief Value used when an SPI signal is not physically connected.
 */
#define SPI_PIN_UNUSED ((SPI_PinTypeDef)0xFFU)

/**
 * @brief Value used when the target determines the SPI peripheral.
 */
#define SPI_PORT_AUTO ((SPI_PortTypeDef)0xFFU)

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
} SPI_ModeTypeDef;

/**
 * @brief SPI frame transmission order.
 */
typedef enum
{
    SPI_BIT_ORDER_MSB_FIRST = 0,
    SPI_BIT_ORDER_LSB_FIRST
} SPI_BitOrderTypeDef;

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
} SPI_FrameSizeTypeDef;

/**
 * @brief SPI chip-select active polarity.
 */
typedef enum
{
    SPI_CHIP_SELECT_ACTIVE_LOW = 0,
    SPI_CHIP_SELECT_ACTIVE_HIGH
} SPI_ChipSelectPolarityTypeDef;

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
} SPI_ResultTypeDef;

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
    SPI_PinTypeDef MosiPin;
    SPI_PinTypeDef MisoPin;
    SPI_PinTypeDef SclkPin;
    SPI_PortTypeDef Port;
} SPI_BusTypeDef;

/**
 * @brief Definition of one device attached to an SPI bus.
 *
 * Device-specific communication settings are stored separately because
 * multiple devices may share the same physical bus while requiring different
 * clock frequencies, modes, frame sizes, and chip-select pins.
 */
typedef struct
{
    SPI_BusTypeDef *Bus;

    SPI_PinTypeDef ChipSelectPin;
    SPI_ChipSelectPolarityTypeDef ChipSelectPolarity;

    uint32_t FrequencyHz;
    uint32_t TimeoutMilliseconds;

    SPI_ModeTypeDef Mode;
    SPI_BitOrderTypeDef BitOrder;
    SPI_FrameSizeTypeDef FrameSize;
} SPI_DeviceTypeDef;

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize a physical SPI bus.
 *
 * The target implementation validates the selected pins and peripheral,
 * configures the required pin routing, and enables the SPI peripheral.
 *
 * @param Bus SPI bus handle.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_ResultTypeDef SPI_BusInit(SPI_BusTypeDef *Bus);

/**
 * @brief Initialize a device attached to an SPI bus.
 *
 * The associated bus must already be initialized. The target implementation
 * validates the device configuration and configures the chip-select pin as an
 * inactive GPIO output.
 *
 * @param Device SPI device handle.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_ResultTypeDef SPI_DeviceInit(SPI_DeviceTypeDef *Device);

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
 * @param Device SPI device handle.
 * @param Data   Frames to transmit.
 * @param Count  Number of frames to transmit.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_ResultTypeDef SPI_Write(SPI_DeviceTypeDef *Device, const void *Data, size_t Count);

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
 * @param Device SPI device handle.
 * @param Data   Destination frame buffer.
 * @param Count  Number of frames to receive.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_ResultTypeDef SPI_Read(SPI_DeviceTypeDef *Device, void *Data, size_t Count);

/**
 * @brief Simultaneously transmit and receive SPI frames.
 *
 * The driver applies the device configuration, asserts chip select, exchanges
 * the requested frames, and then deasserts chip select.
 *
 * One frame is received for every frame transmitted.
 *
 * @param Device  SPI device handle.
 * @param TxData Frames to transmit.
 * @param RxData Destination for received frames.
 * @param Count   Number of frames to exchange.
 *
 * @return SPI_RESULT_OK on success.
 */
SPI_ResultTypeDef SPI_ReadWrite(SPI_DeviceTypeDef *Device, const void *TxData, void *RxData, size_t Count);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_API_SPI_H */
