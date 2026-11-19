package com.worldrpg.neonlauncher.network

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.SocketTimeoutException

/**
 * ASE server query, ported from the desktop client:
 * - Port: game port + SERVER_LIST_QUERY_PORT_OFFSET (123) (Shared/sdk/Common.h:92,
 *   CServerListItem::GetQueryPort, CServerList.cpp:597-600).
 * - Request: the single byte "r" (CQueryReceiver::RequestQuery, CQueryReceiver.cpp:23-41).
 * - Reply: "EYE2" then length-prefixed strings and four bytes
 *   (CQueryReceiver::GetServerResponse, CQueryReceiver.cpp:85-237).
 * - Retry: a reply older than 4000 ms gets one resend and the query gives up at
 *   SERVER_LIST_ITEM_TIMEOUT (8000 ms) (CServerListItem::Pulse, CServerList.cpp:491-596;
 *   CServerList.h:41). The desktop TCP firewall-open step is not ported.
 */
object ServerQuery {

    const val QUERY_PORT_OFFSET = 123
    private const val BUFFER = 4096           // SERVER_LIST_QUERY_BUFFER
    private const val RESEND_MS = 4000
    private const val TIMEOUT_MS = 8000       // SERVER_LIST_ITEM_TIMEOUT

    data class Result(
        val gameName: String,
        val serverName: String,
        val gameType: String,
        val mapName: String,
        val version: String,
        val passworded: Boolean,
        val serials: Boolean,
        val players: Int,
        val maxPlayers: Int,
        val buildType: Int,
        val buildNumber: Int,
        val httpPort: Int,
        val playerNames: List<String>,
        val pingMs: Int
    )

    /** Null when the server did not answer or the reply was not a valid EYE2 packet. */
    suspend fun query(host: String, gamePort: Int): Result? = withContext(Dispatchers.IO) {
        runCatching {
            val address = InetAddress.getByName(host)
            require(gamePort in 1..65535)
            // Desktop GetQueryPort returns ushort, including wraparound at high game ports.
            val target = InetSocketAddress(address, (gamePort + QUERY_PORT_OFFSET) and 0xFFFF)
            DatagramSocket().use { socket ->
                val request = byteArrayOf('r'.code.toByte())
                val buf = ByteArray(BUFFER)
                val deadline = System.currentTimeMillis() + TIMEOUT_MS
                var sentAt = System.currentTimeMillis()
                var resent = false
                socket.send(DatagramPacket(request, request.size, target))
                while (true) {
                    val now = System.currentTimeMillis()
                    if (now >= deadline) return@runCatching null
                    val wait = if (!resent) (sentAt + RESEND_MS - now) else (deadline - now)
                    socket.soTimeout = wait.coerceAtLeast(1).toInt()
                    try {
                        val packet = DatagramPacket(buf, buf.size)
                        socket.receive(packet)
                        if (packet.address != address) continue
                        val ping = (System.currentTimeMillis() - sentAt).toInt()
                        parse(buf, packet.length, ping)?.let { return@runCatching it }
                    } catch (_: SocketTimeoutException) {
                        if (!resent) {
                            resent = true
                            sentAt = System.currentTimeMillis()
                            socket.send(DatagramPacket(request, request.size, target))
                        }
                    }
                }
                @Suppress("UNREACHABLE_CODE") null
            }
        }.getOrNull()
    }

    /** CQueryReceiver::ReadString: the length byte counts itself. */
    private class Reader(val b: ByteArray, val len: Int) {
        var i = 4
        fun readString(): ByteArray? {
            if (i <= len && i < b.size) {
                val n = b[i].toInt() and 0xFF
                if (i + n <= len && n > 0) {
                    val out = b.copyOfRange(i + 1, i + n)
                    i += n
                    return out
                }
                i++
            }
            return null
        }
    }

    internal fun parse(b: ByteArray, len: Int, ping: Int): Result? {
        if (len < 15) return null
        if (b[0] != 'E'.code.toByte() || b[1] != 'Y'.code.toByte() || b[2] != 'E'.code.toByte() || b[3] != '2'.code.toByte()) return null
        val r = Reader(b, len)
        val game = r.readString() ?: return null
        r.readString() ?: return null                  // port, ignored as on desktop
        val serverName = r.readString() ?: return null
        val gameType = r.readString() ?: return null
        val mapRaw = r.readString() ?: return null
        val version = r.readString() ?: return null
        if (r.i + 4 > len) return null
        val passworded = b[r.i++].toInt() == 1
        val serials = b[r.i++].toInt() == 1
        var players = b[r.i++].toInt() and 0xFF
        var maxPlayers = b[r.i++].toInt() and 0xFF

        // The map field carries NUL-separated extras: players/max, build type, build number,
        // ping status, net route, up time, http port (CQueryReceiver.cpp:146-198).
        val parts = splitNul(mapRaw)
        val mapName = parts.getOrNull(0)?.let(::ascii).orEmpty()
        parts.getOrNull(1)?.let(::ascii)?.takeIf { it.isNotEmpty() }?.split('/')?.takeIf { it.size == 2 }?.let { (j, m) ->
            j.trim().toIntOrNull()?.takeIf { it in 0..0xFFFF }?.let { players = it }
            m.trim().toIntOrNull()?.takeIf { it in 0..0xFFFF }?.let { maxPlayers = it }
        }
        val buildType = parts.getOrNull(2)?.let(::ascii)?.takeIf { it.isNotEmpty() }?.toIntOrNull() ?: 1
        val buildNumber = parts.getOrNull(3)?.let(::ascii)?.takeIf { it.isNotEmpty() }?.toIntOrNull() ?: 0
        // parts[4] ping status is checked by the closed net module on desktop (UpdatePingStatus); not ported.
        val httpPort = parts.getOrNull(7)?.let(::ascii)?.toIntOrNull()?.takeIf { it in 0..0xFFFF } ?: 0

        val names = ArrayList<String>()
        while (r.i < len) {
            val raw = r.readString() ?: continue
            val nick = ascii(raw)
            names += removeColorCodes(nick).ifEmpty { nick }
        }
        if (players > maxPlayers) players = maxPlayers

        return Result(
            gameName = ascii(game), serverName = ascii(serverName), gameType = ascii(gameType),
            mapName = mapName, version = ascii(version), passworded = passworded, serials = serials,
            players = players, maxPlayers = maxPlayers, buildType = buildType, buildNumber = buildNumber,
            httpPort = httpPort, playerNames = names, pingMs = ping
        )
    }

    /** Same pieces as the desktop strlen walk: split on each NUL. */
    private fun splitNul(raw: ByteArray): List<ByteArray> {
        val out = ArrayList<ByteArray>()
        var start = 0
        for (k in raw.indices) {
            if (raw[k].toInt() == 0) {
                out += raw.copyOfRange(start, k); start = k + 1
            }
        }
        out += raw.copyOfRange(start, raw.size)
        return out
    }

    private fun ascii(raw: ByteArray): String = String(raw, Charsets.ISO_8859_1)

    /** Desktop RemoveColorCodes: drops #RRGGBB. */
    private val colorCode = Regex("#[0-9A-Fa-f]{6}")
    fun removeColorCodes(s: String): String = s.replace(colorCode, "")
}
