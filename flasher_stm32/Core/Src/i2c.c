/*
  stm32flash - Open Source ST STM32 flash program for *nix
  Copyright (C) 2014 Antonio Borneo <borneo.antonio@gmail.com>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/


#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "compiler.h"
#include "port.h"
#include "main.h"

extern UART_HandleTypeDef huart1;
extern I2C_HandleTypeDef hi2c1;


struct i2c_priv {
	int fd;
	int addr;
};

static port_err_t i2c_open(struct port_interface *port,
			   struct port_options *ops)
{
	struct i2c_priv *h;
	int  addr;

	/* 2. check options */
	addr = ops->bus_addr;
	if (addr < 0x03 || addr > 0x77) {
		fprintf(stderr, "I2C address out of range [0x03-0x77]\n");
		return PORT_ERR_UNKNOWN;
	}

	/* 3. open it */
	h = calloc(sizeof(*h), 1);
	if (h == NULL) {
		fprintf(stderr, "End of memory\n");
		return PORT_ERR_UNKNOWN;
	}

	h->addr = addr;
	port->private = h;
	return PORT_ERR_OK;
}

static port_err_t i2c_close(struct port_interface *port)
{
	struct i2c_priv *h;

	h = (struct i2c_priv *)port->private;
	if (h == NULL)
		return PORT_ERR_UNKNOWN;
	free(h);
	port->private = NULL;
	return PORT_ERR_OK;
}

static port_err_t i2c_read(struct port_interface *port, void *buf,
			   size_t nbyte)
{
	struct i2c_priv *h;


	h = (struct i2c_priv *)port->private;
	if (h == NULL)
		return PORT_ERR_UNKNOWN;
    
	HAL_I2C_Master_Receive(&hi2c1, (uint16_t)(h->addr<<1), buf, nbyte, HAL_MAX_DELAY);

	return PORT_ERR_OK;
}

static port_err_t i2c_write(struct port_interface *port, void *buf,
			    size_t nbyte)
{
	struct i2c_priv *h;

	h = (struct i2c_priv *)port->private;
	if (h == NULL)
		return PORT_ERR_UNKNOWN;

    HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(h->addr<<1), buf, nbyte, HAL_MAX_DELAY);

	return PORT_ERR_OK;
}

//static port_err_t i2c_gpio(struct port_interface __unused *port,
//			   serial_gpio_t __unused n,
//			   int __unused level)
//{
//	return PORT_ERR_OK;
//}

static const char *i2c_get_cfg_str(struct port_interface *port)
{
	struct i2c_priv *h;
	static char str[11];

	h = (struct i2c_priv *)port->private;
	if (h == NULL)
		return "INVALID";
	snprintf(str, sizeof(str), "addr 0x%2x", h->addr);
	return str;
}

static struct varlen_cmd i2c_cmd_get_reply[] = {
	{0x10, 11},
	{0x11, 17},
	{0x12, 18},
	{ /* sentinel */ }
};

static port_err_t i2c_flush(struct port_interface __unused *port)
{
	/* We shouldn't need to flush I2C */
	return PORT_ERR_OK;
}

struct port_interface port_i2c = {
	.name	= "i2c",
	.flags	= PORT_STRETCH_W | PORT_NPAG_CSUM,
	.open	= i2c_open,
	.close	= i2c_close,
	.flush  = i2c_flush,
	.read	= i2c_read,
	.write	= i2c_write,
	.cmd_get_reply	= i2c_cmd_get_reply,
	.get_cfg_str	= i2c_get_cfg_str,
};


