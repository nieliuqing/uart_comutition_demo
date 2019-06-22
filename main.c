#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>

#include "uart_cmd.h"

//#define PRINT_PACKET 1

#define RETRY_TIMES (3)

//#define MCU_UART_BAUD 9600
#define MCU_UART_BAUD 115200

#define FW_VERSION ((19<<24) | (6<<16) | (5<<8) | (4))
#define MCU_FW_PATH "/etc/GP-A081_19060504.bin"

/*
 * frame sync  length  command payload  checksum
 * 2bytes      2bytes  1bytes  Nbytes   2bytes
 *
 */
#define SYNC_TAG 		(0x55)
#define PACKET_PAYLOAD	(256)

#define REQUEST_UPDATE	1  	//A113->MCU
#define SEND_PACKET     2	//A113->MCU
#define STOP_UPDATE		3	//A113->MCU
#define GET_VERSION		4	//A113->MCU


#define UPDATE_SUCCESS	5	//MCU->A113
#define UPDATE_FAIL		6	//MCU->A113
#define PACKET_OK		7	//MCU->A113
#define PACKET_ERROR	8	//MCU->A113
#define START_UPDATE 	9   //MCU->A113
#define RETURN_VERSION 	10   //MCU->A113

static int uart_fd = -1;

static void print_hex(unsigned char *buf, int size)
{
#ifdef PRINT_PACKET
	int i;
	for (i = 0; i < size; i++) {
		if (!(i % 16)) {
			printf("\n");
			printf("%.2x ", buf[i]);
		} else {
			printf("%.2x ", buf[i]);
		}
	}
#endif
}

/*
 * get_file_size
 *
 * return the length of file
 *
 */
static unsigned long get_file_size(const char *path)
{
	struct stat tmp;
	if (stat(path, &tmp) < 0) {
		return -1;
	} else {
		return tmp.st_size;
	}
}

/*
 * calc_check_sum
 *
 * return: sum (unsigned shot)
 *
 */
static inline unsigned short calc_check_sum(unsigned char *buf, int size)
{
	int i;
	unsigned short sum = 0;

	for(i = 0; i < size; i++) {
		sum += buf[i];
	}

	return sum;
}

/*
 *
 */
static int uart_init()
{
	int fd = -1;

	fd = uart_open(fd,"/dev/ttyS2");/*串口号/dev/ttySn*/
	if(fd == -1)
	{
		return -1;
	}

	if(uart_set(fd,MCU_UART_BAUD,8,'N',1) == -1)
	{
		close(fd);
		return -1;
	}

	return fd;
}


static int uart_send(unsigned char *buf, int size)
{
	return uart_write(uart_fd, buf, size);
}

static int uart_receive(unsigned char *buf, int size, int timeout)
{
	int cnt = -1;
	int check_sum = -1;

	unsigned char *ptr;
	unsigned char rcv_buf[32] = {0};

	memset(rcv_buf, 0, sizeof(rcv_buf));

	cnt = uart_read(uart_fd, rcv_buf, size, timeout);
	//cnt = uart_read(uart_fd, rcv_buf, size, 5000);
	if (cnt == -1) {
		return -1;
	}
	
	// when receive the mcu cmd, we need delay 1ms to send cmd to mcu
	usleep(1000);

#ifdef PRINT_PACKET
	printf("\n>>>>>>>>receiver>>>>>>");
	print_hex(rcv_buf, cnt);
	printf("\n");
#endif

	if ((rcv_buf[0] == 0x55) && (rcv_buf[1] == 0x55)) {
		//remove 2bytes sync tag and 2bytes checksum
		check_sum = calc_check_sum(&rcv_buf[2], (cnt-4));
		if ((rcv_buf[cnt-2] == (check_sum & 0xff)) &&
			(rcv_buf[cnt-1] == ((check_sum >> 8) & 0xff))) {
			// remove 2bytes sync tag, 2bytes length, 2bytes checksum
			memcpy(buf, &rcv_buf[4], (cnt-6));
			return (cnt-6);
		}
	}

	return -1;
}

/*
 * fill_sync_header
 * init buf[0],buf[1] 0x55
 *
 */
static inline void fill_sync_header(unsigned char *buf)
{
	buf[0] = SYNC_TAG;
	buf[1] = SYNC_TAG;
}

