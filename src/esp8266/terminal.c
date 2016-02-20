/* Serial terminal example
 * UART RX is interrupt driven
 * Implements a simple GPIO terminal for setting and clearing GPIOs
 *
 * This sample code is in the public domain.
 */

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <esp8266.h>
#include <esp/uart.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

#include "espressif/esp_common.h"

#define MAX_ARGC (10)

static void cmd_on(uint32_t argc, char *argv[])
{
    if (argc >= 2) {
        for(int i=1; i<argc; i++) {
            uint8_t gpio_num = atoi(argv[i]);
            gpio_enable(gpio_num, GPIO_OUTPUT);
            gpio_write(gpio_num, true);
            printf("On %d\n", gpio_num);
        }
    } else {
        printf("Error: missing gpio number.\n");
    }
}

static void cmd_off(uint32_t argc, char *argv[])
{
    if (argc >= 2) {
        for(int i=1; i<argc; i++) {
            uint8_t gpio_num = atoi(argv[i]);
            gpio_enable(gpio_num, GPIO_OUTPUT);
            gpio_write(gpio_num, false);
            printf("Off %d\n", gpio_num);
        }
    } else {
        printf("Error: missing gpio number.\n");
    }
}

static void cmd_help(uint32_t argc, char *argv[])
{
    printf("on <gpio number> [ <gpio number>]+     Set gpio to 1\n");
    printf("off <gpio number> [ <gpio number>]+    Set gpio to 0\n");
    printf("sleep                                  Take a nap\n");
    printf("\nExample:\n");
    printf("  on 0<enter> switches on gpio 0\n");
    printf("  on 0 2 4<enter> switches on gpios 0, 2 and 4\n");
    printf("\n");
    printf("chip size %i\n", sdk_flashchip.chip_size);
}

static void cmd_sleep(uint32_t argc, char *argv[])
{
    printf("Type away while I take a 2 second nap (ie. let you test the UART HW FIFO\n");
    printf("Then: %i\n", xTaskGetTickCount());
    vTaskDelay(2000 / portTICK_RATE_MS);
    printf("Now: %i\n", xTaskGetTickCount());
}




static struct sdk_scan_config scan_cfg;

static void sdk_scan_done_cb(void *arg, sdk_scan_status_t status) {
  printf("scan done, status %i\n", status);

  if (status == SCAN_OK) {
    struct sdk_bss_info *bss_link = (struct sdk_bss_info *)arg;
    bss_link = (struct sdk_bss_info *)bss_link->next.stqe_next;//ignore first

    while (bss_link != NULL) {
      printf("ssid:%s  ch%i  rssi:%i\n",
          bss_link->ssid,
          bss_link->channel,
          bss_link->rssi);
      bss_link = (struct sdk_bss_info *)bss_link->next.stqe_next;
    }
  }
}

static void cmd_ap(uint32_t argc, char *argv[])
{
  bool res = sdk_wifi_station_scan(&scan_cfg, sdk_scan_done_cb);
  printf("scan called, res %i\n", res);
}




static void handle_command(char *cmd)
{
    char *argv[MAX_ARGC];
    int argc = 1;
    char *temp, *rover;
    memset((void*) argv, 0, sizeof(argv));
    argv[0] = cmd;
    rover = cmd;
    // Split string "<command> <argument 1> <argument 2>  ...  <argument N>"
    // into argv, argc style
    while(argc < MAX_ARGC && (temp = strstr(rover, " "))) {
        rover = &(temp[1]);
        argv[argc++] = rover;
        *temp = 0;
    }

    if (strlen(argv[0]) > 0) {
        if (strcmp(argv[0], "help") == 0) cmd_help(argc, argv);
        else if (strcmp(argv[0], "on") == 0) cmd_on(argc, argv);
        else if (strcmp(argv[0], "off") == 0) cmd_off(argc, argv);
        else if (strcmp(argv[0], "sleep") == 0) cmd_sleep(argc, argv);
        else if (strcmp(argv[0], "ap") == 0) cmd_ap(argc, argv);
        else printf("Unknown command %s, try 'help'\n", argv[0]);
    }
}

static void uart_task(void *pvParameters)
{
    char ch;
    char cmd[81];
    int i = 0;
    printf("\n\n\nWelcome to gpiomon. Type 'help<enter>' for, well, help\n");
    printf("%% ");
    while(1) {
        if (read(0, (void*)&ch, 1)) { // 0 is stdin
            printf("%c", ch);
            if (ch == '\n' || ch == '\r') {
                cmd[i] = 0;
                i = 0;
                printf("\n");
                handle_command((char*) cmd);
                printf("%% ");
            } else {
                if (i < sizeof(cmd)) cmd[i++] = ch;
            }
        }
    }
}

static void scan_done2(void *arg, sdk_scan_status_t status)

{
    printf("scan done with status %i\n", status);
}

void scan_task(void *pvParameters) {
    printf("About to start scan\n");
    if(sdk_wifi_get_opmode() == SOFTAP_MODE) {
        printf("ap mode can't scan !!!\r\n");
        return;
    }
    while(1) {
      bool s = sdk_wifi_station_scan(NULL, &scan_done2);
      printf("Scan call done, status: %i\n", s);
      vTaskDelay(100000 / portTICK_RATE_MS);
    }
}

void user_init(void)
{
  sdk_wifi_set_opmode(STATION_MODE);
  uart_set_baud(0, 921600);
  //xTaskCreate(&scan_task, (signed char *)"scan_task", 512, NULL, 2, NULL);
  xTaskCreate(&uart_task, (signed char *)"uart_task", 512, NULL, 2, NULL);
}
