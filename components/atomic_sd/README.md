# Atomic SDSPI Component

'atomic_sdspi' is a minimal component which:
    - initalizes a 4-wire spi bus
    - mounts a FAT SD card

## To add to project:

- Add "atomic_sdspi" to CMakeLists.txt REQUIRES or PRIV_REQUIRES
- Add "#include atomic_sdspi.h" to source files
- Call "atomic_sdspi_init()" to mount card

