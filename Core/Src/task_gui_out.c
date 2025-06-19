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
#include <stdlib.h>


extern osMessageQId qadc_dataHandle;

static FMC_SDRAM_CommandTypeDef command;
static void BSP_SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram, FMC_SDRAM_CommandTypeDef *Command);

#define DIAGRAM_ARR_SZ			480
const uint16_t ALMOST_BLACK_COL = 0x3081;
const uint16_t GREEN_COL = 0x33eb;
/**
 * fill background
 */
void lcd_background(uint16_t color)
{
	uint32_t n = hltdc.LayerCfg[0].ImageHeight * hltdc.LayerCfg[0].ImageWidth;
		for (int i = 0; i < n; i++) {
			*(__IO uint16_t*) (hltdc.LayerCfg[0].FBStartAdress + (i * 2)) =
					(uint16_t) color;
		}
}
void lcd_col(uint16_t offset, uint16_t color, uint32_t vert_start, uint32_t vert_end)
{
	uint32_t h = hltdc.LayerCfg[0].ImageHeight;
	uint32_t w = hltdc.LayerCfg[0].ImageWidth;

		for (int i = vert_start; i < vert_end; i++) {
			*(__IO uint16_t*) (hltdc.LayerCfg[0].FBStartAdress + (2*(offset+i * w))) =
					(uint16_t) color;
		}
}

void lcd_diag_item(uint16_t offset, int item, uint16_t color)
{
	uint32_t h = hltdc.LayerCfg[0].ImageHeight;
		uint32_t w = hltdc.LayerCfg[0].ImageWidth;
	int item_sz = abs(item);
	uint32_t vert_start, vert_end;
	if(item<0) {
		vert_start = h / 2;
		vert_end = h / 2 + item_sz;
	} else {
		vert_end = h / 2;
		vert_start = h / 2 - item_sz;
		if(vert_start<0)
			vert_start = 0;
	}
	lcd_col(offset, color, vert_start, vert_end);
}

void lcd_diagram(int offs, int*arr, size_t sz, const uint16_t color)
{

	uint32_t h = hltdc.LayerCfg[0].ImageHeight;
	uint32_t w = hltdc.LayerCfg[0].ImageWidth;
	// find max in arr
	int i, max, min;
	for(max = arr[0], min = arr[0], i=0; i<sz; i++) {
		if(arr[i] > max)
			max = arr[i];
		if(arr[i] < min)
			min  = arr[i];
	}
	static uint8_t firsttime=1;
	static int nmax;
	if(firsttime) {
		nmax = (abs(min) > abs(max))?abs(min):abs(max);
		firsttime = 0;
	}
	for(int i=0; i<sz; i++) {
		int rec_item = (int)h * arr[i] / nmax /2;
		lcd_diag_item(offs+i, rec_item, color);
		lcd_diag_item(offs+1+i, rec_item, color);
	}
}

void task_gui_out_entry(void const * argument)
{
	osEvent event;
	uint16_t sensor_adc_data[2];
	int16_t sensor_adc_data_diff;
	static int diagram_sensor_adc_diffs[DIAGRAM_ARR_SZ];
	// setup SDRAM
	BSP_SDRAM_Initialization_Sequence(&hsdram1, &command);
	lcd_background(ALMOST_BLACK_COL);

	while(1) {
		event = osMessageGet(qadc_dataHandle, 10);
		if(event.status == osEventMessage){
			lcd_background(ALMOST_BLACK_COL);
			sensor_adc_data[0] = (uint16_t)event.value.v;
			sensor_adc_data[1] = (uint16_t)(event.value.v>>16);
			sensor_adc_data_diff = (int16_t)sensor_adc_data[0] - (int16_t)sensor_adc_data[1];
			//printf("sensoradc: %d\n\r", sensor_adc_data_diff);
			for(int i=0; i<DIAGRAM_ARR_SZ - 1; i++) {
				diagram_sensor_adc_diffs[i] = diagram_sensor_adc_diffs[i+1];
			}
			diagram_sensor_adc_diffs[DIAGRAM_ARR_SZ - 1] = sensor_adc_data_diff;
			lcd_diagram(0, diagram_sensor_adc_diffs, DIAGRAM_ARR_SZ, GREEN_COL);
		}
		osDelay(1);
	}
}


/**
  * @brief  Perform the SDRAM external memory initialization sequence
  * @param  hsdram: SDRAM handle
  * @param  Command: Pointer to SDRAM command structure
  * @retval None
  */
static void BSP_SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram, FMC_SDRAM_CommandTypeDef *Command)
{
  __IO uint32_t tmpmrd =0;
  /* Step 3:  Configure a clock configuration enable command */
  Command->CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
  Command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
  Command->AutoRefreshNumber = 1;
  Command->ModeRegisterDefinition = 0;

  /* Send the command */
  HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

  /* Step 4: Insert 100 us minimum delay */
  /* Inserted delay is equal to 1 ms due to systick time base unit (ms) */
  osDelay(1);

  /* Step 5: Configure a PALL (precharge all) command */
  Command->CommandMode = FMC_SDRAM_CMD_PALL;
  Command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
  Command->AutoRefreshNumber = 1;
  Command->ModeRegisterDefinition = 0;

  /* Send the command */
  HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

  /* Step 6 : Configure a Auto-Refresh command */
  Command->CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
  Command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
  Command->AutoRefreshNumber = 8;
  Command->ModeRegisterDefinition = 0;

  /* Send the command */
  HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

  /* Step 7: Program the external memory mode register */
  tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1          |
                     SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL   |
                     SDRAM_MODEREG_CAS_LATENCY_2           |
                     SDRAM_MODEREG_OPERATING_MODE_STANDARD |
                     SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

  Command->CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
  Command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
  Command->AutoRefreshNumber = 1;
  Command->ModeRegisterDefinition = tmpmrd;

  /* Send the command */
  HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

  /* Step 8: Set the refresh rate counter */
  /* (15.62 us x Freq) - 20 */
  /* Set the device refresh counter */
  hsdram->Instance->SDRTR |= ((uint32_t)((1292)<< 1));

}
