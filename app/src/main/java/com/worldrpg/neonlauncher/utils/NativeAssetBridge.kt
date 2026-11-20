package com.worldrpg.neonlauncher.utils

import android.content.Context
import android.net.Uri
import android.util.Log
import com.worldrpg.neonlauncher.BuildConfig
import com.worldrpg.neonlauncher.utils.GameAssetManifest.Entry
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.sync.Mutex
import org.json.JSONException
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.UncheckedIOException
import java.net.HttpURLConnection
import java.net.URL
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import java.util.UUID

/** Installs and verifies GTA data from a local folder or configured CDN before making it available to the engine. */
class NativeAssetBridge(private val context: Context) {
    private val storage = File(context.filesDir, "game_assets")
    private val generations = File(storage, "generations")
    private val active = File(storage, "active")
    private val cdnRoot = BuildConfig.CDN_ROOT_URL.trim().trimEnd('/').ifBlank { DEFAULT_CDN_ROOT }

    sealed interface DownloadState {
        data object FetchingManifest : DownloadState
        data class Verifying(val current: Int, val total: Int) : DownloadState
        data class Downloading(val fileName: String, val percent: Int) : DownloadState
        data class Complete(val fileCount: Int) : DownloadState
        data class Error(val message: String) : DownloadState
    }

    /** Local developer installs are available only when no CDN is configured. */
    fun findLocalAssetsFolder(): File? {
        // A leftover backup must never override the CDN, including library loading at boot.
        if (cdnRoot.isNotBlank()) return null
        val candidates = listOfNotNull(
            BuildConfig.DEVICE_ASSETS_PATH.takeIf { it.isNotBlank() }?.let { File(it) },
            context.getExternalFilesDir(null)?.let { File(it, "game_assets") },
            context.getExternalFilesDir(null)?.let { File(it, "gta_assets") },
            File(storage, "local_source"),
            File("/sdcard/Android/data/${context.packageName}/files/game_assets"),
            File("/sdcard/Download/WorldRPG_backup"),
            File("/sdcard/worldrpg_gamefiles"),
            File("/sdcard/mta_assets"),
            File("/sdcard/GTASA")
        )
        return candidates.firstOrNull { dir ->
            try {
                dir.isDirectory && GameAssetManifest.requiredFiles.all { path ->
                    File(dir, path).let { it.isFile && it.length() > 0 }
                }
            } catch (_: SecurityException) {
                false
            }
        }
    }

    /** Fast startup gate. CDN builds never fall back to an external folder or local import. */
    fun getReadyStoragePath(): String? {
        return try {
            val generation = activeGeneration()
            if (generation != null && (cdnRoot.isBlank() || !generation.name.startsWith("local-"))) {
                val entries = installedEntries(generation)
                if (entries != null && entries.all { entry ->
                        val file = assetFile(generation, entry.path)
                        file.isFile && file.length() == entry.size
                    }) return generation.absolutePath
            }
            findLocalAssetsFolder()?.absolutePath
        } catch (error: Exception) {
            Log.w(TAG, "Cannot read installed game assets", error)
            null
        }
    }

