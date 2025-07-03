/*
 * task_gui_out.c
 *
 *  Created on: Jun 16, 2025
 *      Author: igor
 */

#include "task_gui_out.h"
#include "ltdc.h"
#include "cmsis_os.h"
#include "fmc.h"
#include "dma2d.h"
#include <stdlib.h>
#include <string.h>

extern osMessageQId qadc_dataHandle;

static FMC_SDRAM_CommandTypeDef command;
static void BSP_SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram, FMC_SDRAM_CommandTypeDef *Command);

#define DIAGRAM_WIDTH           480
#define DIAGRAM_HEIGHT          272
#define DIAGRAM_BUF_SIZE        DIAGRAM_WIDTH
#define COLOR_BACKGROUND        0x3081
#define COLOR_FOREGROUND        0x33EB

#define FB1_ADDR                ((uint32_t)0xC0000000)
#define FB2_ADDR                ((uint32_t)0xC0100000)

static uint32_t fb_front = FB1_ADDR;
static uint32_t fb_back  = FB2_ADDR;

static int16_t diagram_data[DIAGRAM_BUF_SIZE];
static int diagram_head = 0;

extern DMA2D_HandleTypeDef hdma2d;
static void lcd_swap_buffers(void);
static void lcd_clear_fb(uint32_t addr, uint16_t color);

static void dma2d_fill_rect(uint32_t dst_addr, uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    HAL_DMA2D_DeInit(&hdma2d);
    hdma2d.Init.Mode = DMA2D_R2M;
    hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
    hdma2d.Init.OutputOffset = DIAGRAM_WIDTH - w;
    HAL_DMA2D_Init(&hdma2d);

    HAL_DMA2D_Start(&hdma2d, color, dst_addr + 2 * (x + y * DIAGRAM_WIDTH), w, h);
    HAL_DMA2D_PollForTransfer(&hdma2d, HAL_MAX_DELAY);
}


static void lcd_draw_diagram(uint32_t fb)
{
    // Ensure previous DMA2D operation is complete before starting a new one
    while (HAL_DMA2D_GetState(&hdma2d) != HAL_DMA2D_STATE_READY) {
        osDelay(1);  // Delay to avoid tight looping
    }

    // Clear the framebuffer once before starting the diagram drawing
    lcd_clear_fb(fb, COLOR_BACKGROUND);

    int max_val = 1;
    for (int i = 0; i < DIAGRAM_BUF_SIZE; ++i) {
        int abs_val = abs(diagram_data[i]);
        if (abs_val > max_val) max_val = abs_val;
    }

    // Draw the diagram after clearing
    for (int i = 0; i < DIAGRAM_BUF_SIZE; ++i) {
        int idx = (diagram_head + i) % DIAGRAM_BUF_SIZE;
        int val = diagram_data[idx];

        int height = (val * (DIAGRAM_HEIGHT / 2)) / max_val;
        int x = i;
        int y = (height > 0) ? (DIAGRAM_HEIGHT / 2 - height) : (DIAGRAM_HEIGHT / 2);
        int h = abs(height);

        if (x < DIAGRAM_WIDTH && y >= 0 && y + h <= DIAGRAM_HEIGHT) {
            while (HAL_DMA2D_GetState(&hdma2d) != HAL_DMA2D_STATE_READY) {
                osDelay(1);  // Delay to avoid tight looping
            }

            // Draw the diagram using DMA2D
            dma2d_fill_rect(fb, COLOR_FOREGROUND, x, y, 1, h);
        }
    }

    // Ensure DMA2D is ready before swapping buffers
    while (HAL_DMA2D_GetState(&hdma2d) != HAL_DMA2D_STATE_READY) {
        osDelay(1);  // Optional delay to avoid tight looping
    }

    // Swap buffers only after the entire drawing is completed
    lcd_swap_buffers();
}

static void lcd_swap_buffers(void)
{
    // Wait for LTDC layer to be ready
    while (__HAL_LTDC_GET_FLAG(&hltdc, LTDC_FLAG_RR)) {
        osDelay(1);  // Wait for Reload to complete (ensuring no flicker during transition)
    }

    // Ensure DMA2D operation has fully completed before swapping buffers
    while (HAL_DMA2D_GetState(&hdma2d) != HAL_DMA2D_STATE_READY) {
        osDelay(1);  // Ensure the drawing is finished
    }

    // Swap the front and back frame buffers
    uint32_t tmp = fb_front;
    fb_front = fb_back;
    fb_back = tmp;

    // Update the LTDC layer to point to the new front buffer
    __HAL_LTDC_LAYER(&hltdc, 0)->CFBAR = fb_front;  // Set the front buffer for LTDC to display
    __HAL_LTDC_RELOAD_CONFIG(&hltdc);  // Reload LTDC config to apply the new front buffer
}

static void lcd_clear_fb(uint32_t addr, uint16_t color)
{
    // Wait for DMA2D to be ready before clearing the framebuffer
    while (HAL_DMA2D_GetState(&hdma2d) != HAL_DMA2D_STATE_READY) {
        osDelay(1);
    }

    dma2d_fill_rect(addr, color, 0, 0, DIAGRAM_WIDTH, DIAGRAM_HEIGHT);
}
void task_gui_out_entry(void const * argument)
{
    osEvent event;
    uint16_t sensor_adc_data[2];
    int16_t sensor_adc_data_diff;

    BSP_SDRAM_Initialization_Sequence(&hsdram1, &command);
    lcd_clear_fb(fb_front, COLOR_BACKGROUND);
    lcd_clear_fb(fb_back, COLOR_BACKGROUND);
    __HAL_LTDC_LAYER(&hltdc, 0)->CFBAR = fb_front;
    __HAL_LTDC_RELOAD_CONFIG(&hltdc);

    while (1) {
        event = osMessageGet(qadc_dataHandle, 10);
        if (event.status == osEventMessage) {
            sensor_adc_data[0] = (uint16_t)event.value.v;
            sensor_adc_data[1] = (uint16_t)(event.value.v >> 16);
            sensor_adc_data_diff = (int16_t)sensor_adc_data[0] - (int16_t)sensor_adc_data[1];

            diagram_data[diagram_head] = sensor_adc_data_diff;
            diagram_head = (diagram_head + 1) % DIAGRAM_BUF_SIZE;

            lcd_draw_diagram(fb_back);
        }
        osDelay(1);
    }
}

static void BSP_SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram, FMC_SDRAM_CommandTypeDef *Command)
{
    __IO uint32_t tmpmrd =0;
    Command->CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    Command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    Command->AutoRefreshNumber = 1;
    Command->ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);
    osDelay(1);

    Command->CommandMode = FMC_SDRAM_CMD_PALL;
    HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

    Command->CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    Command->AutoRefreshNumber = 8;
    HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

    tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1 |
                      SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
                      SDRAM_MODEREG_CAS_LATENCY_2 |
                      SDRAM_MODEREG_OPERATING_MODE_STANDARD |
                      SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

    Command->CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    Command->ModeRegisterDefinition = tmpmrd;
    HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

    hsdram->Instance->SDRTR |= ((uint32_t)((1292)<< 1));
}
