/*
 * Copyright 2016-2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    camera.c
 * @brief   Application entry point.
 */
#include <stdio.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "MK22F51212.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_adc16.h"

//Clock pin
#define BOARD_LED_GPIO GPIOC
#define BOARD_LED_GPIO_PIN 3

//SI pin
#define BOARD__SI_GPIO_PIN 6

//Pin analog reception
#define BOARD_DAC_BASEADDR ADC0
#define DEMO_ADC16_USER_CHANNEL  8U

// Seuils ajustés pour correspondre à la nouvelle logique
#define THRESHOLD_BLACK 1000 // Seuil pour la détection du noir
#define THRESHOLD_WHITE 3000 // Seuil pour la détection du blanc
#define SEQUENCE_LENGTH 3
#define PIXEL_COUNT 128

#define ALPHA 0.8 //Facteur filtrage

void clockPulse();
void delay(uint32_t duration);
void initADC();
int readADC();
void detectTrack(int *pixels, int length, int *leftPosition, int *rightPosition);
int readFilteredADC();
void adjustDirection(int position);

adc16_config_t adc16ConfigStruct;
adc16_channel_config_t adc16ChannelConfigStruct;
int pixelArray[128];

/*
 * Configuration des broches GPIO pour la clock et SI
 * Initialisation des GPIO et de l'ADC
 * Generation d impulsion de clock pour synchroniser la lecture des pixels
 */

int main(void) {

    gpio_pin_config_t gpioConfigclk;
    gpioConfigclk.pinDirection = kGPIO_DigitalOutput;
    gpioConfigclk.outputLogic = 1;

    gpio_pin_config_t gpioConfigsi;
    gpioConfigsi.pinDirection = kGPIO_DigitalOutput;
    gpioConfigsi.outputLogic = 1;

    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
    #ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
    BOARD_InitDebugConsole();
    #endif

    GPIO_PinInit(BOARD_LED_GPIO, BOARD_LED_GPIO_PIN, &gpioConfigclk);
    GPIO_PinInit(BOARD_LED_GPIO, BOARD__SI_GPIO_PIN, &gpioConfigsi);
    initADC();

    GPIO_PinWrite(BOARD_LED_GPIO, BOARD__SI_GPIO_PIN, 1);
    GPIO_PinWrite(BOARD_LED_GPIO, BOARD_LED_GPIO_PIN, 1);
    GPIO_PinWrite(BOARD_LED_GPIO, BOARD__SI_GPIO_PIN, 0);
    GPIO_PinWrite(BOARD_LED_GPIO, BOARD_LED_GPIO_PIN, 0);

    for(int i=0; i<128; i++) {
        clockPulse();
    }
    GPIO_PinWrite(BOARD_LED_GPIO, BOARD__SI_GPIO_PIN, 0);
    GPIO_PinWrite(BOARD_LED_GPIO, BOARD_LED_GPIO_PIN, 0);
    delay(80000);

    /* Reinitialisation des broches SI et clock
     * Lecture des valeurs de pixels et stockage pixelArray
     * Appel detect track pour analyser les valuers des pixel et detecter la piste
     * Si bordure detecté, calcul le centre de la piste et on appelle adjustDirection pour ajuster la direction du robot
     */
    while(1){
        delay(8000000);
        PRINTF("Test");
        GPIO_PinWrite(BOARD_LED_GPIO, BOARD__SI_GPIO_PIN, 1);
        GPIO_PinWrite(BOARD_LED_GPIO, BOARD_LED_GPIO_PIN, 1);
        GPIO_PinWrite(BOARD_LED_GPIO, BOARD__SI_GPIO_PIN, 0);
        GPIO_PinWrite(BOARD_LED_GPIO, BOARD_LED_GPIO_PIN, 0);

        for(int i=0; i<128; i++) {
            clockPulse();
            pixelArray[i] = readADC();
            PRINTF("%i\r\n", pixelArray[i]);
        }
        GPIO_PinWrite(BOARD_LED_GPIO, BOARD__SI_GPIO_PIN, 0);
        GPIO_PinWrite(BOARD_LED_GPIO, BOARD_LED_GPIO_PIN, 0);

        int leftPosition = -1, rightPosition = -1;
        detectTrack(pixelArray, PIXEL_COUNT, &leftPosition, &rightPosition);

        if (leftPosition != -1 && rightPosition != -1) {
            int trackCenter = (leftPosition + rightPosition) / 2;
            adjustDirection(trackCenter);
        } else {
            PRINTF("Track not detected properly.\n");
        }
    };
    return 0;
}