    fun syncAssets(): Flow<DownloadState> = flow {
        if (!installationLock.tryLock()) {
            emit(DownloadState.Error("Game assets are already syncing."))
            return@flow
        }

        try {
            // findLocalAssetsFolder is disabled when a CDN is configured.
            val localSource = findLocalAssetsFolder()
            if (localSource != null) {
                emit(DownloadState.FetchingManifest)
                syncFromLocalFolder(localSource) { emit(it) }
                return@flow
            }

            // Sync the configured CDN into private storage before engine startup.
            if (cdnRoot.isBlank()) {
                val expectedPath = context.getExternalFilesDir(null)?.let { File(it, "game_assets").absolutePath }
                    ?: BuildConfig.DEVICE_ASSETS_PATH
                emit(DownloadState.Error(
                    "Local game assets not found!\n" +
                    "Please place original GTA Mobile 2.10 files in:\n" +
                    "$expectedPath\n" +
                    "or configure GAME_ASSETS_DIR in .env"
                ))
                return@flow
            }

            emit(DownloadState.FetchingManifest)
            val manifest = fetchManifest(cdnRoot)
            // Reuse a fully installed generation if a previous sync stopped before switching the pointer.
            val current = activeGeneration()?.takeUnless { it.name.startsWith("local-") }
            val currentEntries = current?.let(::installedEntries)
            val matching = if (currentEntries == manifest.entries) current else
                generations.listFiles().orEmpty()
                    .asSequence()
                    .filter { it != current && it.isDirectory && GENERATION.matches(it.name) && !it.name.startsWith("local-") }
                    .sortedByDescending { it.lastModified() }
                    .firstOrNull { installedEntries(it) == manifest.entries }
            val previous = matching ?: current
            if (matching != null && manifest.entries.withIndex().all { (index, entry) ->
                    emit(DownloadState.Verifying(index + 1, manifest.entries.size))
                    matches(assetFile(matching, entry.path), entry)
                }) {
                activateGeneration(matching)
                emit(DownloadState.Complete(manifest.entries.size))
                return@flow
            }

            ensureDirectory(generations)
            val staging = assetFile(storage, "staging-${manifest.generationId}")
            ensureDirectory(staging)
            val partial = assetFile(storage, "asset-download.part")
            try {
                manifest.entries.forEachIndexed { index, entry ->
                    emit(DownloadState.Verifying(index + 1, manifest.entries.size))
                    val target = assetFile(staging, entry.path)
                    if (matches(target, entry)) return@forEachIndexed
                    ensureDirectory(target.parentFile ?: staging)
                    if (staging.usableSpace < entry.size) {
                        throw IOException("Not enough free space for game assets.")
                    }
                    val old = previous?.let { assetFile(it, entry.path) }
                    if (old != null && matches(old, entry)) {
                        old.inputStream().buffered().use { input ->
                            copyVerified(input, partial, entry) {}
                        }
                    } else {
                        val path = entry.path.split('/').joinToString("/") { Uri.encode(it) }
                        val url = URL("$cdnRoot/${manifest.sourceRevision}/$CONTENT_DIR/$path")
                        val connection = open(url)
                        try {
                            emit(DownloadState.Downloading(entry.path, 0))
                            connection.inputStream.buffered().use { input ->
                                copyVerified(input, partial, entry) { percent ->
                                    emit(DownloadState.Downloading(entry.path, percent))
                                }
                            }
                        } finally {
                            connection.disconnect()
                        }
                    }
                    atomicMove(partial, target)
                }

                currentCoroutineContext().ensureActive()
                val installed = File(generations, "${manifest.generationId}-${UUID.randomUUID()}")
                writeSynced(File(staging, GameAssetManifest.FILE_NAME), manifest.json)
                currentCoroutineContext().ensureActive()
                atomicMove(staging, installed)
                activateGeneration(installed)
            } finally {
                partial.delete()
            }
            emit(DownloadState.Complete(manifest.entries.size))
        } catch (error: IOException) {
            Log.w(TAG, "Game asset sync failed", error)
            emit(DownloadState.Error(error.message ?: "Game asset sync failed."))
        } catch (error: SecurityException) {
            Log.w(TAG, "Cannot access game asset storage", error)
            emit(DownloadState.Error("Cannot access game asset storage."))
        } catch (error: JSONException) {
            Log.w(TAG, "Invalid game asset manifest", error)
            emit(DownloadState.Error("Invalid game asset manifest."))
        } finally {
            installationLock.unlock()
        }
    }.flowOn(Dispatchers.IO)

    private suspend fun syncFromLocalFolder(
        sourceDir: File,
        emitState: suspend (DownloadState) -> Unit
    ) {
        val sourceRoot = sourceDir.canonicalFile
        val files = localEntries(sourceRoot)
        ensureDirectory(generations)
        val installed = File(generations, "local-${UUID.randomUUID()}")
        val staging = assetFile(storage, "staging-${installed.name}")
        ensureDirectory(staging)
        try {
            val verified = files.mapIndexed { index, entry ->
                emitState(DownloadState.Verifying(index + 1, files.size))
                val source = assetFile(sourceRoot, entry.path)
                val target = assetFile(staging, entry.path)
                ensureDirectory(target.parentFile ?: staging)
                val sha = source.inputStream().buffered().use { input ->
                    copyVerified(input, target, entry) {}
                }
                entry.copy(sha256 = sha)
            }
            writeSynced(File(staging, GameAssetManifest.FILE_NAME), GameAssetManifest.encode(verified))
            currentCoroutineContext().ensureActive()
            atomicMove(staging, installed)
            activateGeneration(installed)
        } finally {
            staging.deleteRecursively()
        }
        emitState(DownloadState.Complete(files.size))
    }

