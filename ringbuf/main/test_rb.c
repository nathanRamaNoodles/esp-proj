/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"

static const char *TAG = "rb_test";

static char tx_item[] = "kychu test item.";
static char tx_item_1[] = "kychu test item - 1."; /* 20 Bytes. */
static char tx_item_2[] = "kychu test item - 2.";
static char tx_item_3[] = "kychu test item - 3.";

RingbufHandle_t b_handle[3];
static void queue_set_receiving_task(void *queue_set_handle)
{
	QueueSetHandle_t queue_set = (QueueSetHandle_t)queue_set_handle;

	while(1) {
		//Receive multiple items via queue set
		xQueueSetMemberHandle member = xQueueSelectFromSet(queue_set, pdMS_TO_TICKS(1000));
		for(int i = 0; i < 3; i ++) {
			//Check if member is ring buffer
			if (member != NULL && xRingbufferCanRead(b_handle[i], member) == pdTRUE) {
				//Member is ring buffer, receive item from ring buffer
				size_t item_size;
				char *item = (char *)xRingbufferReceive(b_handle[i], &item_size, 0);

				//Handle item
				printf("buf %d rec: ", i);
				for(int x = 0; x < item_size; x ++) printf("%c", item[x]);
				printf("\n");
				//Return Item
				vRingbufferReturnItem(b_handle[i], (void *)item);
			} else {

			}
		}
	}
	vTaskDelete(NULL);
}

