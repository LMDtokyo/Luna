# L4 — Canonical Rename Map

*Document version: 2026-04-18. Scope: user-facing stdlib surface of Luna v4.x.*

---

## 1. Rationale

Luna's existing control-flow keywords already form a small cosmic vocabulary
(`shine`, `eclipse`, `orbit`, `phase`, `nova`, `guard`, `meow`, `seal`,
`spawn`). That vocabulary works because every word carries a picture: `shine`
is a point of emitted light, `eclipse` is a branch where one path darkens the
other, `orbit` is a guarded loop. The stdlib today does **not** carry that
picture forward — it reads as a thin skin over POSIX (`tcp_connect`,
`mutex_lock`, `regex_compile`, `atomic_fetch_add`). A programmer who has
never seen Luna cannot tell from a `.luna` listing that they are in a new
language.

This map fixes that. It promotes the cosmic metaphor from *syntax* into the
*library surface* so that reading any Luna program — TCP client, telegram
bot, sha256 helper — immediately feels like Luna and not like a repackaged
libc. The guiding axes are:

- **Light** for state that is observed (logs, prints, channels, events).
  `shine`, `beam`, `pulse`, `flare`, `glow`.
- **Gravitation / orbit** for I/O and networking (sockets, channels to a
  peer). `orbit`, `anchor`, `tether`, `gateway`, `signal`.
- **Celestial bodies** for structural primitives (connection ≈ star, server ≈
  constellation, mutex ≈ halo). `star`, `moon`, `halo`, `ring`, `comet`.
- **Time/phase** for moments and durations. `moment`, `phase`, `arc`, `epoch`.
- **Crypto / dark matter** for anything opaque. `veil`, `aether`, `sigil`,
  `cipher`, `seal`.
- **Storage / gravity wells** for persistence. `vault`, `archive`, `seed`,
  `stardust`.
- **Result / optional** becomes `phase<T>` with two moon-phases:
  `Bright(v)` and `Dim(e)`. One data type hints at the truth that a result
  is binary — success is a full moon, failure is a new moon — and replaces
  four different spellings (`Result`, `Option`, error codes, `-1`) with one.

Every rename below preserves the original's arity and ordering; nothing is a
semantic change. A mechanical pass can execute this map.

---

## 2. Module renames

The module tree moves from POSIX/Go names to cosmic roles. File paths stay
`src/stdlib/*.luna`; only the import name and the module docstring change.

| old module | new module | role (one-line)                                                      |
|------------|------------|----------------------------------------------------------------------|
| `io`       | `scroll`   | files, handles, streams — a scroll is something you read or write.   |
| `net`      | `orbit`    | TCP/UDP/TLS/WS — every connection is an orbit around a remote body.  |
| `http`     | `beam`     | HTTP client+server — a beam carries a request, gets a beam back.     |
| `sync`     | `halo`     | mutex/rwlock/atomic/channel — protection that rings a resource.      |
| `scheduler`| `nebula`   | tasks, fibers, workers — a cloud of work.                            |
| `crypto`   | `veil`     | hashes, ciphers, signatures — things that hide or authenticate.      |
| `time`     | `chrono`   | instants, durations, wheels. (kept Greek-adjacent; still cosmic.)    |
| `json`     | `lumen`    | structured light — the serialization format Luna speaks to the web.  |
| `regex`    | `glyph`    | patterns carved into text.                                           |
| `db`       | `vault`    | key/value storage — a sealed vault of stardust.                      |
| `collections` | `constellation` | lists, maps, sets — fixed patterns of points.                  |
| `math`     | `helix`    | math lives here; kept short and non-Greek to avoid collision.        |

The kernel/runtime internals (`kernel.luna`, `runtime_core.luna`,
`llvm_api.luna`, `lidl.luna`, `luna_pkg.luna`) keep their names — they are
not user-facing.

Import statements therefore change like:

```luna
# before
import net
import http
import sync
import scheduler

# after
import orbit
import beam
import halo
import nebula
```

---

## 3. New primitive types

These replace ad-hoc `int` sentinels (`-1`, `0`, `-EAGAIN`) scattered across
the stdlib.

### 3.1 `phase<T>` — the two-phase result

```luna
phase<T>
    Bright(value: T)      # success — full moon
    Dim(err: Fault)       # failure — new moon
```

- Pattern-matched with the existing `phase` control-flow keyword (no new
  syntax needed — the type name and the match keyword are deliberately the
  same word, reinforcing "a phase has exactly two sides").
- Replaces: `Result<T,E>`, stdlib returns of `-1`/`0`, `json_is_valid`-style
  `1/0` pairs, `fn ... -> int` error codes.
- Unwrapping helpers: `.bright()` → T (panics on Dim), `.dim()` → Fault,
  `.is_bright()`, `.is_dim()`.

### 3.2 `seen<T>` — presence or absence

```luna
seen<T>
    Here(v: T)
    Gone
```

- Replaces `Option<T>`, `null`, `*T` where nullability is semantic.
- The name evokes *observed* vs *not yet observed*, which is closer to
  Luna's astronomical framing than "Some/None".

### 3.3 `Fault` — the unified error shape

```luna
struct Fault
    code: int          # cosmic error category
    kind: FaultKind    # enum: IO, Net, Parse, Time, Crypto, Vault, Logic
    msg: str           # short human text (up to 256 bytes)
    source: seen<Fault># optional wrapped cause
```

- Replaces `IO_ERR_*`, `HTTP_ERROR`, `SOCK_ERROR`, `DB_ERROR` and the zoo of
  negative integer constants.
- `fault.trace()` prints the source chain; no exceptions, no stack unwind.

### 3.4 `halo` — lock handle (formerly `Mutex`)

- A `halo` is *acquired* (`halo.hold`) and *released* (`halo.release`). The
  RAII-style `halo.within { ... }` block auto-releases on scope exit.
- `halo.dim()` is a read-lock (borrowing light); `halo.dark()` is a
  write-lock (eclipse). This replaces the `RwLock` pair.

### 3.5 `pulse<T>` — channel (formerly `Channel`)

- A pulse transmits a value from sender to receiver. API: `pulse.emit(v)`,
  `pulse.catch() -> phase<T>`, `pulse.seal()`. Bounded and unbounded variants
  differ only by construction: `pulse.bounded(cap)` vs `pulse.open()`.

### 3.6 `moment` and `arc`

- `moment` = a point in time (replaces `Instant`, `SystemTime` merged).
- `arc` = a duration (replaces `Duration`). "An arc of ten seconds" reads
  cleaner than "a duration of ten seconds".

