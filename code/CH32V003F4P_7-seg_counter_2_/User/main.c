/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : Tauno Erik
 * Version            : V1.0.0
 * Date               : 2026/08/19
 * Description        : Main program body.
 *******************************************************************************/

/*
 *@Note
 *Hardware connection:PD5 -- Rx
 *                    PD6 -- Tx
 // MOSI = PC6, SCK = PC5
 *
 */

#include "debug.h"
#include <stdio.h>
#include <stdbool.h>

// Define Time
struct Aeg {
    uint32_t last_time;
    uint32_t interval;
    bool is_time;
};


struct Pin {
    GPIO_TypeDef * port;
    uint16_t pin;
};

// Latch pin
struct Pin latch = {
    GPIOC,      // port
    GPIO_Pin_3  // pin
};

struct Data {
    uint8_t alumine[8];
    uint8_t ylemine[8];
};

// Hardware SPI shiftout
// Master Out Slave In - MOSI = PC6
// Serial Clock        - SCK = PC5
//#define LATCH_PORT GPIOC
//#define LATCH_PIN GPIO_Pin_3


#define MSB_FIRST 0
#define LSB_FIRST 1

#define OFF 0b00000000

/*
LEDid mis peavada p?lema, et kuvada numbreid
*/
static const uint8_t NUMBRID[11] = {
    0b00111111,  // 0
    0b00000110,  // 1
    0b01011011,  // 2
    0b01001111,  // 3
    0b01100110,  // 4
    0b01101101,  // 5
    0b01111101,  // 6
    0b00000111,  // 7
    0b01111111,  // 8
    0b01101111,  // 9
    0b00000000   // 10 - OFF
};

/*
Positsioonid mida saab korraga uuendada
*/
static const uint8_t AKTIIVNE_POS[4] = {
    0b00010001,   // 0 & 4
    0b00100010,   // 1 & 5
    0b01000100,   // 2 & 6
    0b10001000,   // 3 & 7
};

static const uint8_t reverse_table[16] = {
    0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
    0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF
    };

// SystemCoreClock is typically 48000000 (48MHz)
// We pre-calculate how many ticks happen per microsecond
volatile uint32_t ticks_per_us = 0;

// Protot¨¹¨¹bid
void SysTick_FreeRunning_Init (void);
uint32_t micros (void);
uint8_t reverse_bits_fast (uint8_t b);
void SPI_SR_Init (void);
void SPI_SR_Send (uint8_t data, uint8_t dir);
void SPI_SR_latch(void);
void update_counter (uint8_t digits[]);
void update_display (const struct Data *rows);
void remove_leading_zeros (uint8_t digits[]);


int main (void) {
    SystemInit();
    NVIC_PriorityGroupConfig (NVIC_PriorityGroup_1);
    // RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    SystemCoreClockUpdate();
    Delay_Init();
    SysTick_FreeRunning_Init();


#if (SDI_PRINT == SDI_PR_OPEN)
    SDI_Printf_Enable();
#else
    USART_Printf_Init (115200);
#endif

    // Vali ¨¹ks
    // SR_Init();  // Software ShiftOut
    SPI_SR_Init();  // Hardware ShiftOut

    // 50000 microseconds = 50 milliseconds
    struct Aeg counter = {
        0,      // last_time
        10000,  // interval
        false   // is_time
    };

    struct Aeg display = {
        0,     // last_time
        800,   // interval
        false  // is_time
    };

    struct Aeg counter_aeglane = {
        0,      // last_time
        199333,  // interval
        false   // is_time
    };

    struct Data numbridreas = {
        // paremalt vasakule LSB
        // 10 == OFF
        {0, 0, 0, 0, 0, 0, 0, 0}, // alumine
        {0, 0, 0, 0, 0, 0, 0, 0}  // ?lemine
    };

    printf ("SystemClk: %d\r\n", SystemCoreClock);
    printf ("ChipID: %08x\r\n", DBGMCU_GetCHIPID());

    while (1) {
        // ajastamine
        uint32_t current_time = micros();

        // Counter time
        if (current_time - counter.last_time >= counter.interval) {
            counter.last_time = current_time;
            counter.is_time = true;
        }

        // Display time
        if (current_time - display.last_time >= display.interval) {
            display.last_time = current_time;
            display.is_time = true;
        }

        // Counter time
        if (current_time - counter_aeglane.last_time >= counter_aeglane.interval) {
            counter_aeglane.last_time = current_time;
            counter_aeglane.is_time = true;
        }

        // Update counter
        if (counter.is_time) {
            counter.is_time = false;
            update_counter (numbridreas.alumine);
        }

        if (counter_aeglane.is_time) {
            counter_aeglane.is_time = false;
            update_counter (numbridreas.ylemine);
            /* remove_trailing_zeros(numbridreas.ylemine); */
        }

        // Update display
        if (display.is_time) {
            display.is_time = false;
            //update_display_uus (digits_alumine);
            remove_leading_zeros(numbridreas.alumine);
            remove_leading_zeros(numbridreas.ylemine);

            update_display (&numbridreas);
        }
    }
}


