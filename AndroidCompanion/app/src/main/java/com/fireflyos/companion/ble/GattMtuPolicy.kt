package com.fireflyos.companion.ble

object GattMtuPolicy {
    const val DEFAULT_MTU = 23
    const val DESIRED_MTU = 185
    const val PROTOCOL_ATT_CAP = 180

    fun payloadLimit(negotiatedMtu: Int): Int {
        val safeMtu = if (negotiatedMtu < DEFAULT_MTU) {
            DEFAULT_MTU
        } else {
            negotiatedMtu
        }
        return minOf(PROTOCOL_ATT_CAP, safeMtu - 3)
    }
}