### 3.7 `star` / `constellation`

- `star` = one TCP/TLS connection. Replaces `TcpStream`, `TlsStream` which
  become one polymorphic handle with `star.secure` boolean.
- `constellation` = a server / listener. `constellation.rise(port)` begins
  listening; `constellation.catch()` accepts a new `star`.

### 3.8 `scroll` — file handle

- A `scroll` is opened, read, written, rolled (seeked), sealed (closed).
  Replaces `FileHandle`.

### 3.9 `sigil<N>` — fixed-size byte hash

- `sigil<32>` is a SHA-256 hash. `sigil<64>` is BLAKE3-512. Replaces the
  current convention of returning `*u8` + length. Formats as hex with
  `.hex()`.

### 3.10 `beacon` — atomic integer (formerly `AtomicInt`)

- A beacon flashes a value visible to all observers. `beacon.read`,
  `beacon.flash(v)`, `beacon.bump(delta)`, `beacon.compare_flash(old,new)`.

---

## 4. Function / type renames per module

Notation: `old → new — note`. Parameters omitted when unchanged.

### 4.1 `orbit` (was `net`)

#### 4.1.1 TCP — "links" in orbit

| old name                  | new name              | notes                                                                    |
|---------------------------|-----------------------|--------------------------------------------------------------------------|
| `tcp_connect`             | `orbit.link`          | A client establishes a link toward a peer body.                          |
| `tcp_listen`              | `orbit.anchor`        | A server anchors itself at a port; peers orbit around it.                |
| `tcp_accept`              | `orbit.catch`         | Anchor catches an arriving star (client connection).                     |
| `tcp_send`                | `star.emit`           | A star emits bytes outward.                                              |
| `tcp_recv`                | `star.receive`        | A star receives bytes inward.                                            |
| `tcp_close`               | `star.extinguish`     | Closing a connection is a star winking out.                              |
| `tcp_set_timeout`         | `star.set_arc`        | Timeout is "how long before the arc ends".                               |
| `tcp_is_connected`        | `star.is_lit`         | Binary "is this thing still shining at me?"                              |
| `tcp_remote_addr`         | `star.peer_coords`    | Remote address = coordinates of the other body.                          |
| `tcp_remote_port`         | `star.peer_port`      | Unchanged idea; `peer_` prefix groups the pair.                          |
| `tcp_local_port`          | `star.home_port`      | Local port = port anchored on our side.                                  |
| `tcp_listener_close`      | `constellation.fade`  | A listener fades; different metaphor from star extinguishing.            |
| `tcp_listener_port`       | `constellation.port`  |                                                                          |
| `tcp_listener_addr`       | `constellation.addr`  |                                                                          |

#### 4.1.2 TLS — "secured stars"

| old                   | new                    | notes                                                                   |
|-----------------------|------------------------|-------------------------------------------------------------------------|
| `tls_connect`         | `orbit.link_secure`    | Same concept as `link`, but veiled. Takes ALPN as last arg.             |
| `tls_accept`          | `orbit.catch_secure`   | Mirror of `link_secure`.                                                |
| `tls_handshake`       | `star.commune`         | The handshake is a ceremony two stars perform before speaking.          |
| `tls_send`            | `star.emit`            | Same function; the `star.secure` flag routes TLS vs plain.              |
| `tls_recv`            | `star.receive`         | Same function.                                                          |
| `tls_close`           | `star.extinguish`      | Same.                                                                   |
| `tls_set_timeout`     | `star.set_arc`         | Same.                                                                   |
| `tls_peer_cn`         | `star.peer_name`       | Peer certificate common name.                                           |
| `tls_alpn_selected`   | `star.alpn`            | Short; the word ALPN is specific enough.                                |
| `tls_is_valid`        | `star.is_trusted`      | Name hints at *why* you'd check — cert validation.                      |
| `tls_last_error`      | `star.last_fault`      | Returns a `Fault`, not a string buffer.                                 |

#### 4.1.3 UDP — "drifts" (no orbit, just broadcast)

| old                    | new                    | notes                                                                  |
|------------------------|------------------------|------------------------------------------------------------------------|
| `udp_bind`             | `drift.anchor`         | UDP doesn't link, it drifts; but you still anchor a local port.        |
| `udp_send_to`          | `drift.cast`           | "Cast a datagram toward this address."                                 |
| `udp_recv_from`        | `drift.gather`         | Gather one datagram.                                                   |
| `udp_close`            | `drift.release`        | Drift has no connection to tear down, just released.                   |
| `udp_set_timeout`      | `drift.set_arc`        |                                                                        |
| `udp_set_broadcast`    | `drift.broadcast`      | Kept as-is; "broadcast" is already astronomical.                       |

#### 4.1.4 WebSockets — "pulses over stars"

| old              | new                         | notes                                                                |
|------------------|-----------------------------|----------------------------------------------------------------------|
| `ws_connect`     | `cascade.link`              | A `cascade` is a bi-directional pulse chain. Reads as one unit.      |
| `wss_connect`    | `cascade.link_secure`       | TLS variant.                                                         |
| `ws_send_text`   | `cascade.emit_text`         |                                                                      |
| `ws_send_binary` | `cascade.emit_binary`       |                                                                      |
| `ws_send_ping`   | `cascade.ping`              | Kept — `ping` is already a bounce off a surface.                     |
| `ws_recv`        | `cascade.catch`             | Catch a frame; returns `phase<Frame>`.                               |
| `ws_frame_opcode`| `frame.kind`                | Frame is its own small struct now.                                   |
| `ws_frame_payload_ptr` | `frame.payload`       | Returns a slice.                                                     |
| `ws_frame_payload_len` | (merged into `frame.payload`) | Slices carry length.                                         |
| `ws_close`       | `cascade.seal`              | The cascade is sealed when done.                                     |
| `ws_listen`      | `cascade.anchor`            | Server.                                                              |
| `ws_accept`      | `cascade.catch_client`      | Server accepts incoming cascade.                                     |
| `ws_broadcast`   | `cascade.pulse_all`         | Push frame to every anchored client.                                 |
| `ws_server_close`| `cascade.fade`              |                                                                      |

#### 4.1.5 Raw sockets / packet building

