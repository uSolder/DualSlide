set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m7)

# CMake's compiler check must not attempt to link a host executable.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

find_program(ARM_NONE_EABI_GCC arm-none-eabi-gcc REQUIRED)
find_program(ARM_NONE_EABI_ASM arm-none-eabi-gcc REQUIRED)

set(CMAKE_C_COMPILER "${ARM_NONE_EABI_GCC}")
set(CMAKE_ASM_COMPILER "${ARM_NONE_EABI_ASM}")

find_program(ARM_NONE_EABI_OBJCOPY arm-none-eabi-objcopy REQUIRED)

set(CMAKE_OBJCOPY "${ARM_NONE_EABI_OBJCOPY}")