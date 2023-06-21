/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart1;

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include "utils.h"
#include "stm32.h"
#include "port.h"


/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#if defined(__WIN32__) || defined(__CYGWIN__)
#include <windows.h>
#endif

#define VERSION "0.7"
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
/* device globals */
stm32_t		*stm		= NULL;
struct port_interface *port = NULL;

/* settings */
struct port_options port_opts = {
	.device			= NULL,
	.serial_mode		= "8e1",
	.bus_addr		= 0,
	.rx_frame_max		= STM32_MAX_RX_FRAME,
	.tx_frame_max		= STM32_MAX_TX_FRAME,
};

enum actions {
	ACT_NONE,
	ACT_READ,
	ACT_WRITE,
	ACT_WRITE_UNPROTECT,
	ACT_READ_PROTECT,
	ACT_READ_UNPROTECT,
	ACT_ERASE_ONLY,
	ACT_CRC
};

enum actions	action		= ACT_NONE;
int		npages		= 0;
int             spage           = 0;
int             no_erase        = 0;
char		verify		= 0;
int		retry		= 10;
char		exec_flag	= 0;
uint32_t	execute		= 0;
char		init_flag	= 1;
int		use_stdinout	= 0;
char		force_binary	= 0;
FILE		*diag;
char		reset_flag	= 0;
char		*filename;
char		*gpio_seq	= NULL;
uint32_t	start_addr	= 0;
uint32_t	readwrite_len	= 0;


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* functions */

int _write(int fd, char* ptr, int len) {
    HAL_UART_Transmit(&huart1, (uint8_t *) ptr, len, HAL_MAX_DELAY);
    return len;
}
#include <stdarg.h>

int my_fprintf(FILE *stream, const char *format, ...) {
	va_list	listp;
	va_start(listp, format);
	int ret =  vprintf(format, listp);
	va_end(listp);
	return ret;
}

#define fprintf my_fprintf
//int  parse_options(int argc, char *argv[]);
//void show_help(char *name);

static const char *action2str(enum actions act)
{
	switch (act) {
		case ACT_READ:
			return "memory read";
		case ACT_WRITE:
			return "memory write";
		case ACT_WRITE_UNPROTECT:
			return "write unprotect";
		case ACT_READ_PROTECT:
			return "read protect";
		case ACT_READ_UNPROTECT:
			return "read unprotect";
		case ACT_ERASE_ONLY:
			return "flash erase";
		case ACT_CRC:
			return "memory crc";
		default:
			return "";
	};
}

static void err_multi_action(enum actions new)
{
	fprintf(stderr,
		"ERROR: Invalid options !\n"
		"\tCan't execute \"%s\" and \"%s\" at the same time.\n",
		action2str(action), action2str(new));
}

static int is_addr_in_ram(uint32_t addr)
{
	return addr >= stm->dev->ram_start && addr < stm->dev->ram_end;
}

static int is_addr_in_flash(uint32_t addr)
{
	return addr >= stm->dev->fl_start && addr < stm->dev->fl_end;
}

static int is_addr_in_opt_bytes(uint32_t addr)
{
	/* option bytes upper range is inclusive in our device table */
	return addr >= stm->dev->opt_start && addr <= stm->dev->opt_end;
}

static int is_addr_in_sysmem(uint32_t addr)
{
	return addr >= stm->dev->mem_start && addr < stm->dev->mem_end;
}

/* returns the page that contains address "addr" */
static int flash_addr_to_page_floor(uint32_t addr)
{
	int page;
	uint32_t *psize;

	if (!is_addr_in_flash(addr))
		return 0;

	page = 0;
	addr -= stm->dev->fl_start;
	psize = stm->dev->fl_ps;

	while (addr >= psize[0]) {
		addr -= psize[0];
		page++;
		if (psize[1])
			psize++;
	}

	return page;
}

/* returns the first page whose start addr is >= "addr" */
int flash_addr_to_page_ceil(uint32_t addr)
{
	int page;
	uint32_t *psize;

	if (!(addr >= stm->dev->fl_start && addr <= stm->dev->fl_end))
		return 0;

	page = 0;
	addr -= stm->dev->fl_start;
	psize = stm->dev->fl_ps;

	while (addr >= psize[0]) {
		addr -= psize[0];
		page++;
		if (psize[1])
			psize++;
	}

	return addr ? page + 1 : page;
}

