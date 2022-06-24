/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"
#include "freertos/event_groups.h"
#include "time.h"
#include "sys/time.h"

#include "driver/gpio.h"

#define LED_PIN        19

#define SPP_TAG "SPP_ACCEPTOR_DEMO"
#define SPP_SERVER_NAME "SPP_SERVER"
#define EXAMPLE_DEVICE_NAME "ESP_SPP_ACCEPTOR"
#define SPP_SHOW_DATA 0
#define SPP_SHOW_SPEED 1
#define SPP_SHOW_MODE SPP_SHOW_DATA    /*Choose show mode: show data or speed*/

static const esp_spp_mode_t esp_spp_mode = ESP_SPP_MODE_CB;

static struct timeval time_new, time_old;
static long data_num = 0;

static const esp_spp_sec_t sec_mask = ESP_SPP_SEC_AUTHENTICATE;
static const esp_spp_role_t role_slave = ESP_SPP_ROLE_SLAVE;

xTaskHandle xTaskHandle_Main = NULL;
void vTask_Main (void *pvParameters);

xTaskHandle xTaskHandle_SendSPP = NULL;
void vTask_SendSPP (void *pvParameters);

xTaskHandle xTaskHandle_ReceiveSPP = NULL;
void vTask_ReceiveSPP (void *pvParameters);

EventGroupHandle_t xEventGroup_StatusSPP = NULL;

const EventBits_t SPP_TX_READY =           0x00000001;
const EventBits_t SPP_RX_READY = 	       0x00000002;

int32_t connection_handle = 0;

char rx_buff[256];

void gpio_initial_config(void);



static char *bda2str(uint8_t *bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

static void print_speed(void)
{
    float time_old_s = time_old.tv_sec + time_old.tv_usec / 1000000.0;
    float time_new_s = time_new.tv_sec + time_new.tv_usec / 1000000.0;
    float time_interval = time_new_s - time_old_s;
    float speed = data_num * 8 / time_interval / 1000.0;
    ESP_LOGI(SPP_TAG, "speed(%fs ~ %fs): %f kbit/s" , time_old_s, time_new_s, speed);
    data_num = 0;
    time_old.tv_sec = time_new.tv_sec;
    time_old.tv_usec = time_new.tv_usec;
}

static void esp_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
	BaseType_t xHigherPriorityTaskWoken, xResult;

	xHigherPriorityTaskWoken = pdFALSE;

	char bda_str[18] = {0};

    switch (event) {
    case ESP_SPP_INIT_EVT:
        if (param->init.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(SPP_TAG, "ESP_SPP_INIT_EVT");
            esp_spp_start_srv(sec_mask, role_slave, 0, SPP_SERVER_NAME);
        } else {
            ESP_LOGE(SPP_TAG, "ESP_SPP_INIT_EVT status:%d", param->init.status);
        }
        break;
    case ESP_SPP_DISCOVERY_COMP_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_DISCOVERY_COMP_EVT");
        break;
    case ESP_SPP_OPEN_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_OPEN_EVT");

        connection_handle = param->start.handle;

		xResult = xEventGroupSetBitsFromISR(xEventGroup_StatusSPP, SPP_TX_READY, &xHigherPriorityTaskWoken);

		if( xResult != pdFAIL )
		{
		  portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
		}

        break;
    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_CLOSE_EVT status:%d handle:%d close_by_remote:%d", param->close.status, param->close.handle, param->close.async);

        xEventGroupClearBitsFromISR(xEventGroup_StatusSPP, SPP_TX_READY);

        break;
    case ESP_SPP_START_EVT:
        if (param->start.status == ESP_SPP_SUCCESS)
        {
            ESP_LOGI(SPP_TAG, "ESP_SPP_START_EVT handle:%d sec_id:%d scn:%d", param->start.handle, param->start.sec_id, param->start.scn);
            esp_bt_dev_set_device_name(EXAMPLE_DEVICE_NAME);
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        }
        else
        {
            ESP_LOGE(SPP_TAG, "ESP_SPP_START_EVT status:%d", param->start.status);
        }
        break;
    case ESP_SPP_CL_INIT_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_CL_INIT_EVT");
        break;
    case ESP_SPP_DATA_IND_EVT:
//#if (SPP_SHOW_MODE == SPP_SHOW_DATA)
        /*
         * We only show the data in which the data length is less than 128 here. If you want to print the data and
         * the data rate is high, it is strongly recommended to process them in other lower priority application task
         * rather than in this callback directly. Since the printing takes too much time, it may stuck the Bluetooth
         * stack and also have a effect on the throughput!
         */

    	if(!(xEventGroupGetBits(xEventGroup_StatusSPP) & SPP_RX_READY))
    	{
    		uint16_t data_len = 0;
    		if(param->data_ind.len < (sizeof(rx_buff) - 1))
    		{
    			data_len = param->data_ind.len;
    		}
    		else
    		{
    			data_len = (sizeof(rx_buff) - 1);
    		}

    		memcpy((uint8_t*)rx_buff, (uint8_t*)param->data_ind.data, data_len);
    		rx_buff[data_len] = 0;
    		xResult = xEventGroupSetBitsFromISR(xEventGroup_StatusSPP, SPP_RX_READY, &xHigherPriorityTaskWoken);

    		if( xResult != pdFAIL )
			{
			  portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
			}
    	}
		/*
        ESP_LOGI(SPP_TAG, "ESP_SPP_DATA_IND_EVT len:%d handle:%d",
                 param->data_ind.len, param->data_ind.handle);
        if (param->data_ind.len < 128) {
            esp_log_buffer_hex("", param->data_ind.data, param->data_ind.len);
        }
#else
        gettimeofday(&time_new, NULL);
        data_num += param->data_ind.len;
        if (time_new.tv_sec - time_old.tv_sec >= 3) {
            print_speed();
        }
#endif*/
        break;
    case ESP_SPP_CONG_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_CONG_EVT");
        if (!param->write.cong)
		{
			xResult = xEventGroupSetBitsFromISR(xEventGroup_StatusSPP, SPP_TX_READY, &xHigherPriorityTaskWoken);

			if( xResult != pdFAIL )
			{
			  portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
			}
		}

        break;
    case ESP_SPP_WRITE_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_WRITE_EVT");
        if (!param->write.cong)
        {
        	xResult = xEventGroupSetBitsFromISR(xEventGroup_StatusSPP, SPP_TX_READY, &xHigherPriorityTaskWoken);

        	if( xResult != pdFAIL )
			{
			  portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
			}
        }
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_SRV_OPEN_EVT status:%d handle:%d, rem_bda:[%s]", param->srv_open.status,
                 param->srv_open.handle, bda2str(param->srv_open.rem_bda, bda_str, sizeof(bda_str)));
        gettimeofday(&time_old, NULL);
        break;
    case ESP_SPP_SRV_STOP_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_SRV_STOP_EVT");

    	xEventGroupClearBitsFromISR(xEventGroup_StatusSPP, SPP_TX_READY);

        break;
    case ESP_SPP_UNINIT_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_UNINIT_EVT");
        break;
    default:
        break;
    }
}

