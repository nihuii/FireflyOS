package com.fireflyos.companion.data

enum class ConnectionStatus {
    Idle,
    Scanning,
    Connecting,
    Connected,
    Disconnected,
    Error,
}

data class DeviceState(
    val status: ConnectionStatus = ConnectionStatus.Idle,
    val deviceName: String? = null,
    val address: String? = null,
    val connectedAtMillis: Long = 0L,
    val errorMessage: String? = null,
)
