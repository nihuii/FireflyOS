package com.fireflyos.companion.ble

import org.junit.Assert.assertEquals
import org.junit.Test

class GattMtuPolicyTest {
    @Test
    fun payloadLimitUsesNegotiatedMtuAndProtocolCap() {
        assertEquals(20, GattMtuPolicy.payloadLimit(23))
        assertEquals(180, GattMtuPolicy.payloadLimit(185))
        assertEquals(180, GattMtuPolicy.payloadLimit(517))
    }

    @Test
    fun invalidOrTooSmallMtuFallsBackToDefaultAttPayload() {
        assertEquals(20, GattMtuPolicy.payloadLimit(0))
        assertEquals(20, GattMtuPolicy.payloadLimit(3))
        assertEquals(20, GattMtuPolicy.payloadLimit(22))
    }
}
