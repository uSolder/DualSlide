/**
 * @file time.h
 * @brief Monotonic system-time contract.
 *
 * This interface provides a hardware-independent source of elapsed time for
 * timeout handling, scheduling, profiling, and other duration measurements.
 *
 * The implementation may use a processor system timer, peripheral timer,
 * operating-system clock, or another monotonic timing source.
 */

#ifndef TARGET_API_TIME_H
#define TARGET_API_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Public types                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Results returned by time-service operations.
 */
typedef enum
{
    TIME_RESULT_OK = 0,
    TIME_RESULT_NOT_INITIALIZED,
    TIME_RESULT_UNSUPPORTED,
    TIME_RESULT_ERROR
} Time_ResultTypeDef;

/* -------------------------------------------------------------------------- */
/* Public functions                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initializes the monotonic time service.
 *
 * The implementation shall configure or connect to a monotonic timing source.
 * Calling this function more than once shall not reset the elapsed-time count
 * unless required by the underlying platform.
 *
 * @return TIME_RESULT_OK if initialization succeeds.
 * @return TIME_RESULT_UNSUPPORTED if no suitable timing source is available.
 * @return TIME_RESULT_ERROR if initialization fails.
 */
Time_ResultTypeDef Time_Init(void);

/**
 * @brief Returns the elapsed monotonic time in milliseconds.
 *
 * The returned value represents elapsed time from an implementation-defined
 * reference point, normally system startup or time-service initialization.
 * It does not represent calendar time or time of day.
 *
 * The counter wraps naturally at UINT32_MAX. Elapsed durations shall be
 * calculated using unsigned subtraction:
 *
 * @code
 * uint32_t start_time = Time_GetMilliseconds();
 *
 * if ((uint32_t)(Time_GetMilliseconds() - start_time) >= timeout_ms)
 * {
 *     // Timeout occurred.
 * }
 * @endcode
 *
 * @return Monotonically increasing millisecond tick count.
 */
uint32_t Time_GetMilliseconds(void);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_API_TIME_H */
