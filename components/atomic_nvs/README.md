# atomic_nvs

'atomic_nvs' is a component to:
    - initialize nvs memory

## To add to project:

- Add "atomic_nvs" to CMakeLists.txt REQUIRES or PRIV_REQUIRES
- Add "#include atomic_nvs.h" to source files
- Call "atomic_nvs_init()"

## Keys

ASCII strings, unique, max length 15 chars

## Values

- integer types: uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t
- zero-terminated string (up to 4000 bytes including null terminator)
- variable length binary data (blob) up to 508,000 bytes

## Namespaces

ASCII strings, unique, max length 15 chars
Up to 254 namespaces per partition.

## Iterators

nvs_entry_find() creates an opaque handle, which is used in subsequent calls to the nvs_entry_next() and nvs_entry_info() functions.
nvs_entry_next() advances an iterator to the next key-value pair.
nvs_entry_info() returns information about each key-value pair
