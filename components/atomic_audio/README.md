# atomic_audio

i2s audio playback component for ESP-IDF 5.5.x+

## CMakeLists.txt

```c
REQUIRES
    atomic_audio
```

## include

```c
#include "atomic_audio.h"
```

## example

```c
atomic_audio_config_t audio_config = {
    .bclk_pin = GPIO_NUM_43,
    .ws_pin = GPIO_NUM_44,
    .dout_pin = GPIO_NUM_16,
};

atomic_audio_init(&audio_config)
atomic_audio_play_wav_file("/sd/laser.wav")
```
