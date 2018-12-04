// SPDX-License-Identifier: GPL-3.0
/*
 * Copyright (c) 2014-2018 Nils Weiss
 */

#include <cstring>
#include "trace.h"
#include "Can.h"
#include "IsoTp.h"
#include "TestIsoTp.h"

static const int __attribute__((unused)) g_DebugZones = ZONE_ERROR | ZONE_WARNING | ZONE_VERBOSE | ZONE_INFO;

const os::TaskEndless canTest("ISOTP_Test",
                              1024, os::Task::Priority::HIGH, [](const bool&){
                              constexpr const hal::Can& can = hal::Factory<hal::Can>::get<hal::Can::MAINCAN>();
                              app::ISOTP isotp(can);

                              Trace(ZONE_INFO, "Hallo ISOTP Test\r\n");
                              while (true) {
                                  os::ThisTask::sleep(std::chrono::milliseconds(300));
                                  std::string_view message = "Erste Nachricht mit isotp";

                                  isotp.send_Message(0x4FF, message);

                                  os::ThisTask::sleep(std::chrono::milliseconds(300));
                                  Trace(ZONE_INFO, "pending %d\r\n", can.messagePending());

                                  // app::isotp.receive_Message();
                              }
    });