| old                    | new                    | notes                                                                  |
|------------------------|------------------------|------------------------------------------------------------------------|
| `raw_socket_create`    | `ether.open`           | `ether` namespace for layer-3 raw access.                              |
| `raw_socket_send`      | `ether.cast`           |                                                                        |
| `raw_socket_recv`      | `ether.gather`         |                                                                        |
| `raw_socket_close`     | `ether.close`          |                                                                        |
| `packet_build_tcp`     | `ether.forge_tcp`      | "Forge" hints it's a low-level construction, not normal send.          |
| `packet_build_udp`     | `ether.forge_udp`      |                                                                        |
| `packet_build_icmp`    | `ether.forge_icmp`     |                                                                        |
| `packet_build_ping`    | `ether.forge_ping`     |                                                                        |
| `packet_parse`         | `ether.read_packet`    |                                                                        |
| `packet_get_field`     | `ether.field`          |                                                                        |
| `packet_checksum`      | `ether.checksum`       | Kept.                                                                  |

#### 4.1.6 DNS / IP

| old              | new              | notes                                                                 |
|------------------|------------------|-----------------------------------------------------------------------|
| `luna_dns_resolve` | `orbit.locate` | "Find the coordinates of this host."                                  |
| `luna_ip_parse`    | `coords.parse` | IP address lives under the `coords` namespace.                        |
| `luna_ip_to_string`| `coords.show`  |                                                                       |
| `luna_ip_in_cidr`  | `coords.inside`| "Are these coords inside this region?"                                |

---

### 4.2 `beam` (was `http`)

#### 4.2.1 Client

| old                 | new                 | notes                                                                  |
|---------------------|---------------------|------------------------------------------------------------------------|
| `http_request`      | `beam.send`         | You send a beam and receive a beam back.                               |
| `http_get`          | `beam.fetch`        | Reads better than `get`; implies retrieval.                            |
| `http_post`         | `beam.push`         | Reads as sending energy outward.                                       |
| `http_post_json`    | `beam.push_lumen`   | Ties JSON module's new name into the verb.                             |
| `url_parse`         | `beam.parse_url`    | URL stays — it's a real thing.                                         |
| `HttpResponse`      | `Echo`              | The response is the echo of your beam.                                 |
| `ParsedUrl`         | `BeamTarget`        |                                                                        |
| `http_method_name`  | *internal*          | Stays internal.                                                        |
| `http_parse_response` | `beam.read_echo`  | Parses raw bytes into an `Echo`.                                       |

#### 4.2.2 Server

| old                  | new                   | notes                                                                 |
|----------------------|-----------------------|-----------------------------------------------------------------------|
| `http_parse_request` | `beam.read_incoming`  | Parses bytes into an `Incoming` struct.                               |
| `http_reset_request` | `beam.reset_incoming` |                                                                       |
| `http_reset_response`| `beam.reset_outgoing` |                                                                       |
| `http_response_new`  | `beam.reply`          | `reply(status)` — starts building a response.                         |
| `http_response_header` | `reply.headband`    | Single header append. "Headband" keeps the *header* metaphor while    |
|                      |                       | being distinct.                                                       |
| `http_response_body` | `reply.carry`         | The body is what the beam carries.                                    |
| `http_response_text` | `reply.text`          |                                                                       |
| `http_response_html` | `reply.html`          |                                                                       |
| `http_response_json` | `reply.lumen`         | Matches `json` module rename.                                         |
| `http_response_serialize` | `reply.shine`    | Serialise the outgoing beam to bytes (shine it out).                  |
| `route_add`          | `beam.route`          | `beam.route(method, pattern, handler)`.                               |
| `route_match`        | `beam.dispatch`       | Returns handler id for current incoming.                              |
| `http_get_method`    | `incoming.method`     | Struct field, no function call.                                       |
| `http_get_path`      | `incoming.path`       |                                                                       |
| `http_get_header`    | `incoming.headband`   | Paired verb with `reply.headband`.                                    |
| `http_get_query_param` | `incoming.query`    |                                                                       |
| `http_get_cookie`    | `incoming.crumb`      | Cookies are *crumbs* — cosmic-adjacent, memorable.                    |
| `http_get_body`      | `incoming.body`       |                                                                       |
| `http_get_path_param`| `incoming.param`      |                                                                       |
| `http_get_header_count` | `incoming.headband_count` |                                                                 |
| `http_get_query_count` | `incoming.query_count` |                                                                    |
| `http_get_cookie_count`| `incoming.crumb_count` |                                                                    |
| `url_decode`         | `beam.unseal_url`     |                                                                       |
| `url_encode`         | `beam.seal_url`       |                                                                       |
| `content_type_for_ext` | `beam.mime_for`     | "MIME" is widely-understood; `content_type_for_ext` is awkward.       |
| `content_type_str`   | `beam.mime_show`      |                                                                       |
| `status_text`        | `beam.status_show`    |                                                                       |
| `METHOD_GET` .. `METHOD_OPTIONS` | `Method.Get` .. `Method.Options` | Enum on a `Method` type, not bare ints.            |
| `STATUS_OK` etc.     | `Status.Ok` etc.      | Same.                                                                 |

---

### 4.3 `scroll` (was `io`)

#### 4.3.1 Core handle API

| old              | new                  | notes                                                                   |
|------------------|----------------------|-------------------------------------------------------------------------|
| `io_open`        | `scroll.unroll`      | Opening a scroll is unrolling it.                                       |
| `io_close`       | `scroll.reroll`      | Inverse of unroll.                                                      |
| `io_read`        | `scroll.read`        | Kept — already matches.                                                 |
| `io_write`       | `scroll.write`       | Kept.                                                                   |
| `io_seek`        | `scroll.skip_to`     | "Seek" reads like a search; `skip_to` is what it does.                  |
| `io_flush`       | `scroll.flush`       | Kept.                                                                   |
| `io_sync`        | `scroll.commit`      | fsync is committing to stone.                                           |
| `io_truncate`    | `scroll.cut`         | Truncating is literally cutting the scroll.                             |
| `FileHandle`     | `Scroll`             | The primary type.                                                       |
| `FileInfo`       | `ScrollStat`         |                                                                         |

#### 4.3.2 Path-level operations