/***************************************************
 *
 **************************************************/
void SysTick_FreeRunning_Init (void) {
    ticks_per_us = SystemCoreClock / 1000000;  // 48 ticks per us at 48MHz

    SysTick->CTLR = 0;                         // Disable counter during config
    SysTick->CNT = 0;                          // Reset the 64-bit counter
    SysTick->CMP = 0xFFFFFFFF;                 // Set compare to max so it doesn't trigger unexpectedly

    // CTLR Bits:
    // Bit 0 = Enable Counter
    // Bit 1 = Disable Interrupt (0) -> We don't want interrupts crushing the CPU!
    // Bit 2 = Clock Source (1 = HCLK, 0 = HCLK/8)
    SysTick->CTLR = 0x05;  // Enable counter, Use HCLK, No Interrupt
}

/*
 * Returns the number of microseconds elapsed since boot
 */
uint32_t micros (void) {
    // Read the lower 32 bits of the 64-bit hardware counter
    // At 48MHz, a 32-bit counter overflows every ~89 seconds,
    // which is plenty for non-blocking delta-time checks.
    return (uint32_t)(SysTick->CNT / ticks_per_us);
}

/*
*/
uint8_t reverse_bits_fast (uint8_t b) {
    return (reverse_table[b & 0xF] << 4) | reverse_table[b >> 4];
}


/*
 * Hardware SPI
 */
void SPI_SR_Init (void) {
    RCC_APB2PeriphClockCmd (RCC_APB2Periph_SPI1 | RCC_APB2Periph_GPIOC, ENABLE);

    // MOSI = PC6, SCK = PC5
    GPIO_InitTypeDef gpio = {
        .GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6,
        .GPIO_Mode = GPIO_Mode_AF_PP,
        .GPIO_Speed = GPIO_Speed_50MHz,
    };
    GPIO_Init (GPIOC, &gpio);

    // Latch pin as output
    gpio.GPIO_Pin = latch.pin;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init (latch.port, &gpio);

    SPI_InitTypeDef spi = {
        .SPI_Direction = SPI_Direction_1Line_Tx,
        .SPI_Mode = SPI_Mode_Master,
        .SPI_DataSize = SPI_DataSize_8b,
        .SPI_CPOL = SPI_CPOL_Low,
        .SPI_CPHA = SPI_CPHA_1Edge,
        .SPI_NSS = SPI_NSS_Soft,
        .SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8,
        .SPI_FirstBit = SPI_FirstBit_MSB,
    };
    SPI_Init (SPI1, &spi);
    SPI_Cmd (SPI1, ENABLE);
}