    private suspend fun localEntries(sourceDir: File): List<Entry> {
        val root = sourceDir.toPath()
        val files = mutableListOf<Entry>()
        try {
            // Files.walk does not follow symbolic links unless FOLLOW_LINKS is requested.
            Files.walk(root).use { paths ->
                val iterator = paths.iterator()
                while (iterator.hasNext()) {
                    currentCoroutineContext().ensureActive()
                    val path = iterator.next()
                    if (!Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)) continue
                    val relative = root.relativize(path).toString()
                    if (relative == GameAssetManifest.FILE_NAME) continue
                    if (files.size == GameAssetManifest.MAX_FILES) throw IOException("Too many local game asset files.")
                    files.add(Entry(relative, Files.size(path), ""))
                }
            }
        } catch (error: UncheckedIOException) {
            throw IOException("Cannot read local game assets.", error.cause)
        }
        GameAssetManifest.validate(files, local = true)
        return files
    }

    private suspend fun fetchManifest(cdnRoot: String): GameAssetManifest.Remote {
        val connection = open(URL("$cdnRoot/main/${GameAssetManifest.FILE_NAME}"))
        try {
            val json = connection.inputStream.use { input ->
                val output = ByteArrayOutputStream()
                val buffer = ByteArray(BUFFER_SIZE)
                while (true) {
                    currentCoroutineContext().ensureActive()
                    val count = input.read(buffer)
                    if (count < 0) break
                    if (output.size() + count > GameAssetManifest.MAX_BYTES) {
                        throw IOException("Game asset manifest is too large.")
                    }
                    output.write(buffer, 0, count)
                }
                output.toString(Charsets.UTF_8.name())
            }
            return GameAssetManifest.parseRemote(connection.getHeaderField("X-Repo-Commit"), json)
        } finally {
            connection.disconnect()
        }
    }

    private fun installedEntries(directory: File): List<Entry>? {
        return try {
            val file = assetFile(directory, GameAssetManifest.FILE_NAME)
            if (file.length() !in 1..GameAssetManifest.MAX_BYTES.toLong()) return null
            GameAssetManifest.parse(file.readText(), local = directory.name.startsWith("local-"))
        } catch (_: IOException) {
            null
        } catch (_: JSONException) {
            null
        }
    }

    private fun activeGeneration(): File? {
        if (!active.isFile || active.length() !in 1..128) return null
        val name = active.readText()
        if (!GENERATION.matches(name)) return null
        val directory = assetFile(generations, name)
        return directory.takeIf { it.isDirectory && File(it, GameAssetManifest.FILE_NAME).isFile }
    }

    private fun assetFile(root: File, path: String): File {
        val file = File(root.canonicalFile, path)
        if (file.canonicalFile != file) throw IOException("Unsafe game asset path.")
        return file
    }

    private fun ensureDirectory(directory: File) {
        if (!directory.isDirectory && !directory.mkdirs()) {
            throw IOException("Cannot create game asset storage.")
        }
    }

    private fun writeSynced(file: File, contents: String) {
        FileOutputStream(file).use { output ->
            output.write(contents.toByteArray(Charsets.UTF_8))
            output.fd.sync()
        }
    }

    private fun activateGeneration(directory: File) {
        val partial = assetFile(storage, "asset-activation.part")
        try {
            writeSynced(partial, directory.name)
            atomicMove(partial, active)
        } finally {
            partial.delete()
        }
    }

    private fun atomicMove(from: File, to: File) {
        Files.move(from.toPath(), to.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
    }

    private suspend fun matches(file: File, entry: Entry): Boolean {
        if (!file.isFile || file.length() != entry.size) return false
        if (entry.sha256.isEmpty()) return true
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(BUFFER_SIZE)
        file.inputStream().buffered().use { input ->
            while (true) {
                currentCoroutineContext().ensureActive()
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.hexDigest() == entry.sha256
    }

    private suspend fun copyVerified(
        input: InputStream,
        partial: File,
        entry: Entry,
        progress: suspend (Int) -> Unit
    ): String {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(BUFFER_SIZE)
        var total = 0L
        var previousPercent = 0
        return FileOutputStream(partial).use { output ->
            while (true) {
                currentCoroutineContext().ensureActive()
                val count = input.read(buffer)
                if (count < 0) break
                total += count
                if (total > entry.size) throw IOException("Unexpected size for ${entry.path}.")
                digest.update(buffer, 0, count)
                output.write(buffer, 0, count)
                val percent = (100L * total / entry.size).toInt().coerceAtMost(99)
                if (percent > previousPercent) {
                    progress(percent)
                    previousPercent = percent
                }
            }
            val sha = digest.hexDigest()
            if (total != entry.size || (entry.sha256.isNotEmpty() && sha != entry.sha256)) {
                throw IOException("Integrity check failed for ${entry.path}.")
            }
            output.fd.sync()
            sha
        }
    }

    private fun open(url: URL): HttpURLConnection {
        var endpoint = url
        repeat(6) {
            if (endpoint.protocol != "https" && endpoint.protocol != "http") {
                throw IOException("Game asset URLs must use HTTP or HTTPS.")
            }
            val connection = (endpoint.openConnection() as HttpURLConnection).apply {
                instanceFollowRedirects = false
                connectTimeout = 15_000
                readTimeout = 30_000
            }
            var keepOpen = false
            try {
                val status = connection.responseCode
                if (status == HttpURLConnection.HTTP_OK) {
                    keepOpen = true
                    return connection
                }
                if (status !in REDIRECTS) throw IOException("Game assets CDN returned HTTP $status.")
                val location = connection.getHeaderField("Location")
                    ?: throw IOException("Missing game asset redirect.")
                val next = URL(endpoint, location)
                if (endpoint.protocol == "https" && next.protocol != "https") {
                    throw IOException("Game assets CDN redirected HTTPS to an insecure URL.")
                }
                endpoint = next
            } finally {
                if (!keepOpen) connection.disconnect()
            }
        }
        throw IOException("Too many game asset redirects.")
    }

    private companion object {
        const val TAG = "NativeAssetBridge"
        const val DEFAULT_CDN_ROOT = ""
        const val CONTENT_DIR = "game_files_android_mta"
        const val BUFFER_SIZE = 64 * 1024
        val GENERATION = Regex("([0-9a-f]{40}|local)-[0-9a-f-]{36}")
        val REDIRECTS = setOf(301, 302, 303, 307, 308)
        val installationLock = Mutex()
    }
}
