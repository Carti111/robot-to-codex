TinyGL from the official SparkBot dependency espressif2022/tinygl 0.4.1~1, commit 7ca5c2622acedc71a44a3a0b0ab581671b575c16. Original MIT license retained. Build-system package-manager hooks were omitted for local vendoring. On ESP32, the documented replaceable memory.c allocator uses PSRAM for CPU-only rendering allocations, preserving internal SRAM for Wi-Fi, TLS, audio and LCD DMA. Native tests retain the original libc allocator.
Archive SHA256 b9175fae1f573377404ce8df1439ea3e43648838cbb864bbbc39fdb0d0615f01.
https://github.com/espressif2022/TinyGL
