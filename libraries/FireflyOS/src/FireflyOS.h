#pragma once

#define FIREFLYOS_VERSION_MAJOR 0
#define FIREFLYOS_VERSION_MINOR 1
#define FIREFLYOS_VERSION_PATCH 0

#include "firefly/core/SystemEvent.h"
#include "firefly/core/EventBus.h"
#include "firefly/core/SystemState.h"
#include "firefly/core/NotificationModel.h"
#include "firefly/core/StateStore.h"
#include "firefly/core/CapabilityRegistry.h"
#include "firefly/core/AppRegistry.h"
#include "firefly/core/AppManager.h"
#include "firefly/core/SystemLifecycle.h"
#include "firefly/core/ResourceGovernor.h"
#include "firefly/hal/DeviceInterfaces.h"
#include "firefly/hal/I2cBusManager.h"
#include "firefly/hal/LockedRegisterDevice.h"
#include "firefly/ui/UiTokens.h"
#include "firefly/ui/UiTheme.h"
#include "firefly/ui/NavigationController.h"