| old              | new                  | notes                                                                   |
|------------------|----------------------|-------------------------------------------------------------------------|
| `io_exists`      | `scroll.exists`      |                                                                         |
| `io_is_file`     | `scroll.is_scroll`   | Self-referential; reads well.                                           |
| `io_is_dir`      | `scroll.is_library`  | A directory is a library of scrolls.                                    |
| `io_file_size`   | `scroll.weight`      | Size → weight; tangible.                                                |
| `io_file_info`   | `scroll.inspect`     | Returns `ScrollStat`.                                                   |
| `io_mkdir`       | `library.build`      | Directory ops move under `library` namespace.                           |
| `io_rmdir`       | `library.burn`       | Remove a directory = burn the library (empty).                          |
| `io_readdir`     | `library.list`       |                                                                         |
| `io_list_dir`    | `library.list`       | Merge with `readdir`.                                                   |
| `io_rename`      | `scroll.rename`      |                                                                         |
| `io_remove`      | `scroll.burn`        | Delete a file = burn it.                                                |
| `io_copy`        | `scroll.copy`        |                                                                         |
| `io_read_file`   | `scroll.slurp`       | Read whole file; the Ruby-ism `slurp` is fine — it's one gulp.          |
| `io_write_file`  | `scroll.spill`       | Write whole buffer to path.                                             |
| `io_append_file` | `scroll.append`      |                                                                         |
| `io_read_lines`  | `scroll.lines`       | Returns iterable of lines.                                              |

#### 4.3.3 Buffered streams

| old                        | new                       | notes                                                   |
|----------------------------|---------------------------|---------------------------------------------------------|
| `buffered_reader_new`      | `stream.intake`           | Input stream. An intake collects light.                 |
| `buffered_reader_read`     | `intake.read`             |                                                         |
| `buffered_reader_read_line`| `intake.line`             |                                                         |
| `buffered_reader_destroy`  | `intake.close`            |                                                         |
| `buffered_writer_new`      | `stream.outflow`          | Output stream.                                          |
| `buffered_writer_write`    | `outflow.write`           |                                                         |
| `buffered_writer_write_str`| `outflow.write_str`       |                                                         |
| `buffered_writer_write_line`| `outflow.line`           |                                                         |
| `buffered_writer_flush`    | `outflow.flush`           |                                                         |
| `buffered_writer_destroy`  | `outflow.close`           |                                                         |

#### 4.3.4 Standard streams

| old                 | new                      | notes                                                       |
|---------------------|--------------------------|-------------------------------------------------------------|
| `stdin_read`        | `whisper.read`           | Input from the user is a whisper to the program.            |
| `stdin_read_line`   | `whisper.line`           |                                                             |
| `stdout_write`      | `shine.bytes`            | `shine()` already exists for printing; extend with variants.|
| `stdout_write_str`  | `shine` (kept)           | The canonical print call — already cosmic.                  |
| `stdout_write_int`  | `shine.int`              |                                                             |
| `stderr_write`      | `flare.bytes`            | `flare` = distress signal, the error channel.               |
| `stderr_write_str`  | `flare`                  | `flare(msg)` is the stderr equivalent of `shine(msg)`.      |

#### 4.3.5 RAM filesystem

| old prefix `ramfs_*` | new namespace `cache.*` | A RAM-only filesystem is an in-memory cache. |

So `ramfs_open` → `cache.unroll`, `ramfs_mkdir` → `cache.build`, etc.
The full list mirrors the `scroll.*`/`library.*` API one-for-one.

---

### 4.4 `halo` (was `sync`)

#### 4.4.1 Mutexes

| old                      | new                 | notes                                                              |
|--------------------------|---------------------|--------------------------------------------------------------------|
| `Mutex`                  | `halo`              | The type itself.                                                   |
| `mutex_new`              | `halo.new`          |                                                                    |
| `mutex_lock`             | `halo.hold`         | You *hold* a halo around the resource.                             |
| `mutex_try_lock`         | `halo.try_hold`     | Returns `phase<()>` not `int`.                                     |
| `mutex_unlock`           | `halo.release`      |                                                                    |
| `mutex_is_locked`        | `halo.held`         |                                                                    |
| *new*                    | `halo.within(fn)`   | RAII-style scope block, auto-releases on exit.                     |

#### 4.4.2 RW locks

| old                   | new               | notes                                                                  |
|-----------------------|-------------------|------------------------------------------------------------------------|
| `RwLock`              | `halo` (same type with `.dim()` / `.dark()` methods) | One type, two lock modes.           |
| `rwlock_new`          | `halo.new_shared` | Returns a halo that permits shared reads.                              |
| `rwlock_read_lock`    | `halo.dim`        | A dim hold — light still shines through for other readers.             |
| `rwlock_read_unlock`  | `halo.undim`      |                                                                        |
| `rwlock_write_lock`   | `halo.dark`       | A dark hold — eclipses all readers and writers.                        |
| `rwlock_write_unlock` | `halo.undark`     |                                                                        |
| `rwlock_try_read_lock`| `halo.try_dim`    |                                                                        |
| `rwlock_try_write_lock`| `halo.try_dark`  |                                                                        |

#### 4.4.3 Spinlocks and flags

| old                      | new               | notes                                                               |
|--------------------------|-------------------|---------------------------------------------------------------------|
| `SpinLock`               | `spark`           | A spinlock spins fast like a spark.                                 |
| `spinlock_new`           | `spark.new`       |                                                                     |
| `spinlock_lock`          | `spark.catch`     |                                                                     |
| `spinlock_try_lock`      | `spark.try_catch` |                                                                     |
| `spinlock_unlock`        | `spark.drop`      |                                                                     |
| `spinlock_is_locked`     | `spark.held`      |                                                                     |
| `AtomicFlag`             | `wink`            | A flag that blinks 0/1.                                             |
| `atomic_flag_new`        | `wink.new`        |                                                                     |
| `atomic_flag_test_and_set`| `wink.flash`     | Atomic test-and-set = flash the wink.                               |
| `atomic_flag_clear`      | `wink.dark`       |                                                                     |
| `atomic_flag_load`       | `wink.read`       |                                                                     |

#### 4.4.4 Atomic integer

| old                          | new                    | notes                                                        |
|------------------------------|------------------------|--------------------------------------------------------------|
| `AtomicInt`                  | `beacon`               | A beacon shows a value to every observer.                    |
| `atomic_new`                 | `beacon.new`           |                                                              |
| `atomic_load`                | `beacon.read`          |                                                              |
| `atomic_store`               | `beacon.flash`         |                                                              |
| `atomic_fetch_add`           | `beacon.bump`          | `bump(n)` returns old value, adds `n`.                       |
| `atomic_fetch_sub`           | `beacon.drain`         |                                                              |
| `atomic_fetch_max`           | `beacon.raise`         |                                                              |
| `atomic_fetch_min`           | `beacon.lower`         |                                                              |
| `atomic_compare_exchange`    | `beacon.swap_if`       | CAS; second value is the expected sigil.                     |
| `atomic_compare_exchange_weak`| `beacon.swap_if_weak` |                                                              |
| `atomic_swap`                | `beacon.swap`          |                                                              |
| `atomic_fetch_and`           | `beacon.mask_and`      |                                                              |
| `atomic_fetch_or`            | `beacon.mask_or`       |                                                              |
| `atomic_fetch_xor`           | `beacon.mask_xor`      |                                                              |
| `memory_fence`               | `halo.fence`           | Memory fence is a halo-level operation (global barrier).     |
| `ORDER_RELAXED`..`ORDER_SEQ_CST` | `Tide.Calm`, `Tide.Pull`, `Tide.Push`, `Tide.Both`, `Tide.Strict` | Ordering = tide strength. |

