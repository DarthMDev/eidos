/*
 * cmake/mbedtls_wasm.h -- Mbed TLS's configuration for the wasm32 build
 * (MBEDTLS_USER_CONFIG_FILE, CMakeLists.txt).
 *
 * WASI has no platform entropy and no sockets Mbed TLS knows: the relay's
 * sockets are sdk/mod_net.c's own, and the random bytes come from WASI's
 * random_get (getentropy), which the host answers from the platform's
 * generator, through mbedtls_hardware_poll (sdk/mod_net.c).
 */
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_PLATFORM_MS_TIME_ALT