void app_main(void)
{
	ESP_LOGI(TAG, "ring buffer test, RINGBUF_TYPE_NOSPLIT");
	//Create ring buffer;
	RingbufHandle_t buf_handle;
	buf_handle = xRingbufferCreate(1028, RINGBUF_TYPE_NOSPLIT);
	if(buf_handle == NULL) {
		ESP_LOGE(TAG, "Failed to create ring buffer.");
	}

	//Send an item
	UBaseType_t res = xRingbufferSend(buf_handle, tx_item, sizeof(tx_item), pdMS_TO_TICKS(1000));
	if(res != pdTRUE) {
		ESP_LOGE(TAG, "Failed to send item.");
	}

	//Receive on item from no-split ring buffer
	size_t item_size;
	char *item = (char *)xRingbufferReceive(buf_handle, &item_size, pdMS_TO_TICKS(1000));

	//Check received item
	if(item != NULL) {
		//Print item
		for(int i = 0; i < item_size; i ++) {
			printf("%c", item[i]);
		}
		printf("\n");
		//Return Item
		vRingbufferReturnItem(buf_handle, (void *)item);
	} else {
		//Failed to receive item
		ESP_LOGE(TAG, "Failed to receive item.");
	}
	vRingbufferDelete(buf_handle);

	ESP_LOGI(TAG, "ring buffer test, RINGBUF_TYPE_ALLOWSPLIT");
	buf_handle = xRingbufferCreate(80, RINGBUF_TYPE_ALLOWSPLIT); /* 80 bytes. */
	if(buf_handle == NULL) {
		ESP_LOGE(TAG, "Failed to create ring buffer.");
	}
	res = xRingbufferSend(buf_handle, tx_item_1, sizeof(tx_item_1), pdMS_TO_TICKS(1000));
	if(res != pdTRUE) {
		ESP_LOGE(TAG, "Failed to send item 1.");
	}
	res = xRingbufferSend(buf_handle, tx_item_2, sizeof(tx_item_2), pdMS_TO_TICKS(1000));
	if(res != pdTRUE) {
		ESP_LOGE(TAG, "Failed to send item 2.");
	}
	res = xRingbufferSend(buf_handle, tx_item_3, sizeof(tx_item_3), pdMS_TO_TICKS(1000)); /* it will no enough space. */
	if(res != pdTRUE) {
		ESP_LOGE(TAG, "Failed to send item 3.");
	}

	//Receive an item from allow-split ring buffer
	size_t item_size1, item_size2;
	char *item1, *item2;
	BaseType_t ret = xRingbufferReceiveSplit(buf_handle, (void **)&item1, (void **)&item2, &item_size1, &item_size2, pdMS_TO_TICKS(1000));

	//Check received item
	if (ret == pdTRUE && item1 != NULL) {
		for (int i = 0; i < item_size1; i++) {
			printf("%c", item1[i]);
		}
		vRingbufferReturnItem(buf_handle, (void *)item1);
		//Check if item was split
		if (item2 != NULL) {
			for (int i = 0; i < item_size2; i++) {
				printf("%c", item2[i]);
			}
			vRingbufferReturnItem(buf_handle, (void *)item2);
		}
		printf("\n");
	} else {
		//Failed to receive item
		ESP_LOGE(TAG, "Failed to receive item.");
	}
	vRingbufferDelete(buf_handle);

	ESP_LOGI(TAG, "ring buffer test, RINGBUF_TYPE_BYTEBUF");
	buf_handle = xRingbufferCreate(50, RINGBUF_TYPE_BYTEBUF); /* 50 bytes. */
	ESP_LOGI(TAG, "Max Item Size: %d.", xRingbufferGetMaxItemSize(buf_handle));
	ESP_LOGI(TAG, "Current Free Size: %d.", xRingbufferGetCurFreeSize(buf_handle));
	res = xRingbufferSend(buf_handle, tx_item_1, sizeof(tx_item_1), pdMS_TO_TICKS(1000));
	if(res != pdTRUE) {
		ESP_LOGE(TAG, "Failed to send item 1.");
	}
	ESP_LOGI(TAG, "Max Item Size: %d.", xRingbufferGetMaxItemSize(buf_handle));
	ESP_LOGI(TAG, "Current Free Size: %d.", xRingbufferGetCurFreeSize(buf_handle));
	res = xRingbufferSend(buf_handle, tx_item_2, sizeof(tx_item_2), pdMS_TO_TICKS(1000));
	if(res != pdTRUE) {
		ESP_LOGE(TAG, "Failed to send item 2.");
	}
	ESP_LOGI(TAG, "Max Item Size: %d.", xRingbufferGetMaxItemSize(buf_handle));
	ESP_LOGI(TAG, "Current Free Size: %d.", xRingbufferGetCurFreeSize(buf_handle));
	res = xRingbufferSend(buf_handle, tx_item_3, sizeof(tx_item_3), pdMS_TO_TICKS(1000)); /* it will no enough space. */
	if(res != pdTRUE) {
		ESP_LOGE(TAG, "Failed to send item 3.");
	}
	ESP_LOGI(TAG, "Max Item Size: %d.", xRingbufferGetMaxItemSize(buf_handle));
	ESP_LOGI(TAG, "Current Free Size: %d.", xRingbufferGetCurFreeSize(buf_handle));

	//Receive data from byte buffer
	item = (char *)xRingbufferReceiveUpTo(buf_handle, &item_size, pdMS_TO_TICKS(1000), 60);
	ESP_LOGI(TAG, "Max Item Size: %d.", xRingbufferGetMaxItemSize(buf_handle));
	ESP_LOGI(TAG, "Current Free Size: %d.", xRingbufferGetCurFreeSize(buf_handle));

	//Check received data
	if (item != NULL) {
		//Print item
		for (int i = 0; i < item_size; i++) {
			printf("%c", item[i]);
		}
		printf("\n");
		//Return Item
		vRingbufferReturnItem(buf_handle, (void *)item); /* flush data at here. */
	} else {
		//Failed to receive item
		printf("Failed to receive item\n");
	}
	ESP_LOGI(TAG, "Max Item Size: %d.", xRingbufferGetMaxItemSize(buf_handle));
	ESP_LOGI(TAG, "Current Free Size: %d.", xRingbufferGetCurFreeSize(buf_handle));
	vRingbufferDelete(buf_handle);

	ESP_LOGI(TAG, "ring buffer test, Ring Buffers with Queue Sets");
	for(int i = 0; i < 3; i ++) {
		b_handle[i] = xRingbufferCreate(80, RINGBUF_TYPE_NOSPLIT);
		if(b_handle[i] == NULL) {
			ESP_LOGE(TAG, "Failed to create ring buffer.");
		}
	}
	ESP_LOGI(TAG, "buffers create complete.");
	QueueSetHandle_t queue_set = xQueueCreateSet(4);
	for(int i = 0; i < 3; i ++) {
		if(xRingbufferAddToQueueSetRead(b_handle[i], queue_set) != pdTRUE) {
			ESP_LOGE(TAG, "Failed to add queue set, idx: %d.", i);
		}
	}
	ESP_LOGI(TAG, "Add to queue set ok.");
	xTaskCreate(&queue_set_receiving_task, "buf_handler", 2048, (void *)queue_set, 5, NULL); /* need a large stack, 2048 */
	ESP_LOGI(TAG, "add item to buffer.");
	for(int i = 0; i < 5; i ++) {
		for(int j = 0; j < 3; j ++) {
			xRingbufferSend(b_handle[j], tx_item_1, sizeof(tx_item_1), pdMS_TO_TICKS(1000));
		}
	}
}