#### 4.4.5 Channels

| old                     | new                  | notes                                                           |
|-------------------------|----------------------|-----------------------------------------------------------------|
| `Channel`               | `pulse`              | See 3.5.                                                        |
| `channel_new`           | `pulse.bounded`      | Takes capacity.                                                 |
| `channel_send`          | `pulse.emit`         | Blocking send.                                                  |
| `channel_try_send`      | `pulse.try_emit`     |                                                                 |
| `channel_recv`          | `pulse.catch`        | Blocking receive. Returns `phase<T>`.                           |
| `channel_try_recv`      | `pulse.try_catch`    |                                                                 |
| `channel_close`         | `pulse.seal`         |                                                                 |
| `channel_is_closed`     | `pulse.sealed`       |                                                                 |
| `channel_len`           | `pulse.depth`        |                                                                 |
| `channel_is_empty`      | `pulse.quiet`        |                                                                 |
| `channel_is_full`       | `pulse.saturated`    |                                                                 |
| `UnboundedChannel`      | `pulse` (same type, `.open()` constructor) | One type, capacity-or-infinite.           |
| `unbounded_channel_new` | `pulse.open`         |                                                                 |
| `unbounded_channel_send`| `pulse.emit`         | Same method.                                                    |
| `unbounded_channel_recv`| `pulse.catch`        | Same method.                                                    |
| `unbounded_channel_close`| `pulse.seal`        |                                                                 |
| `unbounded_channel_len` | `pulse.depth`        |                                                                 |
| `unbounded_channel_is_closed` | `pulse.sealed`  |                                                                 |

#### 4.4.6 Condvar, Once, Barrier, Latch

| old                    | new                 | notes                                                              |
|------------------------|---------------------|--------------------------------------------------------------------|
| `Condvar`              | `herald`            | A herald announces a condition.                                    |
| `condvar_new`          | `herald.new`        |                                                                    |
| `condvar_wait`         | `herald.await`      | Takes the halo to release while waiting.                           |
| `condvar_wait_timeout` | `herald.await_for`  | With an `arc` timeout.                                             |
| `condvar_signal`       | `herald.call`       | Wakes one waiter.                                                  |
| `condvar_broadcast`    | `herald.cry`        | Wakes all.                                                         |
| `OnceCell`             | `dawn`              | Computed exactly once — like sunrise.                              |
| `once_new`             | `dawn.new`          |                                                                    |
| `once_set`             | `dawn.break`        | "Break of dawn" — set the value.                                   |
| `once_get`             | `dawn.peek`         |                                                                    |
| `once_get_or_init`     | `dawn.rise`         | Compute-and-store pattern.                                         |
| `Barrier`              | `meridian`          | A barrier where N runners must converge.                           |
| `barrier_new`          | `meridian.new`      |                                                                    |
| `barrier_wait`         | `meridian.cross`    |                                                                    |
| `Latch`                | `gate`              | Countdown latch.                                                   |
| `latch_new`            | `gate.new`          |                                                                    |
| `latch_count_down`     | `gate.fall`         |                                                                    |
| `latch_wait`           | `gate.await`        |                                                                    |
| `latch_get_count`      | `gate.remaining`    |                                                                    |

---

### 4.5 `nebula` (was `scheduler`)

#### 4.5.1 Top-level scheduler

| old                        | new                       | notes                                                      |
|----------------------------|---------------------------|------------------------------------------------------------|
| `sched_init`               | `nebula.ignite`           | Bring the scheduler online.                                |
| `sched_init_per_core`      | `nebula.ignite_per_core`  |                                                            |
| `spawn` *(stdlib fn)*      | `spawn` (kept — keyword)  | The keyword `spawn` stays. The fn wrapper is `nebula.spawn`.|
| `spawn_priority`           | `nebula.spawn_at`         | "Spawn at priority P"                                      |
| `spawn_cancellable`        | `nebula.spawn_cancelable` |                                                            |
| `spawn_blocking`           | `nebula.spawn_grounded`   | Blocking tasks live on a "grounded" (non-fiber) thread.    |
| `await_task`               | `task.await`              | Method on the task handle.                                 |
| `run_until_complete`       | `nebula.orbit_forever`    | Run the scheduler until every task lands.                  |
| `active_tasks`             | `nebula.active`           |                                                            |
| `total_spawned`            | `nebula.spawned_total`    |                                                            |
| `num_workers`              | `nebula.workers`          |                                                            |
| `cpu_count`                | `nebula.cores`            |                                                            |
| `steal_count`              | `nebula.heists`           | Work-stealing = heists from other workers; whimsical but memorable. |
| `steal_rate`               | `nebula.heist_rate`       |                                                            |
| `sched_shutdown`           | `nebula.collapse`         | The nebula collapses at shutdown.                          |
| `sched_yield`              | `drift`                   | Yielding your time slice = drifting.                       |
| `sleep_ms`                 | `slumber(@ms)`            | Free function; fibers and threads alike.                   |

#### 4.5.2 Tasks

| old              | new                | notes                                                            |
|------------------|--------------------|------------------------------------------------------------------|
| `Task` (struct)  | `Comet`            | A moving task with a trail (state history).                      |
| `task_get_state` | `comet.state`      |                                                                  |
| `task_is_ready`  | `comet.ready`      |                                                                  |
| `task_complete`  | `comet.land`       | A completed task has landed.                                     |
| `task_wait`      | `comet.await`      |                                                                  |
| `task_get_result`| `comet.result`     | Returns `phase<T>`.                                              |
| `task_get_id`    | `comet.id`         |                                                                  |
| `task_get_priority` | `comet.priority`|                                                                  |
| `STATE_READY` .. `STATE_FAILED` | `CometState.{Ready,Flying,Stalled,Landed,Vanished,Fallen}` | One enum. |
| `PRIORITY_*`     | `Priority.{Faint,Normal,Bright,Supernova}` | Priority = brightness.                         |

#### 4.5.3 Fibers / threads / workers

