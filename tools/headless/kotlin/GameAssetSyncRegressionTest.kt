package com.worldrpg.neonlauncher.utils

import android.content.Context
import com.worldrpg.neonlauncher.BuildConfig
import kotlinx.coroutines.flow.last
import kotlinx.coroutines.runBlocking
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLConnection
import java.net.URLStreamHandler
import java.nio.file.Files
import java.security.MessageDigest
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicReference

private fun manifest(files: Map<String, ByteArray>): String = GameAssetManifest.encode(files.map { (path, bytes) ->
    val sha = MessageDigest.getInstance("SHA-256").apply { update(bytes) }.hexDigest()
    GameAssetManifest.Entry(path, bytes.size.toLong(), sha)
})

private fun write(root: File, path: String, text: String) {
    File(root, path).apply { parentFile.mkdirs(); writeText(text) }
}

private fun installed(bridge: NativeAssetBridge): File = File(checkNotNull(bridge.getReadyStoragePath()))

private suspend fun verifyLocalImport(root: File) {
    val source = File(root, "source")
    BuildConfig.CDN_ROOT_URL = ""
    BuildConfig.DEVICE_ASSETS_PATH = source.path
    val context = Context(File(root, "local"))
    val bridge = NativeAssetBridge(context)
    for (path in GameAssetManifest.requiredFiles) write(source, path, "new")
    write(source, "data/tuning.dat", "")
    write(context.filesDir, "game_assets/staging-local/data/gta.dat", "old")
    write(context.filesDir, "game_assets/staging-local/data/removed.cfg", "stale")

    expectComplete(bridge)
    val generation = installed(bridge)
    check(File(generation, "data/gta.dat").readText() == "new")
    check(!File(generation, "data/removed.cfg").exists())
    check(File(generation, "data/tuning.dat").isFile)
    val entries = GameAssetManifest.parse(File(generation, GameAssetManifest.FILE_NAME).readText(), local = true)
    check(entries.all { it.sha256.length == 64 })
    val legacy = GameAssetManifest.encode(entries.map { it.copy(sha256 = "") })
    write(generation, GameAssetManifest.FILE_NAME, legacy)
    check(installed(bridge) == generation)
    write(source, "data/gta.dat", "")
    check(bridge.syncAssets().last() is NativeAssetBridge.DownloadState.Error)
    check(installed(bridge) == generation)
}

private data class Update(
    val files: Map<String, ByteArray>,
    val json: String = manifest(files),
    val corrupt: String? = null
)

private suspend fun verifyCdn(root: File) {
    val first = Update((GameAssetManifest.requiredFiles + "data/addon.dat").associateWith { "old".toByteArray() })
    val second = Update(first.files + mapOf("data/default.dat" to "new".toByteArray(), "data/addon.dat" to "new".toByteArray()))
    val update = AtomicReference(first)
    val requests = ConcurrentHashMap<String, Int>()
    URL.setURLStreamHandlerFactory { scheme ->
        if (scheme != "http") null else object : URLStreamHandler() {
            override fun openConnection(url: URL): URLConnection {
                check(url.host == "assets.test")
                val current = update.get()
                val prefix = "/main/game_files_android_mta/"
                val body = when {
                    url.path == "/main/${GameAssetManifest.FILE_NAME}" -> current.json.toByteArray()
                    url.path.startsWith(prefix) -> {
                        val asset = url.path.removePrefix(prefix)
                        requests.merge(asset, 1, Int::plus)
                        if (asset == current.corrupt) "bad".toByteArray() else current.files[asset]
                    }
                    else -> null
                }
                return object : HttpURLConnection(url) {
                    override fun connect() {}
                    override fun disconnect() {}
                    override fun usingProxy() = false
                    override fun getResponseCode() = if (body == null) 404 else 200
                    override fun getInputStream(): InputStream = checkNotNull(body).inputStream()
                }
            }
        }
    }
    BuildConfig.DEVICE_ASSETS_PATH = ""
    BuildConfig.CDN_ROOT_URL = "http://assets.test"
    val context = Context(File(root, "cdn"))
    val bridge = NativeAssetBridge(context)
    expectComplete(bridge)
    val before = installed(bridge)
    check(File(before, "data/addon.dat").readText() == "old")
    val marker = File(context.filesDir, "game_assets/active")
    val previousMarker = marker.readText()

    update.set(second.copy(corrupt = "data/addon.dat"))
    check(bridge.syncAssets().last() is NativeAssetBridge.DownloadState.Error)
    check(installed(bridge) == before)
    val copied = requests["data/default.dat"]

    update.set(second)
    expectComplete(bridge)
    val after = installed(bridge)
    check(after != before && File(after, "data/addon.dat").readText() == "new")
    check(requests["data/default.dat"] == copied) { "Retry redownloaded a verified file" }

    marker.writeText(previousMarker)
    val beforeRecovery = requests.toMap()
    expectComplete(bridge)
    check(installed(bridge) == after && requests == beforeRecovery)

    val missingHashes = second.json.replace(Regex("\"sha256\":\"[0-9a-f]{64}\""), "\"sha256\":\"\"")
    update.set(second.copy(json = missingHashes))
    check(bridge.syncAssets().last() is NativeAssetBridge.DownloadState.Error)
    check(installed(bridge) == after)
    check(File(after, "data/addon.dat").delete())
    check(bridge.getReadyStoragePath() == null)
}

private suspend fun expectComplete(bridge: NativeAssetBridge) {
    val result = bridge.syncAssets().last()
    check(result is NativeAssetBridge.DownloadState.Complete) { "Expected a completed import, got $result" }
}

fun main() = runBlocking {
    val root = Files.createTempDirectory("neon-assets-test").toFile()
    try {
        verifyLocalImport(root)
        verifyCdn(root)
        val valid = manifest(GameAssetManifest.requiredFiles.associateWith { byteArrayOf(1) })
        val pinned = GameAssetManifest.parseRemote("a".repeat(40), valid)
        check(pinned.sourceRevision == "a".repeat(40))
        val fallback = GameAssetManifest.parseRemote(null, valid)
        check(fallback.sourceRevision == "main" && fallback.generationId.matches(Regex("[a-f0-9]{40}")))
        check(runCatching { GameAssetManifest.parseRemote(null, valid + "{}") }.exceptionOrNull() is IOException)
    } finally {
        root.deleteRecursively()
    }
    println("PASS: SHA-256, unpinned CDN, retry, atomic activation, recovery and local asset import")
}
