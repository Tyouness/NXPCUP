/*
 * Copyright (c) ...
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "board.h"
#include "fsl_pwm.h"
#include "fsl_clock.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/* The PWM base address */
#define BOARD_PWM_BASEADDR        PWM1
#define PWM_SRC_CLK_FREQ          CLOCK_GetFreq(kCLOCK_BusClk)
#define DEMO_PWM_FAULT_LEVEL      true
#define APP_DEFAULT_PWM_FREQUENCY (50UL) // On laisse 50 Hz, comme dans votre code d'origine
#ifndef APP_DEFAULT_PWM_FREQUENCY
#define APP_DEFAULT_PWM_FREQUENCY (50UL)
#endif

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/

/*
 * Cette fonction initialisait les 3 phases en mode "SignedCenterAligned" + complémentaire.
 * On change simplement "kPWM_SignedCenterAligned" en "kPWM_CenterAligned".
 */
static void PWM_DRV_Init3PhPwm(void)
{
    uint16_t deadTimeVal;
    pwm_signal_param_t pwmSignal[2];
    uint32_t pwmSourceClockInHz;
    uint32_t pwmFrequencyInHz = APP_DEFAULT_PWM_FREQUENCY;

    pwmSourceClockInHz = PWM_SRC_CLK_FREQ;

    /* Set deadtime count, on garde le calcul d'origine */
    deadTimeVal = ((uint64_t)pwmSourceClockInHz * 650) / 1000000000;

    pwmSignal[0].pwmChannel       = kPWM_PwmA;
    pwmSignal[0].level            = kPWM_HighTrue;
    pwmSignal[0].dutyCyclePercent = 50; /* 50% dutycycle, inchangé */
    pwmSignal[0].deadtimeValue    = deadTimeVal;
    pwmSignal[0].faultState       = kPWM_PwmFaultState0;
    pwmSignal[0].pwmchannelenable = true;

    pwmSignal[1].pwmChannel       = kPWM_PwmB;
    pwmSignal[1].level            = kPWM_HighTrue;
    pwmSignal[1].dutyCyclePercent = 50;
    pwmSignal[1].deadtimeValue    = deadTimeVal;
    pwmSignal[1].faultState       = kPWM_PwmFaultState0;
    pwmSignal[1].pwmchannelenable = true;

    /* CHANGEMENT ICI : kPWM_CenterAligned au lieu de kPWM_SignedCenterAligned */
    PWM_SetupPwm(BOARD_PWM_BASEADDR,
                 kPWM_Module_0,
                 pwmSignal,
                 2,
                 kPWM_CenterAligned,  // <<< CHANGÉ
                 pwmFrequencyInHz,
                 pwmSourceClockInHz);

    /* Les sous-modules 1 et 2 utilisaient déjà 1 canal seulement, on change également le mode */
#ifdef DEMO_PWM_CLOCK_DEVIDER
    PWM_SetupPwm(BOARD_PWM_BASEADDR,
                 kPWM_Module_1,
                 pwmSignal,
                 1,
                 kPWM_CenterAligned,  // <<< CHANGÉ
                 pwmFrequencyInHz,
                 pwmSourceClockInHz / (1 << DEMO_PWM_CLOCK_DEVIDER));
#else
    PWM_SetupPwm(BOARD_PWM_BASEADDR,
                 kPWM_Module_1,
                 pwmSignal,
                 1,
                 kPWM_CenterAligned,  // <<< CHANGÉ
                 pwmFrequencyInHz,
                 pwmSourceClockInHz);
#endif

#ifdef DEMO_PWM_CLOCK_DEVIDER
    PWM_SetupPwm(BOARD_PWM_BASEADDR,
                 kPWM_Module_2,
                 pwmSignal,
                 1,
                 kPWM_CenterAligned,
                 pwmFrequencyInHz,
                 pwmSourceClockInHz / (1 << DEMO_PWM_CLOCK_DEVIDER));
#else
    PWM_SetupPwm(BOARD_PWM_BASEADDR,
                 kPWM_Module_2,
                 pwmSignal,
                 1,
                 kPWM_CenterAligned,  // <<< CHANGÉ
                 pwmFrequencyInHz,
                 pwmSourceClockInHz);
#endif
}

/*!
 * @brief Main function
 */