| old                  | new                  | notes                                                          |
|----------------------|----------------------|----------------------------------------------------------------|
| `Fiber`              | `filament`           | A fiber is a filament of light inside the nebula.              |
| `fiber_new`          | `filament.new`       |                                                                |
| `fiber_pool_acquire` | `filament.draw`      | Draw a filament from the pool.                                 |
| `fiber_pool_release` | `filament.return`    |                                                                |
| `FIBER_READY`..`FIBER_COMPLETED` | `FilamentState.*` |                                                    |
| `Thread`             | `strand`             | An OS thread is a heavier strand.                              |
| `thread_new`         | `strand.new`         |                                                                |
| `thread_start`       | `strand.ignite`      |                                                                |
| `thread_join`        | `strand.gather`      | Join = gather back in.                                         |
| `thread_suspend`     | `strand.freeze`      |                                                                |
| `thread_resume`      | `strand.thaw`        |                                                                |
| `thread_terminate`   | `strand.sever`       |                                                                |
| `thread_yield`       | `drift`              | Same free fn as `sched_yield`.                                 |
| `thread_sleep_ms`    | `slumber`            |                                                                |
| `thread_cpu_count`   | `nebula.cores`       |                                                                |
| `Worker`             | `drone`              | Internal — kept for completeness. Workers are the nebula's     |
|                      |                      | drones that pull comets from queues.                           |

---

### 4.6 `veil` (was `crypto`)

#### 4.6.1 Hashes

| old              | new                  | notes                                                              |
|------------------|----------------------|--------------------------------------------------------------------|
| `sha256`         | `veil.sha256`        | Returns `sigil<32>`.                                               |
| `sha256_hex`     | `sigil.hex`          | Method on `sigil<N>`; works for every hash width.                  |
| `sha1`           | `veil.sha1`          | Returns `sigil<20>`. Kept because WebSocket needs it.              |
| `blake3_hash`    | `veil.blake3`        | Returns `sigil<32>`.                                               |
| `blake3_hex`     | `sigil.hex`          | (Merged; see above.)                                               |
| `hmac_sha256`    | `veil.sign_sha256`   | HMAC is a signature operation.                                     |

#### 4.6.2 Key derivation / AEAD

| old                   | new                 | notes                                                            |
|-----------------------|---------------------|------------------------------------------------------------------|
| `pbkdf2_hmac_sha256`  | `veil.derive`       | "Derive a key from a passphrase."                                |
| `chacha20_encrypt`    | `veil.cipher`       | Stream cipher.                                                   |
| `chacha20_decrypt`    | `veil.uncipher`     |                                                                  |
| `aead_encrypt`        | `veil.seal`         | AEAD seals plaintext + AAD into a ciphertext + tag.              |
| `aead_decrypt`        | `veil.unseal`       |                                                                  |
| `aes_encrypt_block`   | `veil.aes_block`    | Low-level; kept accessible.                                      |

#### 4.6.3 Randomness / constant-time

| old               | new                 | notes                                                              |
|-------------------|---------------------|--------------------------------------------------------------------|
| `csprng_bytes`    | `veil.entropy`      | Pull N random bytes.                                               |
| `entropy_u64`     | `veil.entropy_u64`  |                                                                    |
| `entropy_range`   | `veil.entropy_range`|                                                                    |
| `entropy_bool`    | `veil.entropy_bool` |                                                                    |
| `entropy_token`   | `veil.token`        | Random URL-safe token.                                             |
| `entropy_uuid`    | `veil.uuid`         |                                                                    |
| `entropy_salt`    | `veil.salt`         |                                                                    |
| `ct_eq`           | `veil.eq_steady`    | Constant-time comparison — "steady" hints why it exists.           |
| `ct_select_u8`    | `veil.pick_steady`  |                                                                    |
| `secure_zero`     | `veil.erase`        | Wipe a region.                                                     |
| `secure_wipe_random` | `veil.scour`     | Overwrite with random bytes.                                       |

---

### 4.7 `chrono` (was `time`)

| old                    | new                   | notes                                                           |
|------------------------|-----------------------|-----------------------------------------------------------------|
| `Duration`             | `arc`                 | See 3.6.                                                        |
| `dur_new`              | `arc.new`             |                                                                 |
| `dur_from_secs`        | `arc.secs`            | `chrono.arc.secs(5)`.                                           |
| `dur_from_millis`      | `arc.millis`          |                                                                 |
| `dur_from_micros`      | `arc.micros`          |                                                                 |
| `dur_from_nanos`       | `arc.nanos`           |                                                                 |
| `dur_as_secs`          | `arc.in_secs`         |                                                                 |
| `dur_as_millis`        | `arc.in_millis`       |                                                                 |
| `dur_as_nanos`         | `arc.in_nanos`        |                                                                 |
| `dur_add`/`sub`/`mul`/`div` | operator overloads on `arc` | `arc + arc`, `arc * n`.                              |
| `Instant`              | `moment`              | See 3.6.                                                        |
| `instant_now`          | `chrono.now`          |                                                                 |
| `instant_elapsed`      | `moment.since`        | "How long since this moment?"                                   |
| `instant_duration_since` | `moment.gap_to`     | Difference between two moments.                                 |
| `SystemTime`           | `moment` (same type; `chrono.now_wall` for wall-clock variant). | Merged.            |
| `system_time_from_epoch_ms` | `chrono.from_epoch_ms` |                                                            |
| `to_iso`               | `moment.iso`          |                                                                 |
| `to_rfc2822`           | `moment.rfc2822`      |                                                                 |
| `format_moment`        | `moment.format`       | Native name — no rename.                                        |
| `now_ms` / `now_nano`  | `chrono.now_ms` / `chrono.now_ns` |                                                     |
| `sleep_ms`             | `slumber(@ms)`        | Shared with `nebula`.                                           |
| `Stopwatch`            | `trail`               | A stopwatch leaves a trail of laps.                             |
| `stopwatch_start`      | `trail.start`         |                                                                 |
| `stopwatch_lap`        | `trail.mark`          |                                                                 |
| `stopwatch_stop`       | `trail.end`           |                                                                 |
| `benchmark`            | `chrono.gauge`        | Measure a fn's average runtime.                                 |
| `Timeout`              | `countdown`           |                                                                 |
| `timeout_new`          | `countdown.new`       |                                                                 |
| `timeout_is_expired`   | `countdown.done`      |                                                                 |
| `timeout_remaining`    | `countdown.left`      |                                                                 |
| `RateLimiter`          | `throttle`            |                                                                 |
| `rate_limiter_new`     | `throttle.new`        |                                                                 |
| `rate_limiter_try_acquire` | `throttle.try_pass`|                                                                 |
| `rate_limiter_wait`    | `throttle.pass`       |                                                                 |
| `wheel_schedule`       | `chrono.schedule`     | Hashed-wheel scheduler. The wheel is implementation, not API.   |
| `wheel_cancel`         | `chrono.cancel`       |                                                                 |