//introsuit delai on executant une instruction NOP
void delay(uint32_t duration) {
    volatile uint32_t i = 0;
    for (i = 0; i < duration; ++i) {
        __asm("NOP"); /* delay */
    }
}

//Applique filtrage pour lisser la valeur lue
int readFilteredADC() {
    static int previousValue = 0;
    int rawValue = readADC();
    int filteredValue = (int)(ALPHA * rawValue + (1 - ALPHA) * previousValue);
    previousValue = filteredValue;
    return filteredValue;
}

void clockPulse() {
    GPIO_PinWrite(BOARD_LED_GPIO, BOARD_LED_GPIO_PIN, 1);
    delay(1);
    GPIO_PinWrite(BOARD_LED_GPIO, BOARD_LED_GPIO_PIN, 0);
    delay(1);
}

// Initialisation de l'ADC
void initADC() {
    ADC16_GetDefaultConfig(&adc16ConfigStruct);
    ADC16_Init(BOARD_DAC_BASEADDR, &adc16ConfigStruct);
    ADC16_EnableHardwareTrigger(BOARD_DAC_BASEADDR, false);
    if (ADC16_DoAutoCalibration(BOARD_DAC_BASEADDR) != kStatus_Success) {
        PRINTF("ADC calibration failed!\n");
    } else {
        PRINTF("ADC calibration successful!\n");
    }
    adc16ChannelConfigStruct.channelNumber = DEMO_ADC16_USER_CHANNEL;
    adc16ChannelConfigStruct.enableInterruptOnConversionCompleted = false;
    adc16ChannelConfigStruct.enableDifferentialConversion = false;
}

int readADC() {
    ADC16_SetChannelConfig(BOARD_DAC_BASEADDR, 0, &adc16ChannelConfigStruct);
    while (!(ADC16_GetChannelStatusFlags(BOARD_DAC_BASEADDR, 0) & kADC16_ChannelConversionDoneFlag)) {
    }
    return ADC16_GetChannelConversionValue(BOARD_DAC_BASEADDR, 0);
}

/*
 * Parcourt les pixels pour trouver deux séquences de pixels noirs
 * vérifie la présence d'une piste blanche entre les deux lignes noires en comptant le nombre de pixels blancs
 */
void detectTrack(int *pixels, int length, int *leftPosition, int *rightPosition) {
    int firstBlackLineStart = -1;
    int secondBlackLineStart = -1;
    int count = 0;

    for (int i = 0; i < length; i++) {
        if (pixels[i] < THRESHOLD_BLACK) { // Détection du noir
            count++;
            if (count >= SEQUENCE_LENGTH) {
                if (firstBlackLineStart == -1) {
                    firstBlackLineStart = i - SEQUENCE_LENGTH + 1;
                } else if (secondBlackLineStart == -1) {
                    secondBlackLineStart = i - SEQUENCE_LENGTH + 1;
                    break;
                }
                count = 0;

           }
        } else {
            count = 0;
        }
    }

    if (firstBlackLineStart != -1 && secondBlackLineStart != -1) {
        *leftPosition = firstBlackLineStart;
        *rightPosition = secondBlackLineStart;
    } else {
        *leftPosition = -1;
        *rightPosition = -1;
    }
}
/*
 * Calcul la deviation de la position du centre de la piste par rapp au centre de l image
 * print la direction
 */
void adjustDirection(int position) {
    int center = PIXEL_COUNT / 2;
    int deviation = position - center;

    if (deviation > 0) {
        PRINTF("Move left\n");
    } else if (deviation < 0) {
        PRINTF("Move right\n");
    } else {
        PRINTF("Move straight\n");
    }
}