int main(void)
{
    pwm_config_t pwmConfig;
    pwm_fault_param_t faultConfig;
    uint32_t pwmVal = 4; // On conserve le cycle initial

    /* Init board, debug, etc. */
    CLOCK_SetClkDiv(kCLOCK_DivFlexcom4Clk, 1u);
    CLOCK_AttachClk(BOARD_DEBUG_UART_CLK_ATTACH);

    BOARD_InitPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    /* Enable PWM1 SUB Clockn */
    SYSCON->PWM1SUBCTL |= (SYSCON_PWM1SUBCTL_CLK0_EN_MASK
                           | SYSCON_PWM1SUBCTL_CLK1_EN_MASK
                           | SYSCON_PWM1SUBCTL_CLK2_EN_MASK);

    PRINTF("FlexPWM driver example (modif pour eviter va-et-vient)\n");

    /*
     * pwmConfig.enableDebugMode        = false;
     * pwmConfig.enableWait             = false;
     * pwmConfig.reloadSelect           = kPWM_LocalReload;
     * pwmConfig.clockSource            = kPWM_BusClock;
     * pwmConfig.prescale               = kPWM_Prescale_Divide_1;
     * pwmConfig.initializationControl  = kPWM_Initialize_LocalSync;
     * pwmConfig.forceTrigger           = kPWM_Force_Local;
     * pwmConfig.reloadFrequency        = kPWM_LoadEveryOportunity;
     * pwmConfig.reloadLogic            = kPWM_ReloadImmediate;
     * pwmConfig.pairOperation          = kPWM_Independent; // On va le mettre sur Independent
     */
    PWM_GetDefaultConfig(&pwmConfig);

    /* On utilise full cycle reload */
    pwmConfig.reloadLogic = kPWM_ReloadPwmFullCycle;

    /*
     * CHANGEMENT ICI : on passe de kPWM_ComplementaryPwmA à kPWM_Independent
     * pour ne plus avoir les sorties complémentaires qui pourraient causer
     * des signaux inversés.
     */
    pwmConfig.pairOperation   = kPWM_Independent;  // <<< CHANGÉ

    pwmConfig.enableDebugMode = true;

    /* Sous-module 0 */
    if (PWM_Init(BOARD_PWM_BASEADDR, kPWM_Module_0, &pwmConfig) == kStatus_Fail)
    {
        PRINTF("PWM initialization failed for SM0\n");
        return 1;
    }

    /* Sous-module 1, même clock que SM0 */
    pwmConfig.clockSource           = kPWM_Submodule0Clock;
    pwmConfig.prescale              = kPWM_Prescale_Divide_1;
    pwmConfig.initializationControl = kPWM_Initialize_MasterSync;

    if (PWM_Init(BOARD_PWM_BASEADDR, kPWM_Module_1, &pwmConfig) == kStatus_Fail)
    {
        PRINTF("PWM initialization failed for SM1\n");
        return 1;
    }

    /* Sous-module 2, idem */
    if (PWM_Init(BOARD_PWM_BASEADDR, kPWM_Module_2, &pwmConfig) == kStatus_Fail)
    {
        PRINTF("PWM initialization failed for SM2\n");
        return 1;
    }

    /*
     * Configuration défaut des fautes
     */
    PWM_FaultDefaultConfig(&faultConfig);
#ifdef DEMO_PWM_FAULT_LEVEL
    faultConfig.faultLevel = DEMO_PWM_FAULT_LEVEL;
#endif

    /* Setup fault pour chaque channel */
    PWM_SetupFaults(BOARD_PWM_BASEADDR, kPWM_Fault_0, &faultConfig);
    PWM_SetupFaults(BOARD_PWM_BASEADDR, kPWM_Fault_1, &faultConfig);
    PWM_SetupFaults(BOARD_PWM_BASEADDR, kPWM_Fault_2, &faultConfig);
    PWM_SetupFaults(BOARD_PWM_BASEADDR, kPWM_Fault_3, &faultConfig);

    /* Disable map si fault sur A */
    PWM_SetupFaultDisableMap(BOARD_PWM_BASEADDR,
                             kPWM_Module_0,
                             kPWM_PwmA,
                             kPWM_faultchannel_0,
                             kPWM_FaultDisable_0
                             | kPWM_FaultDisable_1
                             | kPWM_FaultDisable_2
                             | kPWM_FaultDisable_3);
    PWM_SetupFaultDisableMap(BOARD_PWM_BASEADDR,
                             kPWM_Module_1,
                             kPWM_PwmA,
                             kPWM_faultchannel_0,
                             kPWM_FaultDisable_0
                             | kPWM_FaultDisable_1
                             | kPWM_FaultDisable_2
                             | kPWM_FaultDisable_3);
    PWM_SetupFaultDisableMap(BOARD_PWM_BASEADDR,
                             kPWM_Module_2,
                             kPWM_PwmA,
                             kPWM_faultchannel_0,
                             kPWM_FaultDisable_0
                             | kPWM_FaultDisable_1
                             | kPWM_FaultDisable_2
                             | kPWM_FaultDisable_3);

    /* Appel de la fonction qui configure les PWM (avec le mode CenterAligned) */
    PWM_DRV_Init3PhPwm();

    /* Charger les registres */
    PWM_SetPwmLdok(BOARD_PWM_BASEADDR,
                   kPWM_Control_Module_0
                   | kPWM_Control_Module_1
                   | kPWM_Control_Module_2,
                   true);

    /* Démarrer le PWM */
    PWM_StartTimer(BOARD_PWM_BASEADDR,
                   kPWM_Control_Module_0
                   | kPWM_Control_Module_1
                   | kPWM_Control_Module_2);

    while (1U)
    {
        /* Attendre au moins 100 périodes PWM */
        SDK_DelayAtLeastUs((1000000U / APP_DEFAULT_PWM_FREQUENCY) * 100,
                           SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);

        pwmVal = pwmVal + 4;

        if (pwmVal > 100)
        {
            pwmVal = 4;
        }

        /* Mettre à jour le duty cycle (toujours en mode center-aligned) */
        PWM_UpdatePwmDutycycle(BOARD_PWM_BASEADDR,
                               kPWM_Module_0,
                               kPWM_PwmA,
                               kPWM_CenterAligned, // <<< CHANGÉ
                               pwmVal);

        PWM_UpdatePwmDutycycle(BOARD_PWM_BASEADDR,
                               kPWM_Module_1,
                               kPWM_PwmA,
                               kPWM_CenterAligned, // <<< CHANGÉ
                               (pwmVal >> 1));

        PWM_UpdatePwmDutycycle(BOARD_PWM_BASEADDR,
                               kPWM_Module_2,
                               kPWM_PwmA,
                               kPWM_CenterAligned, // <<< CHANGÉ
                               (pwmVal >> 2));

        /* Recharger les registres */
        PWM_SetPwmLdok(BOARD_PWM_BASEADDR,
                       kPWM_Control_Module_0
                       | kPWM_Control_Module_1
                       | kPWM_Control_Module_2,
                       true);
    }
}
