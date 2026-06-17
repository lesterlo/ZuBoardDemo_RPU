#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <metal/log.h>
#include <metal/version.h>
#include <openamp/open_amp.h>
#include <openamp/version.h>

#include "FreeRTOS.h"
#include "platform_info.h"
#include "task.h"
#include "xgpio.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xstatus.h"
#include "zudemo/rpu_control_protocol.h"

#define LPRINTF(fmt, ...) xil_printf("%s():%u " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LPERROR(fmt, ...) LPRINTF("ERROR: " fmt, ##__VA_ARGS__)

static XGpio led_gpio;
static struct rpmsg_endpoint ctrl_ept;
static TaskHandle_t comm_task;
static TaskHandle_t led_task;

static volatile uint32_t heartbeat_count;
static volatile uint32_t rx_count;
static volatile uint32_t error_count;
static volatile uint8_t led_mode = ZUD_RPU_LED_HEARTBEAT;
static volatile uint8_t led_on;

static void apply_led_state(uint8_t on)
{
	uint32_t value = XGpio_DiscreteRead(&led_gpio, 1);

	if (on)
		value |= ZUDEMO_RPU_LED_MASK;
	else
		value &= ~ZUDEMO_RPU_LED_MASK;

	XGpio_DiscreteWrite(&led_gpio, 1, value);
}

static uint32_t uptime_ms(void)
{
	return (uint32_t)((xTaskGetTickCount() * 1000u) / configTICK_RATE_HZ);
}

static uint32_t set_led_mode(uint8_t mode)
{
	switch (mode) {
	case ZUD_RPU_LED_OFF:
		led_mode = mode;
		led_on = 0;
		apply_led_state(led_on);
		return ZUD_RPU_STATUS_OK;
	case ZUD_RPU_LED_ON:
		led_mode = mode;
		led_on = 1;
		apply_led_state(led_on);
		return ZUD_RPU_STATUS_OK;
	case ZUD_RPU_LED_TOGGLE:
		led_mode = mode;
		led_on = !led_on;
		apply_led_state(led_on);
		return ZUD_RPU_STATUS_OK;
	case ZUD_RPU_LED_HEARTBEAT:
		led_mode = mode;
		return ZUD_RPU_STATUS_OK;
	default:
		return ZUD_RPU_STATUS_BAD_PAYLOAD;
	}
}

static void send_response(struct rpmsg_endpoint *ept,
			  const struct zudemo_rpu_msg_header *request,
			  uint32_t dst,
			  uint8_t type, uint32_t status,
			  const void *payload, uint16_t payload_len)
{
	uint8_t tx[ZUD_RPU_MAX_FRAME_SIZE] = {0};
	struct zudemo_rpu_msg_header header;
	const uint32_t frame_len = sizeof(header) + payload_len;

	header.magic = ZUD_RPU_MAGIC;
	header.version = ZUD_RPU_VERSION;
	header.type = type;
	header.payload_len = payload_len;
	header.sequence = request ? request->sequence : 0;
	header.status = status;

	if (frame_len > sizeof(tx)) {
		error_count++;
		return;
	}

	memcpy(tx, &header, sizeof(header));
	if (payload_len)
		memcpy(tx + sizeof(header), payload, payload_len);

	if (rpmsg_sendto(ept, tx, frame_len, dst) < 0)
		error_count++;
}

static void send_status(struct rpmsg_endpoint *ept,
			const struct zudemo_rpu_msg_header *request,
			uint32_t dst)
{
	struct zudemo_rpu_status_payload payload;

	payload.core_id = ZUDEMO_RPU_CORE_ID;
	payload.led_mode = led_mode;
	payload.led_on = led_on;
	payload.heartbeat_count = heartbeat_count;
	payload.uptime_ms = uptime_ms();
	payload.rx_count = rx_count;
	payload.error_count = error_count;

	send_response(ept, request, dst, ZUD_RPU_MSG_STATUS, ZUD_RPU_STATUS_OK,
		      &payload, sizeof(payload));
}

