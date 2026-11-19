package com.worldrpg.neonlauncher.network

import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.util.concurrent.atomic.AtomicInteger

private fun server(id: String = "test-server", endpoint: String = "192.0.2.1:22003"): JSONObject = JSONObject()
    .put("id", id).put("name", "Official name").put("tagline", "Test").put("description", "Description")
    .put("countries", JSONArray()).put("languages", JSONArray()).put("links", JSONArray())
    .put("endpoints", JSONArray().put(endpoint))

private fun catalogue(vararg servers: JSONObject): ByteArray = JSONObject().put("schema_version", 1)
    .put("servers", JSONArray(servers.toList())).toString().toByteArray()

private fun invalid(bytes: ByteArray) {
    check(runCatching { NeonServerRegistry.parse(bytes) }.isFailure) { "Accepted malformed registry" }
}

private fun List<ServerBrowserService.BrowserState>.lastServers() = filterIsInstance<ServerBrowserService.BrowserState.Success>().last().servers

fun main(args: Array<String>) = runBlocking {
    check(ServerAddress.normalizeHost("  EXAMPLE.org. ") == "example.org")
    check(ServerAddress.normalizeHost("192.168.001.010") == "192.168.1.10")
    for (host in listOf("", "256.1.2.3", "1.2.3", "http://example.org", "example.org/path", "example.org:22003", "bad host", "-bad.org", "bad..org", "[::1]")) {
        check(ServerAddress.normalizeHost(host) == null) { "Accepted host $host" }
    }
    for (port in listOf("", "0", "65536", "-1", "+22003", "22x03")) check(ServerAddress.parsePort(port) == null)
    check(ServerAddress.parsePort("65535") == 65535)
    check(ServerAddress.fromKey("EXAMPLE.org:22003") == ServerAddress("example.org", 22003))
    check(ServerAddress.fromKey("example.org:invalid") == null)

    val entries = NeonServerRegistry.parse(catalogue(server().put("endpoints", JSONArray().put("192.0.2.1:22003").put("192.0.2.1:22004"))))
    check(entries.size == 2 && entries.all { it.registryId == "test-server" && it.name == "Official name" })
    check(NeonServerRegistry.parse(catalogue()).isEmpty())
    for (endpoint in listOf("example.org:22003", "192.0.2.1:0", "192.0.2.1:65536", "999.0.2.1:22003", "192.000.2.1:22003")) {
        invalid(catalogue(server(endpoint = endpoint)))
    }
    invalid(catalogue(server(), server()))
    invalid(catalogue(server(), server(id = "other")))
    invalid(catalogue(server().put("name", 123)))
    invalid(catalogue(server().put("countries", JSONArray().put("BRA"))))
    invalid(catalogue(server().put("languages", JSONArray().put("English").put("English"))))
    invalid(catalogue(server().put("public_host", "127.0.0.1")))
    invalid(catalogue(server().put("accent", "red")))
    invalid(catalogue(server().put("logo_url", "http://example.org/image.png")))
    invalid("{\"schema_version\":2,\"servers\":[]}".toByteArray())
    invalid("{\"schema_version\":\"1\",\"servers\":[]}".toByteArray())
    invalid(catalogue(server()) + " extra".toByteArray())
    invalid(ByteArray(256 * 1024 + 1) { ' '.code.toByte() })
    invalid(byteArrayOf(0xC0.toByte(), 0xAF.toByte()))
    invalid(catalogue(*(0..256).map { server("server-$it", "192.0.2.1:${22003 + it}") }.toTypedArray()))
    val safeLogo = "https://identity.mta-neon.com/v1/server-registry/assets/" + "a".repeat(64)
    check(NeonServerRegistry.parse(catalogue(server().put("logo_url", safeLogo))).single().logoUrl == safeLogo)
    check(NeonServerRegistry.parse(catalogue(server().put("logo_url", "https://example.org/image.png"))).single().logoUrl.isEmpty())

    val queried = ServerQuery.Result("mta", "Untrusted ASE name", "Race", "Map", "1.7", false, false, 3, 64, 1, 1, 22005, emptyList(), 42)
    val service = ServerBrowserService(fetchRegistry = { entries }, queryServer = { _, _ -> delay(2); queried })
    val states = service.fetchServerList(setOf(entries.first().getKey())).toList()
    val snapshots = states.filterIsInstance<ServerBrowserService.BrowserState.Success>()
    check(snapshots.first().servers.all { it.status == ServerInfo.Status.QUERYING })
    check(snapshots.last().servers.all { it.status == ServerInfo.Status.ONLINE && it.players == 3 && it.name == "Official name" })
    check(snapshots.last().servers.first().isFavorite)

    var mode = 0
    val cached = ServerBrowserService(fetchRegistry = {
        when (mode) { 0 -> entries; 1 -> emptyList(); else -> throw IOException("offline") }
    }, queryServer = { _, _ -> null })
    cached.fetchServerList().toList()
    mode = 2
    val fallback = cached.fetchServerList().toList()
    check(fallback.any { it is ServerBrowserService.BrowserState.Error })
    check(fallback.lastServers().map { it.getKey() } == entries.map { it.getKey() })
    mode = 1
    check(cached.fetchServerList().toList().lastServers().isEmpty())
    mode = 2
    check(cached.fetchServerList().toList().lastServers().isEmpty()) { "Empty catalogue resurrected expired servers" }
    val unavailable = ServerBrowserService(fetchRegistry = { throw IOException("offline") }, queryServer = { _, _ -> null })
    check(unavailable.fetchServerList(setOf("192.0.2.5:22003", "corrupt")).toList().lastServers().size == 2)

    val active = AtomicInteger()
    val peak = AtomicInteger()
    val many = (1..40).map { ServerInfo("Server $it", "192.0.2.$it", 22003) }
    val bounded = ServerBrowserService(fetchRegistry = { many }, queryServer = { _, _ ->
        val count = active.incrementAndGet()
        peak.updateAndGet { old -> maxOf(old, count) }
        try { delay(5); null } finally { active.decrementAndGet() }
    })
    check(bounded.fetchServerList().toList().lastServers().size == 40)
    check(peak.get() in 2..12 && active.get() == 0)
    val cancelled = mutableListOf<ServerBrowserService.BrowserState>()
    val waiting = ServerBrowserService(fetchRegistry = { delay(1000); entries })
    check(withTimeoutOrNull(20) { waiting.fetchServerList().toList(cancelled) } == null)
    check(cancelled.none { it is ServerBrowserService.BrowserState.Error })

    if (args.isNotEmpty()) {
        val live = NeonServerRegistry.parse(File(args[0]).readBytes())
        check(live.isNotEmpty())
        println("PASS: actual Neon catalogue contains ${live.size} endpoints")
    }
    println("PASS: endpoint validation, registry validation, progressive queries, favourites, cache, empty refresh, cancellation and query limits")
}
