package com.fireflyos.companion.ui

import com.fireflyos.companion.data.ConnectionStatus

data class CompanionUiState(
    val remoteActionsEnabled: Boolean,
    val unavailableReason: String,
    val localWatchFeaturesRemainAvailable: Boolean = true,
    val mediaSessionAvailable: Boolean = false,
    val calendarPermissionGranted: Boolean = false,
)

object CompanionUiPolicy {
    fun forConnection(status: ConnectionStatus): CompanionUiState {
        val online = status == ConnectionStatus.Connected
        return CompanionUiState(
            remoteActionsEnabled = online,
            unavailableReason = if (online) "" else
                "Remote actions are unavailable while offline; watch-local features remain available.",
        )
    }
}