void esp_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    char bda_str[18] = {0};

    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:{
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(SPP_TAG, "authentication success: %s bda:[%s]", param->auth_cmpl.device_name,
                     bda2str(param->auth_cmpl.bda, bda_str, sizeof(bda_str)));
        } else {
            ESP_LOGE(SPP_TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
        }
        break;
    }
    case ESP_BT_GAP_PIN_REQ_EVT:{
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit) {
            ESP_LOGI(SPP_TAG, "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        } else {
            ESP_LOGI(SPP_TAG, "Input pin code: 1234");
            esp_bt_pin_code_t pin_code;
            pin_code[0] = '1';
            pin_code[1] = '2';
            pin_code[2] = '3';
            pin_code[3] = '4';
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;
    }

#if (CONFIG_BT_SSP_ENABLED == true)
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %d", param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%d", param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
#endif

    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode:%d bda:[%s]", param->mode_chg.mode,
                 bda2str(param->mode_chg.bda, bda_str, sizeof(bda_str)));
        break;

    default: {
        ESP_LOGI(SPP_TAG, "event: %d", event);
        break;
    }
    }
    return;
}

void app_main(void)
{
    char bda_str[18] = {0};
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    xEventGroup_StatusSPP = xEventGroupCreate();
	gpio_initial_config();

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s initialize bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s enable bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bt_gap_register_callback(esp_bt_gap_cb)) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s gap register failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_spp_register_callback(esp_spp_cb)) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s spp register failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_spp_init(esp_spp_mode)) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s spp init failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

#if (CONFIG_BT_SSP_ENABLED == true)
    /* Set default parameters for Secure Simple Pairing */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
#endif

    /*
     * Set default parameters for Legacy Pairing
     * Use variable pin, input pin code when pairing
     */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    ESP_LOGI(SPP_TAG, "Own address:[%s]", bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));

    xTaskCreatePinnedToCore(vTask_Main,(char*)"Main", 2048, NULL, tskIDLE_PRIORITY + 1, &xTaskHandle_Main, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(vTask_SendSPP,(char*)"SendSPP", 2048, NULL, tskIDLE_PRIORITY + 1, &xTaskHandle_SendSPP, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(vTask_ReceiveSPP,(char*)"ReceiveSPP", 2048, NULL, tskIDLE_PRIORITY + 1, &xTaskHandle_ReceiveSPP, tskNO_AFFINITY);
}

/*
 *
 */

void vTask_Main (void *pvParameters)
{
	while(1)
	{

		vTaskDelay(100);
	}
}

/*
 *
 */

char string[]="hello";

void vTask_SendSPP (void *pvParameters)
{
	while(1)
	{
		xEventGroupWaitBits( xEventGroup_StatusSPP, SPP_TX_READY, pdFALSE, pdTRUE, portMAX_DELAY);
		xEventGroupClearBits(xEventGroup_StatusSPP, SPP_TX_READY);
		esp_spp_write(connection_handle, sizeof(string) - 1, (uint8_t*)string);
		vTaskDelay(100);
	}
}

/*
 *
 */

void vTask_ReceiveSPP (void *pvParameters)
{
	while(1)
	{
		xEventGroupWaitBits( xEventGroup_StatusSPP, SPP_RX_READY, pdFALSE, pdTRUE, portMAX_DELAY);
		printf("%s\r\n",rx_buff);
		if(strncmp((char*)rx_buff,"ledon",5) == 0)
		{
			gpio_set_level(LED_PIN, 1);
		}
		else if(strncmp((char*)rx_buff,"ledoff",6) == 0)
		{
			gpio_set_level(LED_PIN, 0);
		}

		xEventGroupClearBits(xEventGroup_StatusSPP, SPP_RX_READY);

		vTaskDelay(100);
	}
}

/*
 *
 */

void gpio_initial_config(void)
{
	gpio_config_t io_conf;

	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
	io_conf.pin_bit_mask = (1ULL << LED_PIN);
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;

	gpio_config(&io_conf);

	gpio_set_level(LED_PIN, 0);

}