---

### 4.8 `lumen` (was `json`)

| old                     | new                    | notes                                                        |
|-------------------------|------------------------|--------------------------------------------------------------|
| `JsonDoc`               | `Lumen`                | The whole parsed document.                                   |
| `JsonNode`              | `LumenNode`            | (Internal — rarely user-facing.)                             |
| `json_parse`            | `lumen.read`           |                                                              |
| `json_is_valid`         | `lumen.valid`          | Returns bool, not int.                                       |
| `json_stringify`        | `lumen.show`           |                                                              |
| `json_stringify_pretty` | `lumen.show_pretty`    |                                                              |
| `json_get`              | `Lumen[key]`           | Operator overload. Also: `Lumen.field("key")`.               |
| `json_get_index`        | `Lumen[i]`             |                                                              |
| `json_get_string`       | `LumenNode.text`       |                                                              |
| `json_get_int`          | `LumenNode.int`        |                                                              |
| `json_get_float`        | `LumenNode.float`      |                                                              |
| `json_get_bool`         | `LumenNode.bool`       |                                                              |
| `json_is_null`          | `LumenNode.is_dark`    | Null = dark (absent light).                                  |
| `json_array_len`        | `LumenNode.len`        |                                                              |
| `json_object_len`       | `LumenNode.len`        | Merged.                                                      |
| `json_null`             | `lumen.dark`           | Builder for null node.                                       |
| `json_bool`             | `lumen.flag`           |                                                              |
| `json_int`              | `lumen.int`            |                                                              |
| `json_float`            | `lumen.float`          |                                                              |
| `json_string`           | `lumen.text`           |                                                              |
| `json_array`            | `lumen.list`           |                                                              |
| `json_object`           | `lumen.map`            |                                                              |
| `JSON_NULL`..`JSON_OBJECT` | `LumenKind.{Dark,Flag,Int,Float,Text,List,Map}` | Enum instead of raw ints.    |

---

### 4.9 `glyph` (was `regex`)

| old                   | new                    | notes                                                        |
|-----------------------|------------------------|--------------------------------------------------------------|
| `regex_compile`       | `glyph.carve`          | "Carve a glyph into the pattern table."                      |
| `regex_match`         | `glyph.anchors`        | Full-string match — "does the glyph anchor this input?"      |
| `regex_find`          | `glyph.trace`          | First occurrence; trace through input.                       |
| `regex_find_all`      | `glyph.trace_all`      |                                                              |
| `regex_replace`       | `glyph.rewrite`        |                                                              |
| `regex_split`         | `glyph.shatter`        | Split input by pattern = shatter along the glyph.            |
| `regex_match_dfa`     | `glyph.anchors_fast`   | DFA path. Same semantics; performance flag.                  |
| `regex_find_dfa`      | `glyph.trace_fast`     |                                                              |
| `regex_get_group_start` | `trace.group_start` | Methods on the trace result.                                 |
| `regex_get_group_end` | `trace.group_end`      |                                                              |
| `regex_get_match_start`| `trace.start`         |                                                              |
| `regex_get_match_end` | `trace.end`            |                                                              |
| `regex_get_group_count` | `trace.groups`       |                                                              |
| `regex_is_compiled`   | `glyph.carved`         |                                                              |
| `RE_OK`/`RE_ERROR`/`RE_NO_MATCH` | replaced by `phase<Trace>` | No more sentinel ints.                          |

---

### 4.10 `vault` (was `db`)

| old                | new                 | notes                                                              |
|--------------------|---------------------|--------------------------------------------------------------------|
| `db_put`           | `vault.store`       |                                                                    |
| `db_get`           | `vault.recall`      | "Recall from the vault."                                           |
| `db_get_type`      | `vault.kind`        |                                                                    |
| `db_delete`        | `vault.forget`      |                                                                    |
| `db_exists`        | `vault.has`         |                                                                    |
| `db_count`         | `vault.size`        |                                                                    |
| `db_query_prefix`  | `vault.scan`        | Prefix scan.                                                       |
| `db_begin`         | `vault.gather`      | Begin transaction = gather changes.                                |
| `db_commit`        | `vault.sigil`       | Commit = seal with sigil.                                          |
| `db_rollback`      | `vault.scatter`     | Undo.                                                              |
| `db_save`          | `vault.persist`     | Persist to disk.                                                   |
| `db_load`          | `vault.wake`        | Wake the vault from disk.                                          |
| `db_compact`       | `vault.compact`     | Kept.                                                              |
| `db_clear`         | `vault.empty`       |                                                                    |
| `db_put_int`       | `vault.store_int`   | Typed helpers preserved.                                           |
| `db_get_int`       | `vault.recall_int`  |                                                                    |
| `db_put_string`    | `vault.store_text`  |                                                                    |
| `db_is_dirty`      | `vault.smudged`     | Whether uncommitted changes exist.                                 |
| `VAL_NULL`..`VAL_OBJECT` | `VaultKind.{Dark,Flag,Int,Float,Text,Bytes,List,Map}` | Shared vocabulary with `LumenKind`. |
| `DB_OK`/`DB_ERROR`/`DB_NOT_FOUND`/`DB_FULL` | returned as `phase<T, Fault>` | No sentinels.             |

---

## 5. Deprecation policy

**One full release cycle.** When v5.0 ships with the cosmic names, every old
name from this map is kept in the stdlib as a thin alias:

```luna
# src/stdlib/net.luna (compat shim)
pub seal fn tcp_connect(@host_ptr, @host_len, @port) -> int
    # @deprecated(since="5.0", replaced_by="orbit.link")
    return orbit::link(@host_ptr, @host_len, @port)
```

Using an alias produces a compiler warning (`warn: deprecated name
'tcp_connect', use 'orbit.link' instead`) but not an error. The `luna fmt`
tool gets a `--cosmic` flag that rewrites old names to new ones in-place so
migration is a single command. At v6.0 the aliases are removed.

Module-level aliases (`import net` → re-exports `orbit`) follow the same
rule. `import net` in v5.x silently redirects and warns; in v6.0 it errors.

Internal compiler symbols and LIDL-generated FFI names (`luna_socket_*`,
`luna_tls_*`, `luna_scheduler_*`) **do not** get renamed — those are ABI,
not user surface. Only the `.luna`-level functions on top of them change.