static int rpmsg_endpoint_cb(struct rpmsg_endpoint *ept, void *data, size_t len,
			     uint32_t src, void *priv)
{
	struct zudemo_rpu_msg_header request;

	(void)priv;
	rx_count++;

	if (len < sizeof(request)) {
		error_count++;
		return RPMSG_SUCCESS;
	}

	memcpy(&request, data, sizeof(request));

	if (request.magic != ZUD_RPU_MAGIC) {
		error_count++;
		send_response(ept, &request, src, ZUD_RPU_MSG_ERROR,
			      ZUD_RPU_STATUS_BAD_MAGIC, NULL, 0);
		return RPMSG_SUCCESS;
	}
	if (request.version != ZUD_RPU_VERSION) {
		error_count++;
		send_response(ept, &request, src, ZUD_RPU_MSG_ERROR,
			      ZUD_RPU_STATUS_BAD_VERSION, NULL, 0);
		return RPMSG_SUCCESS;
	}
	if (request.payload_len + sizeof(request) != len ||
	    len > ZUD_RPU_MAX_FRAME_SIZE) {
		error_count++;
		send_response(ept, &request, src, ZUD_RPU_MSG_ERROR,
			      ZUD_RPU_STATUS_BAD_LENGTH, NULL, 0);
		return RPMSG_SUCCESS;
	}

	switch (request.type) {
	case ZUD_RPU_MSG_PING:
		send_response(ept, &request, src, ZUD_RPU_MSG_PONG,
			      ZUD_RPU_STATUS_OK, NULL, 0);
		break;
	case ZUD_RPU_MSG_GET_STATUS:
		send_status(ept, &request, src);
		break;
	case ZUD_RPU_MSG_SET_LED: {
		struct zudemo_rpu_led_payload payload;
		uint32_t status;

		if (request.payload_len != sizeof(payload)) {
			error_count++;
			send_response(ept, &request, src, ZUD_RPU_MSG_ERROR,
				      ZUD_RPU_STATUS_BAD_PAYLOAD, NULL, 0);
			break;
		}
		memcpy(&payload, (uint8_t *)data + sizeof(request), sizeof(payload));
		status = set_led_mode(payload.mode);
		if (status == ZUD_RPU_STATUS_OK) {
			send_response(ept, &request, src, ZUD_RPU_MSG_ACK,
				      ZUD_RPU_STATUS_OK, NULL, 0);
		} else {
			error_count++;
			send_response(ept, &request, src, ZUD_RPU_MSG_ERROR,
				      status, NULL, 0);
		}
		break;
	}
	default:
		error_count++;
		send_response(ept, &request, src, ZUD_RPU_MSG_ERROR,
			      ZUD_RPU_STATUS_BAD_TYPE, NULL, 0);
		break;
	}

	return RPMSG_SUCCESS;
}

static void rpmsg_service_unbind(struct rpmsg_endpoint *ept)
{
	(void)ept;
	ML_INFO("remote endpoint destroyed\r\n");
}

static int app(struct rpmsg_device *rdev, void *priv)
{
	struct rproc_plat_info arg;
	int ret;

	arg.rpdev = rdev;
	arg.rproc = (struct remoteproc *)priv;

	ML_INFO("Creating RPMsg endpoint %s\r\n", ZUDEMO_RPU_SERVICE_NAME);
	ret = rpmsg_create_ept(&ctrl_ept, rdev, ZUDEMO_RPU_SERVICE_NAME,
			       RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
			       rpmsg_endpoint_cb, rpmsg_service_unbind);
	if (ret) {
		ML_ERR("Failed to create RPMsg endpoint.\r\n");
		return -1;
	}

	ret = platform_poll_on_vdev_reset(&arg);
	rpmsg_destroy_ept(&ctrl_ept);
	return ret;
}

static void openamp_processing(void *unused_arg)
{
	void *platform = NULL;
	struct rpmsg_device *rpdev;
	int ret;

	(void)unused_arg;

	LPRINTF("OpenAMP %s, libmetal %s\r\n", openamp_version(), metal_ver());
	LPRINTF("Starting %s on R5 core %u\r\n",
		ZUDEMO_RPU_SERVICE_NAME, ZUDEMO_RPU_CORE_ID);

	ret = platform_init(0, NULL, &platform);
	if (ret) {
		LPERROR("Failed to initialize OpenAMP platform.\r\n");
		platform_cleanup(platform);
		while (1)
			;
	}

	while (1) {
		rpdev = platform_create_rpmsg_vdev(platform, 0, VIRTIO_DEV_DEVICE,
						   NULL, NULL);
		if (!rpdev) {
			ML_ERR("Failed to create RPMsg virtio device.\r\n");
			platform_cleanup(platform);
			while (1)
				;
		}

		app(rpdev, platform);
		platform_release_rpmsg_vdev(rpdev, platform);
	}
}

static void led_processing(void *unused_arg)
{
	(void)unused_arg;

	while (1) {
		if (led_mode == ZUD_RPU_LED_HEARTBEAT) {
			led_on = !led_on;
			apply_led_state(led_on);
			heartbeat_count++;
			vTaskDelay(pdMS_TO_TICKS(ZUDEMO_RPU_HEARTBEAT_PERIOD_MS));
		} else {
			vTaskDelay(pdMS_TO_TICKS(100));
		}
	}
}

int main(void)
{
	BaseType_t stat;
	int status;

	status = XGpio_Initialize(&led_gpio, XPAR_AXI_GPIO_0_BASEADDR);
	if (status != XST_SUCCESS) {
		xil_printf("GPIO initialization failed\r\n");
		return -1;
	}

	XGpio_SetDataDirection(&led_gpio, 1, 0x00);
	apply_led_state(0);

	stat = xTaskCreate(led_processing, (const char *)"LED", 1024,
			   NULL, 1, &led_task);
	if (stat != pdPASS)
		LPERROR("cannot create LED task\r\n");

	stat = xTaskCreate(openamp_processing, (const char *)"RPMSG", 2048,
			   NULL, 2, &comm_task);
	if (stat != pdPASS)
		LPERROR("cannot create RPMSG task\r\n");

	vTaskStartScheduler();

	while (1)
		;

	return 0;
}
