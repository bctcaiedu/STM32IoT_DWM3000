/**
 * @file    log.h
 * @brief   드라이버 플랫폼 코드가 쓰는 LOG_* 매크로 (STM32)
 *          기본은 USART1(printf 리타게팅)으로 출력. 끄려면 아래 0 으로.
 */
#ifndef DW3000_LOG_H
#define DW3000_LOG_H

#include <stdio.h>

#define DW3000_LOG_ENABLE   1

#if DW3000_LOG_ENABLE
#define LOG_INF(...)  do { printf("[DW INF] " __VA_ARGS__); printf("\r\n"); } while (0)
#define LOG_ERR(...)  do { printf("[DW ERR] " __VA_ARGS__); printf("\r\n"); } while (0)
#define LOG_WRN(...)  do { printf("[DW WRN] " __VA_ARGS__); printf("\r\n"); } while (0)
#define LOG_DBG(...)  do { } while (0)
#else
#define LOG_INF(...)  do { } while (0)
#define LOG_ERR(...)  do { } while (0)
#define LOG_WRN(...)  do { } while (0)
#define LOG_DBG(...)  do { } while (0)
#endif

#endif /* DW3000_LOG_H */