---

## 6. Examples — before / after

### 6.1 TCP echo client

**Before:**

```luna
import net

fn main() -> int
    @fd = tcp_connect("example.com", 11, 80)
    if @fd < 0
        return 1
    tcp_send(@fd, "GET / HTTP/1.0\r\n\r\n", 18)
    meow @buf: [int; 1024] = [0; 1024]
    @n = tcp_recv(@fd, @buf, 1024)
    tcp_close(@fd)
    return 0
```

**After:**

```luna
import orbit

fn main() -> phase<int>
    seal @star = orbit::link("example.com", 80)?
    @star.emit("GET / HTTP/1.0\r\n\r\n")
    meow @buf: [u8; 1024] = [0; 1024]
    seal @n = @star.receive(@buf)?
    @star.extinguish()
    shine Bright(0)
```

Note: the `?` unwraps a `phase<_>`; `Dim(fault)` is auto-propagated. No more
`if @fd < 0` scaffolding around every call.

### 6.2 Mutex-protected counter

**Before:**

```luna
import sync

meow @m: Mutex = mutex_new()
meow @count: int = 0

fn bump()
    mutex_lock(@m)
    @count = @count + 1
    mutex_unlock(@m)
```

**After:**

```luna
import halo

seal @ring: halo = halo::new()
meow @count: int = 0

fn bump()
    @ring.within
        @count = @count + 1
```

The `halo.within` block auto-releases on exit — no forgotten unlock.

### 6.3 HTTP GET

**Before:**

```luna
import http

fn main() -> int
    @resp = http_get("https://api.example.com/ping", 28)
    if @resp.ok != 1
        return 1
    if @resp.status != 200
        return 1
    return 0
```

**After:**

```luna
import beam

fn main() -> phase<int>
    seal @echo = beam::fetch("https://api.example.com/ping")?
    guard @echo.status == Status.Ok, Dim(Fault.net("non-200"))
    shine Bright(0)
```

### 6.4 JSON parse + field read

**Before:**

```luna
import json

fn main() -> int
    @doc = json_parse("{\"name\":\"luna\",\"age\":4}")
    if json_root_kind(@doc) != JSON_OBJECT
        return 1
    @name_idx = json_get(@doc, 0, "name")
    @name = json_get_string(@doc, @name_idx)
    return 0
```

**After:**

```luna
import lumen

fn main() -> phase<int>
    seal @doc = lumen::read("{\"name\":\"luna\",\"age\":4}")?
    guard @doc.kind == LumenKind.Map, Dim(Fault.parse("not an object"))
    seal @name = @doc["name"].text
    shine Bright(0)
```

### 6.5 Spawn and channel

**Before:**

```luna
import scheduler
import sync

fn worker(@ch: Channel)
    orbit @i in 0..10
        channel_send(@ch, @i)
    channel_close(@ch)

fn main() -> int
    sched_init(0)
    @ch = channel_new(16)
    @t = spawn worker(@ch)
    orbit @iter in 0..10
        @v, @ok = channel_recv(@ch)
        if @ok != CHAN_OK
            nova
        shine(@v)
    await_task(@t)
    return 0
```

**After:**

```luna
import nebula
import halo

fn worker(@p: pulse<int>)
    orbit @i in 0..10
        @p.emit(@i)
    @p.seal()

fn main() -> phase<int>
    nebula::ignite_per_core()
    seal @p = halo::pulse.bounded(16)
    seal @t = spawn worker(@p)
    orbit
        phase @p.catch()
            Bright(v) => shine(v)
            Dim(_)    => nova
    @t.await()
    shine Bright(0)
```

### 6.6 Telegram bot update loop

**Before:**

```luna
import http
import json
import time

fn loop(@token: str, @token_len: int)
    meow @offset: int = 0
    orbit _ in 0..1000000
        @resp = http_get(tg_updates_url(@token, @token_len, @offset), 0)
        if @resp.ok != 1
            sleep_ms(1000)
            orbit @skip in 0..0 pass
        @doc = json_parse(@resp.body)
        # ... process ...
        sleep_ms(500)
```

**After:**

```luna
import beam
import lumen
import chrono

fn loop(@token: str)
    meow @offset: int = 0
    orbit _ in 0..1000000
        phase beam::fetch(tg_updates_url(@token, @offset))
            Bright(echo) =>
                seal @doc = lumen::read(echo.body)?
                # ... process ...
            Dim(_) =>
                chrono::slumber(1000)
        chrono::slumber(500)
```

---

## 7. Coverage audit

Every function listed by `grep '^fn ' src/stdlib/{net,http,io,sync,scheduler,crypto,time,json,regex,db}.luna`
has a row in one of the tables above, *except* internal helpers prefixed
with `_`, parser primitives (`jp_*`, `parse_*`, `nfa_*`, `dfa_*`), buffer
helpers (`http_buf_write_*`, `ser_*`, `resp_write_*`), RAMFS internals
(`ramfs_*` collapse under `cache.*`), blocking-pool internals
(`blocking_pool_*`), deque/queue internals (`deque_*`, `cqueue_*`,
`gqueue_*`), and constants whose group has a table note (e.g.
"`STATE_* → CometState.*`"). The `luna_*` extern-C shim layer is unchanged
by this map — those are ABI.

## 8. Open questions (for reviewer)

1. **Two-word calls vs namespaced methods.** This map uses `orbit::link`
   (double-colon) and `star.emit` (dot). The convention is: *construction
   and free-standing verbs* live on the module (`orbit::link`), *operations
   on a handle* live on the value (`star.emit`). The line is drawn per row;
   if the codebase prefers one uniform style, say the word.
2. **`sleep` vs `slumber` vs `drift`.** Three candidates for three different
   things (block current fiber, block current thread, yield cooperatively).
   Current proposal: `slumber(ms)` blocks, `drift()` yields, no separate
   thread-sleep — if someone needs OS sleep, they use `slumber` on a
   `grounded` task. Confirm before applying.
3. **`phase<T>` and the `phase` keyword.** Deliberately the same word.
   A parser can disambiguate by position. If this is rejected, alternative
   type name candidates: `moon<T>`, `tide<T>`, `light<T>`. I prefer `phase`.
4. **`halo` is overloaded** (both mutex and read-write lock, with `.dim()`
   / `.dark()`). If that's too clever, split: `halo` for mutex, `eclipse`
   for rwlock (`eclipse.dim()` / `eclipse.dark()`). Note `eclipse` is
   already a control-flow keyword — collision-prone.

*End of map.*
