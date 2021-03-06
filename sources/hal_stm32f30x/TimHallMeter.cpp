// SPDX-License-Identifier: GPL-3.0
/*
 * Copyright (c) 2014-2018 Nils Weiss
 */

#include <limits>
#include <cmath>
#include "TimHallMeter.h"
#include "trace.h"

static const int __attribute__((unused)) g_DebugZones = ZONE_ERROR | ZONE_WARNING | ZONE_VERBOSE | ZONE_INFO;

using hal::Factory;
using hal::HallMeter;
using hal::Tim;

#ifndef M_PI
static constexpr float M_PI = 3.14159265358979323846f;
#endif

#if TIM8_HALLMETER_INTERRUPT_ENABLED
extern "C" void TIM8_CC_IRQHandler(void)
{
    constexpr auto& hallMeter = Factory<HallMeter>::get<HallMeter::BLDC_METER>();
    hallMeter.interruptHandler();
}

extern "C" void TIM8_UP_IRQHandler(void)
{
    constexpr auto& hallMeter = Factory<HallMeter>::get<HallMeter::BLDC_METER>();
    hallMeter.interruptHandler();
}
#endif

#if TIM2_HALLMETER_INTERRUPT_ENABLED
extern "C" void TIM2_IRQHandler(void)
{
    constexpr auto& hallMeter = Factory<HallMeter>::get<HallMeter::BLDC_METER_32BIT>();
    hallMeter.interruptHandler();
}
#endif

void HallMeter::interruptHandler(void) const
{
    if (TIM_GetITStatus(mTim.getBasePointer(), TIM_IT_CC1)) {
        TIM_ClearITPendingBit(mTim.getBasePointer(), TIM_IT_CC1);
        TIM_ClearITPendingBit(mTim.getBasePointer(), TIM_IT_Update);
        const uint32_t eventTimestamp = TIM_GetCapture1(mTim.getBasePointer());
        saveTimestamp(eventTimestamp);
    } else if (TIM_GetITStatus(mTim.getBasePointer(), TIM_IT_Update)) {
        TIM_ClearITPendingBit(mTim.getBasePointer(), TIM_IT_Update);
        reset();
    } else {
        // this should not happen
    }
}

void HallMeter::saveTimestamp(const uint32_t timestamp) const
{
    mTimestamps[mTimestampPosition] = timestamp;
    mTimestampPosition = (mTimestampPosition + 1) % NUMBER_OF_TIMESTAMPS;
}

void HallMeter::reset(void) const
{
    mTimestamps.fill(std::numeric_limits<uint32_t>::max());
}

float HallMeter::getCurrentRPS(void) const
{
    static constexpr float HALL_EVENTS_PER_ROTATION = 6;

    const float timerFrequency = mTim.getTimerFrequency();

    uint32_t sumTicksBetweenHallSignals = 0;
    for (size_t i = 0; i < mTimestamps.size(); i++) {
        sumTicksBetweenHallSignals += mTimestamps[i];
    }

    const uint32_t avgTicksBetweenHallSignals = sumTicksBetweenHallSignals / mTimestamps.size();
    const float hallSignalFrequency = timerFrequency / avgTicksBetweenHallSignals;
    const float electricalRotationFrequency = hallSignalFrequency / HALL_EVENTS_PER_ROTATION;
    const float motorRotationFrequency = electricalRotationFrequency / POLE_PAIRS;
    return motorRotationFrequency;
}

float HallMeter::getCurrentOmega(void) const
{
    return getCurrentRPS() * M_PI * 2;
}

void HallMeter::initialize(void) const
{
    mTimestamps.fill(std::numeric_limits<uint32_t>::max());

    /* HallSensor event is delivered with signal TI1F_ED.
     * Rising and falling edge is used.*/
    TIM_SelectInputTrigger(mTim.getBasePointer(), mInputTrigger);

    /* On every event, update is triggered and counter gets reset */
    TIM_SelectSlaveMode(mTim.getBasePointer(), TIM_SlaveMode_Combined_ResetTrigger);

    /* Channel 1 is in input capture mode. On every TCR edge, the counter
     * value is captured and stored into the CCR register. CCR1 interrupt is fired */
    TIM_ICInit(mTim.getBasePointer(), &mIc1Configuration);

    TIM_ClearFlag(mTim.getBasePointer(), TIM_IT_CC1 | TIM_IT_Update);
    TIM_ITConfig(mTim.getBasePointer(), TIM_IT_CC1 | TIM_IT_Update, ENABLE);

    TIM_SelectCOM(mTim.getBasePointer(), ENABLE);

#if TIM8_HALLMETER_INTERRUPT_ENABLED
    NVIC_SetPriority(IRQn_Type::TIM8_CC_IRQn, 0xa);
    NVIC_EnableIRQ(IRQn_Type::TIM8_CC_IRQn);
    NVIC_SetPriority(IRQn_Type::TIM8_UP_IRQn, 0xa);
    NVIC_EnableIRQ(IRQn_Type::TIM8_UP_IRQn);
#endif

#if TIM2_HALLMETER_INTERRUPT_ENABLED
    NVIC_SetPriority(IRQn_Type::TIM2_IRQn, 0xa);
    NVIC_EnableIRQ(IRQn_Type::TIM2_IRQn);
#endif
}

constexpr const std::array<const HallMeter,
                           HallMeter::Description::__ENUM__SIZE> Factory<HallMeter>::Container;
