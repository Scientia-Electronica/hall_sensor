/*
 * task_adc_data.c
 *
 *  Created on: Jun 16, 2025
 *      Author: igor
 */
#include "task_adc_data.h"
#include "adc.h"
#include "cmsis_os.h"
#include <stdio.h>

static uint16_t sensor_adc_data[2];
volatile uint8_t adc_data_received = 0;
extern osMessageQId qadc_dataHandle;

void task_adc_data_entry(void const * argument)
{

	HAL_ADC_Start_DMA(&hadc3, (uint32_t*)sensor_adc_data, sizeof sensor_adc_data / sizeof (uint16_t));
	while(1)
	{
		if(adc_data_received)
		{
			HAL_ADC_Stop_DMA(&hadc3);
			adc_data_received = 0;
			HAL_ADC_Start_DMA(&hadc3, (uint32_t*)sensor_adc_data, sizeof sensor_adc_data / sizeof (uint16_t));
			printf("sensor adc data: %u %u\n", sensor_adc_data[0], sensor_adc_data[1]);
		}
		osDelay(100);
	}
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
	if(hadc == &hadc3)
	{
		adc_data_received = 1;
	}
}
