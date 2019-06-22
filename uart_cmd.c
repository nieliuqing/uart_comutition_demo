#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <termios.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <getopt.h>

#include "uart_cmd.h"

/*
 * 安全读写函数
 */
static ssize_t safe_write(int fd, const unsigned char *vptr, size_t n)
{
    size_t  nleft;
    ssize_t nwritten;
    const char *ptr;

    ptr = vptr;
    nleft = n;

    while(nleft > 0)
    {
		if((nwritten = write(fd, ptr, nleft)) <= 0)
        {
            if((nwritten < 0) && (errno == EINTR))
                nwritten = 0;
            else
                return -1;
        }
        nleft -= nwritten;
        ptr   += nwritten;
    }

    return(n);
}


static ssize_t safe_read(int fd, unsigned char *vptr, size_t n)
{
    size_t nleft;
    ssize_t nread;
    char *ptr;

	int i;
    ptr = vptr;
    nleft = n;

    while(nleft > 0)
    {
        if((nread = read(fd, ptr, nleft)) < 0)
        {
            if(errno == EINTR)//被信号中断
                nread = 0;
            else
                return -1;
        } else {
			if(nread == 0)
				break;
		}

        nleft -= nread;
        ptr += nread;
    }

    return (n-nleft);
}

static ssize_t safe_read_uart(int fd, unsigned char *vptr,size_t n)
{
	int cnt = 0;
	int len = 0;
	int nread = 0;
    char *ptr = vptr;

	// Get sync tags
	if (safe_read(fd, ptr, 2) == -1) {
		printf("%s %d uart read error!\n", __func__, __LINE__);
		return -1;
	}

	if ((ptr[0] != 0x55) || (ptr[1] != 0x55)) {
		printf("%s %d uart read error!\n", __func__, __LINE__);
		return -1;
	}	
	nread += 2;

	// Get package len
	ptr += 2;
	if (safe_read(fd, ptr, 2) == -1) {
		printf("%s %d uart read error!\n", __func__, __LINE__);
		return -1;
	}
	nread += 2;

	len = ptr[0] + (ptr[1] << 8);
	
	// Get data
	ptr += 2;
	if ((cnt = safe_read(fd, ptr, (len-2))) == -1) {
		printf("%s %d uart read error!\n", __func__, __LINE__);
		return -1;	
	}

    return (cnt + nread);
}


int uart_open(int fd, const char *pathname)
{
    assert(pathname);

    /*打开串口*/
    fd = open(pathname, O_RDWR | O_NOCTTY | O_NDELAY);
    if(fd == -1)
    {
        perror("Open UART failed!");
        return -1;
    }

    /*清除串口非阻塞标志*/
    if(fcntl(fd, F_SETFL, 0) < 0)
    {
        fprintf(stderr,"fcntl failed!\n");
        return -1;
    }

    return fd;
}

int uart_set(int fd, int nSpeed, int nBits, char nEvent, int nStop) 
{
	struct termios newttys1,oldttys1;

    //printf("UART Speed:%d,bits:%d,Event:%s,stop:%d\n",nSpeed,nBits,nEvent,nStop);
	/*保存原有串口配置*/
	if(tcgetattr(fd, &oldttys1) != 0)
	{
		perror("Setupserial 1");
		return -1;
	}
	bzero(&newttys1, sizeof(newttys1));
	newttys1.c_cflag |= (CLOCAL | CREAD);
	/*CREAD 开启串行数据接收，CLOCAL并打开本地连接模式*/
	newttys1.c_cflag &= ~CSIZE;/*设置数据位*/

	/*数据位选择*/
	switch(nBits)
	{
		case 7:
			newttys1.c_cflag |= CS7;
			break;
		case 8:
			newttys1.c_cflag |= CS8;
			break;
	}
	/*设置奇偶校验位*/
	switch( nEvent )
	{
		case '0': /*奇校验*/
			newttys1.c_cflag |= PARENB;/*开启奇偶校验*/
			newttys1.c_iflag |= (INPCK | ISTRIP);/*INPCK打开输入奇偶校验；ISTRIP去除字符的第八个比特  */
			newttys1.c_cflag |= PARODD;/*启用奇校验(默认为偶校验)*/
		    break;
		case 'E':/*偶校验*/
			newttys1.c_cflag |= PARENB; /*开启奇偶校验  */
			newttys1.c_iflag |= (INPCK | ISTRIP);/*打开输入奇偶校验并去除字符第八个比特*/
			newttys1.c_cflag &= ~PARODD;/*启用偶校验*/
			break;
		case 'N': /*无奇偶校验*/
			newttys1.c_cflag &= ~PARENB;
			break;
	} 
	/*设置波特率*/ 
	switch( nSpeed )
	{
		case 2400: 
			cfsetispeed(&newttys1, B2400);
			cfsetospeed(&newttys1, B2400);
			break;
		case 4800:
			cfsetispeed(&newttys1, B4800);
			cfsetospeed(&newttys1, B4800);
			break;
		case 9600:
			cfsetispeed(&newttys1, B9600);
			cfsetospeed(&newttys1, B9600);
			break;
		case 115200:
			cfsetispeed(&newttys1, B115200);
			cfsetospeed(&newttys1, B115200);
			break; 
		default: 
			cfsetispeed(&newttys1, B115200);
			cfsetospeed(&newttys1, B115200);
			break;
	}
	/*设置停止位*/
	if( nStop == 1)/*设置停止位；若停止位为1，则清除CSTOPB，若停止位为2，则激活CSTOPB*/
	{
		newttys1.c_cflag &= ~CSTOPB;/*默认为一位停止位； */
	}
	else if( nStop == 2)
	{
		newttys1.c_cflag |= CSTOPB;/*CSTOPB表示送两位停止位*/
	}
	/*设置最少字符和等待时间，对于接收字符和等待时间没有特别的要求时*/
	newttys1.c_cc[VTIME] = 0;/*非规范模式读取时的超时时间；*/
	newttys1.c_cc[VMIN] = 1; /*非规范模式读取时的最小字符数*/
	tcflush(fd, TCIFLUSH);/*tcflush清空终端未完成的输入/输出请求及数据；TCIFLUSH表示清空正收到的数据，且不读取出来 */
	/*激活配置使其生效*/
	if((tcsetattr(fd, TCSANOW, &newttys1)) != 0)
	{
		perror("com set error");
		return -1;
	}
	return 0;
}

int uart_read(int fd, unsigned char *r_buf, size_t len, int timeout)
{
    ssize_t cnt = 0;
    fd_set rfds;
    struct timeval time;
	int ret;

    /*将文件描述符加入读描述符集合*/
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    /*设置超时为2s*/
    time.tv_sec = timeout;
    time.tv_usec = 0;

    /*实现串口的多路I/O*/
    ret = select(fd+1, &rfds, NULL, NULL, &time);
    switch(ret)
    {
        case -1:
            fprintf(stderr,"select error!\n");
            return -1;
        case 0:
            fprintf(stderr,"time over!\n");
            return -1;
        default:
            cnt = safe_read_uart(fd, r_buf, len);
            if(cnt == -1)
            {
                fprintf(stderr,"read error!\n");
                return -1;
            }
            return cnt;
    }
}

int uart_write(int fd,const unsigned char *w_buf, size_t len)
{
    ssize_t cnt = 0;

    cnt = safe_write(fd, w_buf, len);
    if(cnt == -1)
    {
        fprintf(stderr,"write error!\n");
        return -1;
    }

    return cnt;
}

int uart_close(int fd)
{
    assert(fd);
    close(fd);

    /*可以在这里做些清理工作*/

    return 0;
}