/* returns the lower address of flash page "page" */
static uint32_t flash_page_to_addr(int page)
{
	int i;
	uint32_t addr, *psize;

	addr = stm->dev->fl_start;
	psize = stm->dev->fl_ps;

	for (i = 0; i < page; i++) {
		addr += psize[0];
		if (psize[1])
			psize++;
	}

	return addr;
}


uint8_t Buffer[25] = {0};
uint8_t Space[] = " - ";
uint8_t StartMSG[] = "Starting I2C Scanning: \n";
uint8_t EndMSG[] = "Done! \n\n";

unsigned long int scan_i2c_address() {

	uint8_t i=0, ret;

    /*-[ I2C Bus Scanning ]-*/
    fprintf(stdout, "%s", StartMSG);
    for(i=1; i<128; i++)
    {
        ret = HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(i<<1), 3, 5);
        if (ret != HAL_OK) /* No ACK Received At That Address */
        {
      	  fprintf(stdout, "%s", Space);
        }
        else if(ret == HAL_OK)
        {
            sprintf(Buffer, "0x%X", i);
            fprintf(stdout, "%s\n", Buffer);
			return i; // kostyl for now
        }

        if (i % 16 == 0) {
        	fprintf(stdout, "\n");
        }
    }
    fprintf(stdout, "%s", EndMSG);
    return 0;
    /*--[ Scanning Done ]--*/
}

uint8_t Rx_data[4+2];