/*
*/
void SPI_SR_Send (uint8_t data, uint8_t dir) {
    if (dir == LSB_FIRST) {
        data = reverse_bits_fast (data);
    }
    // Wait until TX buffer empty, then send
    while (SPI_I2S_GetFlagStatus (SPI1, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData (SPI1, data);
    while (SPI_I2S_GetFlagStatus (SPI1, SPI_I2S_FLAG_BSY) == SET);
    // Wait until transfer complete, then latch
    /*
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
    GPIO_SetBits(LATCH_PORT, LATCH_PIN);
    GPIO_ResetBits(LATCH_PORT, LATCH_PIN);
    */
}

/*
*/
void SPI_SR_latch(void) {
    // Wait until transfer complete, then latch
    // while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
    GPIO_SetBits (latch.port, latch.pin);
    GPIO_ResetBits (latch.port, latch.pin);
}

/*
*/
/* void SPI_SR_Send16(uint16_t data) {
    SPI_SR_Send((data >> 8) & 0xFF);  // High byte first
    SPI_SR_Send(data & 0xFF);          // Low byte second
    // Latch happens inside SR_Send on each call,
    // so adjust to latch only once after both bytes:
} */

/*
Loendur
Paremalt Vasakule
Tavalised numbrid
Muudab numbreid massiivis
*/
void update_counter (uint8_t digits[]) {
    // muuda 10-ned (off) tagasi 0-ks
    for(uint8_t i = 0; i < 8; i++){
         if (digits[i] > 9) {
            digits[i] = 0;
         }
    }

    digits[0]++;
    if (digits[0] > 9) {
        digits[0] = 0;
        digits[1]++;
    }
    if (digits[1] > 9) {
        digits[1] = 0;
        digits[2]++;
    }
    if (digits[2] > 9) {
        digits[2] = 0;
        digits[3]++;
    }
    if (digits[3] > 9) {
        digits[3] = 0;
        digits[4]++;
    }
    if (digits[4] > 9) {
        digits[4] = 0;
        digits[5]++;
    }
    if (digits[5] > 9) {
        digits[5] = 0;
        digits[6]++;
    }
    if (digits[6] > 9) {
        digits[6] = 0;
        digits[7]++;
    }
    if (digits[7] > 9) {
        digits[7] = 0;
        // i8++;
    }
}


/*
Uuenda ekraanil kuvatavaid numbreid.
*/
void update_display (const struct Data *rows) {
    if (rows == NULL) return; // Guard against null pointers
    static int pos_counter = 0;

    switch (pos_counter) {
        case 0:
            // Sea alumise rea esimene number ON
            // ja sea alumise rea 4 number ON
            SPI_SR_Send (AKTIIVNE_POS[0], LSB_FIRST);    // alumine rida
            SPI_SR_Send (AKTIIVNE_POS[0], LSB_FIRST);    // ?lemine rida

            SPI_SR_Send (NUMBRID[rows->alumine[0]], MSB_FIRST);  // data4
            SPI_SR_Send (NUMBRID[rows->alumine[4]], MSB_FIRST);                         // data3
            SPI_SR_Send (NUMBRID[rows->ylemine[0]], MSB_FIRST);                         // data2
            SPI_SR_Send (NUMBRID[rows->ylemine[4]], MSB_FIRST);
            break;
        case 1:
            SPI_SR_Send (AKTIIVNE_POS[1], LSB_FIRST);
            SPI_SR_Send (AKTIIVNE_POS[1], LSB_FIRST);  // ?¹lemine rida

            SPI_SR_Send (NUMBRID[rows->alumine[1]], MSB_FIRST);  // data4
            SPI_SR_Send (NUMBRID[rows->alumine[5]], MSB_FIRST);                         // data3
            SPI_SR_Send (NUMBRID[rows->ylemine[1]], MSB_FIRST);                         // data2
            SPI_SR_Send (NUMBRID[rows->ylemine[5]], MSB_FIRST);
            break;
        case 2:
            SPI_SR_Send (AKTIIVNE_POS[2], LSB_FIRST);  // alumine rida
            SPI_SR_Send (AKTIIVNE_POS[2], LSB_FIRST);  // ?lemine rida

            SPI_SR_Send (NUMBRID[rows->alumine[2]], MSB_FIRST);  // data4
            SPI_SR_Send (NUMBRID[rows->alumine[6]], MSB_FIRST);                         // data3
            SPI_SR_Send (NUMBRID[rows->ylemine[2]], MSB_FIRST);                         // data2
            SPI_SR_Send (NUMBRID[rows->ylemine[6]], MSB_FIRST);
            break;
        case 3:
            SPI_SR_Send (AKTIIVNE_POS[3], LSB_FIRST);                  // digit2 alumine rida
            SPI_SR_Send (AKTIIVNE_POS[3], LSB_FIRST);                        // digit1 ¨¹lemine rida

            SPI_SR_Send (NUMBRID[rows->alumine[3]], MSB_FIRST);  // data4
            SPI_SR_Send (NUMBRID[rows->alumine[7]], MSB_FIRST);                         // data3
            SPI_SR_Send (NUMBRID[rows->ylemine[3]], MSB_FIRST);                         // data2
            SPI_SR_Send (NUMBRID[rows->ylemine[7]], MSB_FIRST);
            break;
        default:
            break;
    }

    pos_counter++;
    if (pos_counter >= 4) {
        pos_counter = 0;
    }
    SPI_SR_latch();
}


/*
Eemalda ees olevad nullid
10 on OFF
*/
void remove_leading_zeros (uint8_t digits[]) {
    uint8_t off = 10;

    if (digits[7] == 0) {
        digits[7] = off;
    }
    if (digits[7] == off && digits[6] == 0) {
        digits[6] = off;
    }
    if (digits[6] == off && digits[5] == 0) {
        digits[5] = off;
    }
    if (digits[5] == off && digits[4] == 0) {
        digits[4] = off;
    }
    if (digits[4] == off && digits[3] == 0) {
        digits[3] = off;
    }
    if (digits[3] == off && digits[2] == 0) {
        digits[2] = off;
    }
    if (digits[2] == off && digits[1] == 0) {
        digits[1] = off;
    }
    if (digits[1] == off && digits[0] == 0) {
        digits[0] = off;
    }
}

