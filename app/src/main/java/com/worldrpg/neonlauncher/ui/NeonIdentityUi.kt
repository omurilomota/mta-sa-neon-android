package com.worldrpg.neonlauncher.ui

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.SystemClock
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.worldrpg.neonlauncher.network.NeonIdentityApi
import com.worldrpg.neonlauncher.network.NeonIdentitySession
import kotlinx.coroutines.*
import java.util.function.Consumer

/** Shared by launcher and engine. Never keeps a game handshake alive during browser login. */
class NeonIdentityUi(private val activity: Activity) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val session = NeonIdentitySession(activity)
    private val api = NeonIdentityApi()
    private var signInJob: Job? = null
    private var dialog: androidx.appcompat.app.AlertDialog? = null

    fun isSignedIn(): Boolean = session.load() != null
    fun signOut() = session.clear()

    fun signIn(success: Runnable, failure: Consumer<String>) {
        if (signInJob?.isActive == true) return
        signInJob = scope.launch {
            dialog = MaterialAlertDialogBuilder(activity).setTitle("SA:O account")
                .setMessage("Opening Discord. Complete sign-in in your browser, then return to the launcher.")
                .setNegativeButton(android.R.string.cancel) { _, _ ->
                    signInJob?.cancel()
                    failure.accept("SA:O sign-in canceled.")
                }.setCancelable(false).show()
            try {
                val flow = api.start()
                activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(flow.authorizationUrl)).addCategory(Intent.CATEGORY_BROWSABLE))
                val deadline = SystemClock.elapsedRealtime() + 300_000
                var token: String? = null
                while (token == null && SystemClock.elapsedRealtime() < deadline) {
                    delay(2_500)
                    token = api.poll(flow)
                }
                if (token == null) throw java.io.IOException("SA:O sign-in expired. Please try again.")
                currentCoroutineContext().ensureActive()
                session.save(token)
                dialog?.dismiss()
                dialog = null
                success.run()
            } catch (cancel: CancellationException) {
                throw cancel
            } catch (_: Exception) {
                dialog?.dismiss()
                dialog = null
                failure.accept("Unable to complete SA:O sign-in. Please try again.")
            }
        }
    }

    fun authorize(serverId: String, endpoint: String, required: Boolean, success: Consumer<String>, failure: Consumer<String>) {
        scope.launch {
            try {
                val token = session.load()
                if (token == null) {
                    if (required) failure.accept("Sign in to your SA:O account before connecting to this server.")
                    else success.accept("")
                    return@launch
                }
                success.accept(api.ticket(token, serverId, endpoint))
            } catch (cancel: CancellationException) {
                throw cancel
            } catch (_: NeonIdentityApi.SessionExpired) {
                session.clear()
                failure.accept("Your SA:O session expired. Sign in again.")
            } catch (_: Exception) {
                failure.accept("Unable to authorize this server with your SA:O account. Please try again.")
            }
        }
    }

    fun close() {
        scope.cancel()
        dialog?.dismiss()
        dialog = null
    }
}
