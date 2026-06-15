/*
 * Minimal MCUXpresso debug console compatibility for ESP-IDF.
 */
#pragma once

#include <stdio.h>

#define PRINTF printf
#define SCANF scanf
#define PUTCHAR putchar
#define GETCHAR getchar
#define DbgConsole_Printf_NSE printf

