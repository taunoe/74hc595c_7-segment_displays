/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2023/12/25
 * Description        : Main program body.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*
 *@Note
 *Multiprocessor communication mode routine:
 *Master:USART1_Tx(PD5)\USART1_Rx(PD6).
 *This routine demonstrates that USART1 receives the data sent by CH341 and inverts
 *it and sends it (baud rate 115200).
 *
 *Hardware connection:PD5 -- Rx
 *                    PD6 -- Tx
 // MOSI = PC6, SCK = PC5
 *
 */

#include "debug.h"
#include <stdio.h>

// Software shiftout
#define SER_PIN    GPIO_Pin_6   // PC6 - Data
#define SRCLK_PIN  GPIO_Pin_5   // PC5 - Clock
#define RCLK_PIN   GPIO_Pin_3   // PC3 - Latch
#define SR_PORT    GPIOC

// Hardware SPI shiftout
// Master Out Slave In - MOSI = PC6
// Serial Clock        - SCK = PC5
#define LATCH_PIN  RCLK_PIN
#define LATCH_PORT GPIOC

#define NR1    0b00000001
#define NR2    0b00000010
#define NR3    0b00000100
#define NR4    0b00001000
#define NR12   0b00000011
#define NR123  0b00000111
#define NR1234 0b00001111

#define NUM_OF_DIGITS 4
const uint8_t digits[NUM_OF_DIGITS] = {NR1, NR2, NR3, NR4};

#define NUM_OF_NUMBRID 10
const uint8_t numbrid[NUM_OF_NUMBRID] = {
    0b00111111, // 0
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111, // 9
    };

uint8_t output[NUM_OF_DIGITS] = {
  numbrid[1],
  numbrid[2],
  numbrid[3],
  numbrid[4],
};

int paus = 100; // us - microseconds


// SystemCoreClock is typically 48000000 (48MHz)
// We pre-calculate how many ticks happen per microsecond
volatile uint32_t ticks_per_us = 0;

void SysTick_FreeRunning_Init(void) {
    ticks_per_us = SystemCoreClock / 1000000; // 48 ticks per us at 48MHz

    SysTick->CTLR = 0;          // Disable counter during config
    SysTick->CNT = 0;           // Reset the 64-bit counter
    SysTick->CMP = 0xFFFFFFFF;  // Set compare to max so it doesn't trigger unexpectedly
    
    // CTLR Bits: 
    // Bit 0 = Enable Counter
    // Bit 1 = Disable Interrupt (0) -> We don't want interrupts crushing the CPU!
    // Bit 2 = Clock Source (1 = HCLK, 0 = HCLK/8)
    SysTick->CTLR = 0x05;       // Enable counter, Use HCLK, No Interrupt
}

// Returns the number of microseconds elapsed since boot
uint32_t micros(void) {
    // Read the lower 32 bits of the 64-bit hardware counter
    // At 48MHz, a 32-bit counter overflows every ~89 seconds, 
    // which is plenty for non-blocking delta-time checks.
    return (uint32_t)(SysTick->CNT / ticks_per_us);
}


// Software SPI
void SR_Init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_InitTypeDef gpio = {
        .GPIO_Pin   = SER_PIN | SRCLK_PIN | RCLK_PIN,
        .GPIO_Mode  = GPIO_Mode_Out_PP,
        .GPIO_Speed = GPIO_Speed_50MHz,
    };
    GPIO_Init(SR_PORT, &gpio);
}

// Software SPI
void SR_Send(uint8_t data) {
    // Shift out MSB first
    for (int i = 7; i >= 0; i--) {
        // Set data pin
        if (data & (1 << i))
            GPIO_SetBits(SR_PORT, SER_PIN);
        else
            GPIO_ResetBits(SR_PORT, SER_PIN);

        // Pulse shift clock
        GPIO_SetBits(SR_PORT, SRCLK_PIN);
        GPIO_ResetBits(SR_PORT, SRCLK_PIN);
    }

    // Pulse latch to push data to outputs
    GPIO_SetBits(SR_PORT, RCLK_PIN);
    GPIO_ResetBits(SR_PORT, RCLK_PIN);
}

// Hardware SPI
void SPI_SR_Init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1 | RCC_APB2Periph_GPIOC, ENABLE);

    // MOSI = PC6, SCK = PC5
    GPIO_InitTypeDef gpio = {
        .GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_6,
        .GPIO_Mode  = GPIO_Mode_AF_PP,
        .GPIO_Speed = GPIO_Speed_50MHz,
    };
    GPIO_Init(GPIOC, &gpio);

    // Latch pin as output
    gpio.GPIO_Pin  = LATCH_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(LATCH_PORT, &gpio);

    SPI_InitTypeDef spi = {
        .SPI_Direction         = SPI_Direction_1Line_Tx,
        .SPI_Mode              = SPI_Mode_Master,
        .SPI_DataSize          = SPI_DataSize_8b,
        .SPI_CPOL              = SPI_CPOL_Low,
        .SPI_CPHA              = SPI_CPHA_1Edge,
        .SPI_NSS               = SPI_NSS_Soft,
        .SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8,
        .SPI_FirstBit          = SPI_FirstBit_MSB,
    };
    SPI_Init(SPI1, &spi);
    SPI_Cmd(SPI1, ENABLE);
}

void SPI_SR_Send(uint8_t data) {
    // Wait until TX buffer empty, then send
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI1, data);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
    // Wait until transfer complete, then latch
    /*
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
    GPIO_SetBits(LATCH_PORT, LATCH_PIN);
    GPIO_ResetBits(LATCH_PORT, LATCH_PIN);
    */
}

void SPI_SR_latch() {
    // Wait until transfer complete, then latch
    //while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
    GPIO_SetBits(LATCH_PORT, LATCH_PIN);
    GPIO_ResetBits(LATCH_PORT, LATCH_PIN);
}

void SPI_SR_Send16(uint16_t data) {
    SR_Send((data >> 8) & 0xFF);  // High byte first
    SR_Send(data & 0xFF);          // Low byte second
    // Latch happens inside SR_Send on each call,
    // so adjust to latch only once after both bytes:
}


int main(void) {
    SystemInit();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    //RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    SystemCoreClockUpdate();
    Delay_Init();
    SysTick_FreeRunning_Init();


    #if (SDI_PRINT == SDI_PR_OPEN)
        SDI_Printf_Enable();
    #else
        USART_Printf_Init(115200);
    #endif

    // Vali ¨¹ks
    //SR_Init();  // Software ShiftOut
    SPI_SR_Init();  // Hardware ShiftOut

    uint32_t last_update_time = 0;
    // 50000 microseconds = 50 milliseconds
    const uint32_t interval_us = 60;
    uint8_t i = 0;

    printf("SystemClk: %d\r\n", SystemCoreClock);
    printf("ChipID: %08x\r\n", DBGMCU_GetCHIPID());

    while (1) {
        uint32_t current_time = micros();

        if (current_time - last_update_time >= interval_us) {
            last_update_time = current_time;

            SPI_SR_Send(digits[i]);
            SPI_SR_Send(output[i]);
            SPI_SR_latch();
            i++;
            if (i == NUM_OF_DIGITS) { i = 0;}
            
        }
/*
            for (uint8_t i = 0; i < NUM_OF_DIGITS; i++) {
                SPI_SR_Send(digits[i]);
                SPI_SR_Send(output[i]);
                SPI_SR_latch();
                Delay_Us(paus);
            }
*/
    }
    
}