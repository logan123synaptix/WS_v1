APP_FILES = ${SYNAPTIX_DIR}/app/app.c \
			${SYNAPTIX_DIR}/app/user/sx_cdc/sx_user_cdc.c \
			${SYNAPTIX_DIR}/app/user/sx_msc/sx_user_msc.c \
			${SYNAPTIX_DIR}/app/user/sx_mqtt/sx_user_mqtt.c \
			${SYNAPTIX_DIR}/app/user/sx_ex_storage/sx_ex_storage.c \
			${SYNAPTIX_DIR}/app/user/at_usb/test_at.c \
			${SYNAPTIX_DIR}/app/user/at_usb/at_command.c \
			${SYNAPTIX_DIR}/app/user/thingsboard/thingsboard_client.c \
			${SYNAPTIX_DIR}/app/user/sx_sleep_manager/sx_sleep_manager.c

BOARD_FILES = ${SYNAPTIX_DIR}/board/sx_board.c

COMPONENT_FILES = \
				  ${SYNAPTIX_DIR}/components/peripherals/flash/sx_flash.c \
				  ${SYNAPTIX_DIR}/components/peripherals/gpio/sx_gpio.c \
				  ${SYNAPTIX_DIR}/components/peripherals/i2c/sx_i2c.c \
				  ${SYNAPTIX_DIR}/components/peripherals/spi/sx_spi.c \
				  ${SYNAPTIX_DIR}/components/peripherals/uart/sx_uart.c \
				  ${SYNAPTIX_DIR}/components/peripherals/timer/sx_timer.c \
				  ${SYNAPTIX_DIR}/components/modules/gps/gps.c \
				  ${SYNAPTIX_DIR}/components/modules/gps/minmea.c \
				  ${SYNAPTIX_DIR}/components/modules/modem/modem.c \
				  ${SYNAPTIX_DIR}/components/modules/a76xx/a7677s.c \
				  ${SYNAPTIX_DIR}/components/peripherals/usb_cdc_tiny/sx_usb_tiny_cdc.c \
				  ${SYNAPTIX_DIR}/components/peripherals/usb_cdc_tiny/usb_descriptor.c \
				  ${SYNAPTIX_DIR}/components/peripherals/usb_msc_tiny/sx_usb_tiny_msc.c \
				  ${SYNAPTIX_DIR}/components/modules/external_flash/sx_W25Q128.c\
				  $(SYNAPTIX_DIR)/components/peripherals/sleep/sx_sleep.c \
				  $(SYNAPTIX_DIR)/components/modules/imu/bno055.c \
				  $(SYNAPTIX_DIR)/components/modules/rtc/sx_ex_rtc.c \
				  $(SYNAPTIX_DIR)/components/modules/sht3x/sht3x.c \
				  $(SYNAPTIX_DIR)/components/modules/sps30/sensirion_common.c \
				  $(SYNAPTIX_DIR)/components/modules/sensirion_shdlc.c \
				  $(SYNAPTIX_DIR)/components/modules/sensirion_streaming_shdlc.c \
				  $(SYNAPTIX_DIR)/components/modules/sensirion_streaming.c \
				  $(SYNAPTIX_DIR)/components/modules/sensirion_uart_hal.c \
				  $(SYNAPTIX_DIR)/components/modules/sps30_uart.c \
				  $(SYNAPTIX_DIR)/components/modules/ze12a/ze12a.c \
				  $(SYNAPTIX_DIR)/components/modules/ads1115/ads1115.c \
				  $(SYNAPTIX_DIR)/components/modules/pump/sx_pump.c

SERVICES_FILES = ${SYNAPTIX_DIR}/services/logger/logger.c \
				 ${SYNAPTIX_DIR}/services/littlefs/lfs.c \
				 ${SYNAPTIX_DIR}/services/littlefs/lfs_util.c \
				 ${SYNAPTIX_DIR}/services/cJSON/cJSON.c \
				 ${SYNAPTIX_DIR}/services/cJSON/cJSON_Utils.c \
				 ${SYNAPTIX_DIR}/services/OS/sx_os.c \
				 ${SYNAPTIX_DIR}/services/filesystem/file_io.c \
				 ${SYNAPTIX_DIR}/services/shell/cli_shell.c \
				 ${SYNAPTIX_DIR}/services/filesystem/sx_fs.c \
				 ${SYNAPTIX_DIR}/services/fatfs/ff.c \
				 ${SYNAPTIX_DIR}/services/fatfs/ffsystem.c \
				 ${SYNAPTIX_DIR}/services/fatfs/ffunicode.c \
				 ${SYNAPTIX_DIR}/services/sx_fatfs/sx_diskio.c \
				 ${SYNAPTIX_DIR}/services/sx_fatfs/sx_fatfs.c \
				 ${SYNAPTIX_DIR}/services/mqtt/sx_mqtt.c \
				 ${SYNAPTIX_DIR}/services/sleep_service/sx_sleep_service.c \
				 ${SYNAPTIX_DIR}/services/read_bat/sx_read_bat.c \

UTILS_FILES = ${SYNAPTIX_DIR}/utils/cqueue/cqueue.c \
			  ${SYNAPTIX_DIR}/utils/mem/sx_mem.c \
			  ${SYNAPTIX_DIR}/utils/delay/sx_delay.c \
			  ${SYNAPTIX_DIR}/utils/filter/sx_filter.c \


SYNAPTIX_SOURCES = $(APP_FILES) \
				   $(BOARD_FILES) \
				   $(SERVICES_FILES) \
				   $(UTILS_FILES) \
				   $(COMPONENT_FILES)