/*
 * get_fw_version
 * frame sync length GET_VERSION  checksum
 * 2 bytes    2bytes 1byte        2bytes
 */
static bool get_fw_version(unsigned char *version)
{
	int i;
	unsigned short sum = 0;
	unsigned char buf[32] = {0};
	unsigned char rcv_buf[32] = {0};

	memset(buf, 0, sizeof(buf));
	memset(rcv_buf, 0, sizeof(rcv_buf));

	fill_sync_header(buf);

	buf[2] = 0x05;
	buf[3] = 0x00;

	buf[4] = GET_VERSION;

	sum = calc_check_sum(&buf[2], 3);
	buf[5] = (sum & 0xff);
	buf[6] = ((sum >> 8) & 0xff);
	
	#ifdef PRINT_PACKET
	print_hex(buf, 7);
	printf("\n");
	#endif

	for (i = 0; i < RETRY_TIMES; i++) {
		// send get fw version command to mcu
		uart_send(buf, 7);
		if (uart_receive(rcv_buf, sizeof(rcv_buf), 1) != -1) {
			if (rcv_buf[0] == RETURN_VERSION) {
				memcpy(version, &rcv_buf[1], 4);
				return true;
			}
		}
	}

	return false;
}

static bool update_check(void)
{
	unsigned char buf[4] = {0};
	unsigned int ver = 0;

	if (get_fw_version(buf)) {
		printf("MCU: fw version:%.2x %.2x %.2x %.2x\n", buf[0], buf[1], buf[2], buf[3]);
		ver = ((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
		if (ver < FW_VERSION) {
			printf("MCU: detect new muc version!");
			return true;
		}
	}

	printf("MCU: do not neeed update!");

	return false;
}

static bool get_fw_check_sum(FILE *fp, int packets, unsigned int *checksum)
{
	int i,j;
	unsigned int tmp = 0;
	unsigned char buf[PACKET_PAYLOAD] = {0};

	fseek(fp, 0L, SEEK_SET);
	for(i = 0; i < packets; i++) {
		memset(buf, 0, sizeof(buf));
		if (fread(buf, sizeof(unsigned char), PACKET_PAYLOAD, fp) > 0) {
			tmp += calc_check_sum(buf, PACKET_PAYLOAD);
		} else {
			break;
		}
	}
	if (i == packets) {
		*checksum = tmp;
		return true;
	}

	return false;
}

/*
 * request_fw_update
 * frame sync length REQUEST_UPDATE fw size   packet num datasum checksum
 * 2 bytes    2bytes 1byte          2bytes    1bytes     4bytes  2bytes
 */
static bool request_fw_update(unsigned int fw_size, unsigned int packets, unsigned int checksum)
{
	int i;
	unsigned short sum = 0;
	unsigned char buf[32] = {0};
	unsigned char rcv_buf[32] = {0};

	memset(buf, 0, sizeof(buf));

	fill_sync_header(buf);

	buf[2] = 0x0c;
	buf[3] = 0x00;

	buf[4] = REQUEST_UPDATE;

	buf[5] = (fw_size & 0xff);
	buf[6] = ((fw_size >> 8) & 0xff);

	buf[7] = (packets & 0xff);

	buf[8] = (checksum & 0xff);
	buf[9] = ((checksum >> 8) & 0xff);
	buf[10] = ((checksum >> 16) & 0xff);
	buf[11] = ((checksum >> 24) & 0xff);

	sum = calc_check_sum(&buf[2], 10);
	buf[12] = (sum & 0xff);
	buf[13] = ((sum >> 8) & 0xff);

	print_hex(buf, 14);

	for (i = 0; i < RETRY_TIMES; i++) {
		// send update command to mcu
		uart_send(buf, 14);

		// check mcu is ready to update
		memset(rcv_buf, 0, sizeof(rcv_buf));
		if (uart_receive(rcv_buf, sizeof(rcv_buf), 3) != -1) {
			printf("MCU: update status:%.2x\n", rcv_buf[0]);
			if (rcv_buf[0] == START_UPDATE) {
				printf("MCU: start update firmware!\n");
				return true;
			}
		}

	}

	return false;
}

/*
 * stop_fw_update
 * frame sync length STOP_UPDATE  checksum
 * 2 bytes    2bytes 1byte           2bytes
 */
static int stop_fw_update(void)
{
	unsigned short sum = 0;
	unsigned char buf[32] = {0};

	memset(buf, 0, sizeof(buf));

	fill_sync_header(buf);

	buf[2] = 0x05;
	buf[3] = 0x00;

	buf[4] = STOP_UPDATE;

	sum = calc_check_sum(&buf[2], 3);
	buf[5] = (sum & 0xff);
	buf[6] = ((sum >> 8) & 0xff);

	print_hex(buf, 7);

	// send stop update command to mcu
	uart_send(buf, 7);

	return 0;
}

/*
 * creat_packet
 * frame sync length SEND_PACKET packetnum payload   checksum
 * 2 bytes    2bytes 1byte       1byte     256bytes  2bytes
 */
static void create_packet(unsigned char *buf, int packets)
{
	int i;
	unsigned short sum = 0;

	memset(buf, 0, sizeof(buf));

	fill_sync_header(buf);

	buf[2] = 0x06;
	buf[3] = 0x01;

	buf[4] = SEND_PACKET;

	buf[5] = packets + 1; // packet start from 1.

	sum = calc_check_sum(&buf[2], 260);
	buf[262] = (sum & 0xff);
	buf[263] = ((sum >> 8) & 0xff);
#ifdef PRINT_PACKET
	print_hex(buf, 6); //header
	print_hex(&buf[6], 256); //payload
	print_hex(&buf[252], 2); //check sum
	printf("\n");
#endif
}

static bool send_packets(FILE *fp, int packets)
{
	int i,j;
	unsigned char buf[264] = {0};
	unsigned char rcv_buf[32] = {0};

	fseek(fp, 0L, SEEK_SET);
	for(i = 0; i < packets; i++) {
		memset(buf, 0, sizeof(buf));

		// buf[6-261] 256bytes payload
		if (fread(&buf[6], sizeof(unsigned char), PACKET_PAYLOAD, fp) > 0) {
			create_packet(buf, i);
		} else {
			printf("read %s fail!!!\n", MCU_FW_PATH);
			return false;
		}

		for(j = 0; j < RETRY_TIMES; j++) {
			//send packet to mcu
			uart_send(buf, sizeof(buf));

			// check packet is ok
			memset(rcv_buf, 0, sizeof(rcv_buf));
			if (uart_receive(rcv_buf, sizeof(rcv_buf), 3) != -1) {
				if (rcv_buf[0] == PACKET_OK) {
					break;
				} else if (rcv_buf[0] == UPDATE_FAIL) {
					return false;
				}
			}
		}
		if (j == RETRY_TIMES) {
			return false;
		}
	}

	return true;
}

int main(void)
{
	int ret = -1;
	FILE *fp = NULL;

	int packets = 0;
	int fw_size = -1;
	unsigned char rcv_buf[32] = {0};
    unsigned int sum;

	if ((uart_fd = uart_init()) == -1) {
		printf("can't open uart!!!\n");
		return -1;
	}

	ret = update_check();
	if (!ret) {
		return -1;
	}

	if ((fp = fopen(MCU_FW_PATH, "rb")) == NULL) {
		printf("Read %s fail, exit\n", MCU_FW_PATH);
		return -1;
	}

	if ((fw_size = get_file_size(MCU_FW_PATH)) < 0 ) {
		printf("Get file size fail, exit");
		return -1;
	} else {
		packets = fw_size / PACKET_PAYLOAD;
		if (fw_size % packets) {
			packets += 1;
		}
		printf("fw size:0x%.4x(%d), packets:0x%.4x(%d)\n",
				fw_size, fw_size, packets, packets);
	}

	if (!get_fw_check_sum(fp, packets, &sum)) {
		printf("Get file checksum fail, exit");
		return -1;
	}

	if (request_fw_update(fw_size, packets, sum)) {
		if (send_packets(fp, packets)) {
			//get update status
			memset(rcv_buf, 0, sizeof(rcv_buf));
			if (uart_receive(rcv_buf, sizeof(rcv_buf), 6) != -1) {
				if (rcv_buf[0] == UPDATE_SUCCESS) {
					printf("MCU: update success!!!!");
				} else {
					printf("MCU: update fail");
				}
			}
		} else {
			stop_fw_update();
			printf("MCU: update fail");
		}
	}

	fclose(fp);
}
