# atomic_net

'atomic_net' is a full network stack component, including:
    - wifi
    - mqtt
    - homeassistant

## To add to project:

- Add "atomic_net" to CMakeLists.txt REQUIRES or PRIV_REQUIRES
- Add "#include atomic_net.h" to source files
- Call "atomic_net_init()"

## Dependencies

- atomic_bits component