/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  printf("TEST1\n");

  while (1) {
    HAL_Delay(1000);


	int ret = 1;
	stm32_err_t s_err;
	diag = stdout;
	printf("waiting for command\n");

	while(HAL_UART_Receive(&huart1, Rx_data, 1, 100) == HAL_TIMEOUT);

   //! TODO: For now program won't start on its own - you should send 1 to uart
   //! TODO: to start scan address then stm32f401 will exit bootloader and it should be reentered
   //! TODO: to continue you can enter 2
   if (Rx_data[0] == '1') {
		////////////// changed options ///////////
		port_opts.bus_addr = scan_i2c_address(); // address of i2c slave

		if (port_opts.bus_addr==0) {
			printf("Wrong address!\n");
			continue;
		}

   }

  if (Rx_data[0] == '2') {
		action =  ACT_WRITE;

	  //! TODO: execute program after flashing - can be changed
		exec_flag = 1;
		execute  = 0x0;     // strtoul(optarg, NULL, 0);
		if (execute % 4 != 0) {
			fprintf(stderr, "ERROR: Execution address must be word-aligned\n");
			return 1;
		}

		port_opts.device = "/dev/i2c-8"; // dummy else it can break

		//! TODO: Rx Tx max length (used in i2c as read/write length respectively)
		//! TODO: this is working for stm32f401 but can be different for other board
		port_opts.rx_frame_max = 255; // not default 256
		port_opts.tx_frame_max = 128; // not default 256


		// not really needed but leaving just in case
		if (port_opts.rx_frame_max < 0
			|| port_opts.tx_frame_max < 0) {
			fprintf(stderr, "ERROR: Invalid negative value for option -F\n");
			return 1;
		}
		if (port_opts.rx_frame_max == 0)
			port_opts.rx_frame_max = STM32_MAX_RX_FRAME;
		if (port_opts.tx_frame_max == 0)
			port_opts.tx_frame_max = STM32_MAX_TX_FRAME;
		if (port_opts.rx_frame_max < 20
			|| port_opts.tx_frame_max < 6) {
			fprintf(stderr, "ERROR: current code cannot work with small frames.\n");
			fprintf(stderr, "min(RX) = 20, min(TX) = 6\n");
			return 1;
		}
		if (port_opts.rx_frame_max > STM32_MAX_RX_FRAME) {
			fprintf(stderr, "WARNING: Ignore RX length in option -F\n");
			port_opts.rx_frame_max = STM32_MAX_RX_FRAME;
		}
		if (port_opts.tx_frame_max > STM32_MAX_TX_FRAME) {
			fprintf(stderr, "WARNING: Ignore TX length in option -F\n");
			port_opts.tx_frame_max = STM32_MAX_TX_FRAME;
		}

		////////////// changed options ///////////


		if (action == ACT_READ && use_stdinout) {
			diag = stderr;
		}

		// output info about board to be flashed

		fprintf(diag, "stm32flash " VERSION "\n\n");
		fprintf(diag, "http://stm32flash.sourceforge.net/\n\n");


		 if (port_open(&port_opts, &port) != PORT_ERR_OK) {
			fprintf(stderr, "Failed to open port: %s\n", port_opts.device);
			goto close;
		 }

		fprintf(diag, "Interface %s: %s\n", port->name, port->get_cfg_str(port));

		port->flush(port);

		stm = stm32_init(port, init_flag);
		if (!stm)
			goto close;

		fprintf(diag, "Version      : 0x%02x\n", stm->bl_version);
		if (port->flags & PORT_GVR_ETX) {
			fprintf(diag, "Option 1     : 0x%02x\n", stm->option1);
			fprintf(diag, "Option 2     : 0x%02x\n", stm->option2);
		}
		fprintf(diag, "Device ID    : 0x%04x (%s)\n", stm->pid, stm->dev->name);
		fprintf(diag, "- RAM        : Up to %dKiB  (%db reserved by bootloader)\n", (stm->dev->ram_end - 0x20000000) / 1024, stm->dev->ram_start - 0x20000000);
		fprintf(diag, "- Flash      : Up to %dKiB (size first sector: %dx%d)\n", (stm->dev->fl_end - stm->dev->fl_start ) / 1024, stm->dev->fl_pps, stm->dev->fl_ps[0]);
		fprintf(diag, "- Option RAM : %db\n", stm->dev->opt_end - stm->dev->opt_start + 1);
		fprintf(diag, "- System RAM : %dKiB\n", (stm->dev->mem_end - stm->dev->mem_start) / 1024);

		uint8_t		buffer[256];
		uint32_t	addr, start, end;
		unsigned int	len;
		int		failed = 0;
		int		first_page, num_pages;

		/*
		 * Cleanup addresses:
		 *
		 * Starting from options
		 *	start_addr, readwrite_len, spage, npages
		 * and using device memory size, compute
		 *	start, end, first_page, num_pages
		 */


		if (start_addr || readwrite_len) {
			if (start_addr == 0)
				/* default */
				start = stm->dev->fl_start;
			else if (start_addr == 1)
				/* if specified to be 0 by user */
				start = 0;
			else
				start = start_addr;

			if (is_addr_in_flash(start))
				end = stm->dev->fl_end;
			else {
				no_erase = 1;
				if (is_addr_in_ram(start))
					end = stm->dev->ram_end;
				else if (is_addr_in_opt_bytes(start))
					end = stm->dev->opt_end + 1;
				else if (is_addr_in_sysmem(start))
					end = stm->dev->mem_end;
				else {
					/* Unknown territory */
					if (readwrite_len)
						end = start + readwrite_len;
					else
						end = start + sizeof(uint32_t);
				}
			}

			if (readwrite_len && (end > start + readwrite_len))
				end = start + readwrite_len;

			first_page = flash_addr_to_page_floor(start);
			if (!first_page && end == stm->dev->fl_end)
				num_pages = STM32_MASS_ERASE;
			else
				num_pages = flash_addr_to_page_ceil(end) - first_page;
		} else if (!spage && !npages) {
			start = stm->dev->fl_start;
			end = stm->dev->fl_end;
			first_page = 0;
			num_pages = STM32_MASS_ERASE;
		} else {
			first_page = spage;
			start = flash_page_to_addr(first_page);
			if (start > stm->dev->fl_end) {
				fprintf(stderr, "Address range exceeds flash size.\n");
				goto close;
			}

			if (npages) {
				num_pages = npages;
				end = flash_page_to_addr(first_page + num_pages);
				if (end > stm->dev->fl_end)
					end = stm->dev->fl_end;
			} else {
				end = stm->dev->fl_end;
				num_pages = flash_addr_to_page_ceil(end) - first_page;
			}

			if (!first_page && end == stm->dev->fl_end)
				num_pages = STM32_MASS_ERASE;
		}


		    // start writing to memory

			fprintf(diag, "Write to memory\n");

			unsigned int offset = 0;
			unsigned int r;
			unsigned int size;
			unsigned int max_wlen, max_rlen;

			max_wlen = port_opts.tx_frame_max - 2;	/* skip len and crc */
			max_wlen &= ~3;	/* 32 bit aligned */

			max_rlen = port_opts.rx_frame_max;
			max_rlen = max_rlen < max_wlen ? max_rlen : max_wlen;

			/* Assume data from stdin is whole device */
			if (use_stdinout)
				size = end - start;
			else {
				printf("Receive size:\n");
				while(HAL_UART_Receive(&huart1, Rx_data, 4, 1000) == HAL_TIMEOUT);

				size = strtoul(Rx_data, NULL, 0);
				printf("Size received: %d\n", size);

			}

			// TODO: It is possible to write to non-page boundaries, by reading out flash
			//       from partial pages and combining with the input data
			// if ((start % stm->dev->fl_ps[i]) != 0 || (end % stm->dev->fl_ps[i]) != 0) {
			//	fprintf(stderr, "Specified start & length are invalid (must be page aligned)\n");
			//	goto close;
			// }

			// TODO: If writes are not page aligned, we should probably read out existing flash
			//       contents first, so it can be preserved and combined with new data

			// erase memory before flushing
			if (!no_erase && num_pages) {
				fprintf(diag, "Erasing memory\n");
				s_err = stm32_erase_memory(stm, first_page, num_pages);
				if (s_err != STM32_ERR_OK) {
					fprintf(stderr, "Failed to erase memory\n");
					goto close;
				}
			}

			fflush(diag);

			addr = start;
			while(addr < end && offset < size) {
				uint32_t left	= end - addr;
				len		= max_wlen > left ? left : max_wlen;
				len		= len > size - offset ? size - offset : len;
				unsigned int reqlen = len ;


	          // receive len number of bytes from binary file
				printf("Ready to receive portion of data\n");
				while(HAL_UART_Receive(&huart1, buffer, len, HAL_MAX_DELAY) == HAL_TIMEOUT);
				printf("Received portion of data\n");

				if (len == 0) {
					if (use_stdinout) {
						break;
					} else {
						fprintf(stderr, "Failed to read input file\n");
						goto close;
					}
				}

				again:
				s_err = stm32_write_memory(stm, addr, buffer, len);
				if (s_err != STM32_ERR_OK) {
					fprintf(stderr, "Failed to write memory at address 0x%08x\n", addr);
					goto close;
				}

				if (verify) {
					uint8_t compare[len];
					unsigned int offset, rlen;

					offset = 0;
					while (offset < len) {
						rlen = len - offset;
						rlen = rlen < max_rlen ? rlen : max_rlen;
						s_err = stm32_read_memory(stm, addr + offset, compare + offset, rlen);
						if (s_err != STM32_ERR_OK) {
							fprintf(stderr, "Failed to read memory at address 0x%08x\n", addr + offset);
							goto close;
						}
						offset += rlen;
					}

					for(r = 0; r < len; ++r)
						if (buffer[r] != compare[r]) {
							if (failed == retry) {
								fprintf(stderr, "Failed to verify at address 0x%08x, expected 0x%02x and found 0x%02x\n",
									(uint32_t)(addr + r),
									buffer [r],
									compare[r]
								);
								goto close;
							}
							++failed;
							goto again;
						}

					failed = 0;
				}

				addr	+= len;
				offset	+= len;

				fprintf(diag,
					"\rWrote %saddress 0x%08x\n",
					verify ? "and verified " : "",
					addr
				);
				fflush(diag);

				if( len < reqlen)	/* Last read already reached EOF */
					break ;
			}

			fprintf(diag,	"Done.\n");
			ret = 0;
			goto close;


	close:

	    // start execution of flashed code
		if (stm && exec_flag && ret == 0) {
			if (execute == 0)
				execute = stm->dev->fl_start;

			fprintf(diag, "\nStarting execution at address 0x%08x... ", execute);
			fflush(diag);
			if (stm32_go(stm, execute) == STM32_ERR_OK) {
				reset_flag = 0;
				fprintf(diag, "done.\n");
			} else
				fprintf(diag, "failed.\n");
		}

		if (stm   ) stm32_close  (stm);
		if (port)
			port->close(port);

		fprintf(diag, "\n");
		return ret;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
//  while (1)
//  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
//  }
  }

  }

  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
