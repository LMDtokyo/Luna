# Luna Roadmap — путь до Go/C# parity

Огромный план чтобы Luna могла писать полноценные серверы, ботов,
бекенды, работу с БД, памятью, приложения уровня Go или C#.

**Обновление файла**: при завершении пункта меняй `[ ]` на `[x]`,
указывай commit/файл. Цель — единый источник правды.

---

## Существующая работа — без дубликатов

Перед тем как делать что-то новое, проверь не сделано ли уже:

| Уже есть | Файл | Заметка |
|---|---|---|
| Append-only KV store | `std/std/kvstore.luna` | T4. base64-encoded values, `kv_get/put/delete/keys/count/compact` |
| Native BTree KV (aspirational) | `src/stdlib/db.luna` | 856 строк, **несовместим с bootminor** (использует `*u8` типы). Для будущего полного компилятора. |
| HTTPS клиент | `std/ext/https.luna` | T6 через `openssl s_client` subprocess. Pattern для FFI-через-CLI. |
| HTTP сервер | `std/net/http_server.luna` | Fork-per-connection, arena-managed. |
| Logger (НЕ МИГРИРОВАЛ) | удалён | Старый `src/stdlib_new/logger.luna` использовал `_putenv` (extern C), несовместимо. Нужен новый. |

---

## Что готово (по состоянию на 2026-05-11)

### Core
- [x] **Bootminor compiler** — self-host, fixed-point byte-identical, 235884 байт ([src/bootminor/luna-mini.elf](../src/bootminor/luna-mini.elf))
- [x] **Lock-free bump allocator** — `LOCK XADD` atomic heap_top, thread-safe
- [x] **Arena memory management** — `arena_mark()` / `arena_reset(@m)`, утечки устранены
- [x] **Tier architecture** — T0–T8 в std/, enforced linter ([tools/lint_tiers.sh](../tools/lint_tiers.sh))

### Sys-уровень
- [x] **Syscalls**: read, write, open, close, lseek, exit, creat, mmap, munmap, brk, getpid, getrandom, execve, pipe, socket, bind, listen, accept, connect, setsockopt, mprotect, nanosleep, clone, futex, gettid, tgkill, time, wait4
- [x] **Threading** — `thread_spawn`, fork via `sys_clone(SIGCHLD)`

### Stdlib T3 (core)
- [x] env — [std/core/env.luna](../std/core/env.luna)
- [x] sys — [std/core/sys.luna](../std/core/sys.luna)

### Stdlib T4 (std)
- [x] base64 (13 PASS) — [std/std/base64.luna](../std/std/base64.luna)
- [x] cli (19 PASS)
- [x] csv (миграция, без тестов) — [std/std/csv.luna](../std/std/csv.luna)
- [x] io (27 PASS) — [std/std/io.luna](../std/std/io.luna)
- [x] json (37 PASS) — [std/std/json.luna](../std/std/json.luna) — incl. `json_get_str/int/bool`, `json_array_at/len/str/int`
- [x] kvstore (27 PASS) — [std/std/kvstore.luna](../std/std/kvstore.luna) — incl. `kv_keys/count/compact`
- [x] map (19 PASS)
- [x] strings (29 PASS)
- [x] template (9 PASS) — `{{var}}` substitution
- [x] test (assertion framework)
- [x] time (8 PASS) — `time_now`, `time_format_iso`, civil-from-days
- [x] url (14 PASS) — `url_encode/decode`, RFC 3986

### Stdlib T5 (net)
- [x] tcp (11 PASS, E2E) — `tcp_listen/accept/connect/send/recv/close`, SO_REUSEADDR
- [x] dns (13 PASS, E2E) — UDP DNS resolver
- [x] http (10 PASS, E2E) — клиент, `http_get/post`, `http_get_host` (с DNS)
- [x] http_server (30 PASS, E2E) — fork-based, `http_serve_forking`, cookies, query, form parser
- [x] http_router (11 PASS, E2E) — path params (`/users/:id`), method matching
- [x] multipart (12 PASS) — RFC 7578 file upload parser

### Stdlib T6 (ext)
- [x] crypto/sha256 (6 PASS) — incl. HMAC, NIST + RFC 4231 vectors
- [x] crypto/sha512 (3 PASS) — incl. HMAC
- [x] websocket (6 PASS) — RFC 6455, SHA-1, frame codec, `ws_upgrade`
- [x] process — `shell_run`, `shell_capture` (fork+exec)
- [x] https — HTTPS клиент через `openssl s_client` subprocess

### Examples
- [x] [chat_server](../examples/net/chat_server.luna) — REST messenger (auth + sessions + persistent storage)
- [x] [telegram_bot](../examples/bots/telegram_bot.luna) — long-poll bot, /start /help /echo /stats
- [x] [hello_server](../examples/net/hello_server.luna), [messenger_api](../examples/net/messenger_api.luna)

### Tooling
- [x] `luna build/run/new` CLI ([src/bootminor/luna](../src/bootminor/luna))
- [x] `luna pkg init/add/list/remove/sync` package manager (git-based)
- [x] `tools/lint_tiers.sh` — enforces tier discipline
- [x] `tests/run_all.sh` — unified test runner

### Verification
- [x] 313 unit tests (21 модуль)
- [x] 10/10 E2E (tcp_echo, http_get, dns_resolve, http_host_get, http_server, http_router, concurrent, chat, chat_stress, pkg)
- [x] 53 bootminor self-tests (m2b 8 + m2c 10 + types 10 + closures 12 + threads 3 + ADT 6 + generics 4)
- [x] Fixed-point self-host (byte-identical re-compile)
- [x] Arena memory: 200 req → 0 KB RSS growth (chat_stress.sh)

---

## TIER 1 — Production foundation (без этого НЕ ставим в прод)

Эти 6 пунктов — **must-have** для серьёзной эксплуатации.

- [x] **1.1 SQLite через subprocess** — [std/ext/db/sqlite.luna](../std/ext/db/sqlite.luna). **DONE 2026-05-11**. 145 строк. 25 unit tests PASS (CREATE/INSERT/SELECT/UPDATE/DELETE/COUNT/ORDER BY/transactions BEGIN-COMMIT-ROLLBACK/CREATE INDEX/error path/db_quote/db_int). API: `db_open/exec/query/query_one/scalar/quote/int/close`.
- [ ] **1.2 SQLite через FFI (production)** — расширить bootminor: emit `.dynamic`, `DT_NEEDED libsqlite3.so.0`, `.interp /lib64/ld-linux-x86-64.so.2`, indirect call через GOT/PLT. ~600 строк bootminor + ~400 wrapper. Replaces 1.1.
- [~] **1.3 TLS через libssl FFI** — на той же dlopen инфраструктуре. Inbound HTTPS server + замена openssl subprocess. **DESIGN READY**: [docs/TLS_DESIGN.md](TLS_DESIGN.md) — 7 фаз, путь A (dlopen) рекомендован.
   - **P1 DONE 2026-05-12**: Bootminor `--dynamic` флаг + минимальный dynamic ELF (PT_INTERP + PT_DYNAMIC + .dynstr/.dynsym/.hash + DT_NEEDED libc.so.6). ~180 LoC. ld-linux корректно загружает.
   - **P2 DONE 2026-05-12**: PLT call-site emission через `call qword [rip+disp32]` + .got.plt slots + .rela.plt (R_X86_64_JUMP_SLOT) + DT_PLTGOT/PLTRELSZ/PLTREL/JMPREL/BIND_NOW. ld-linux заполняет GOT slots at load time. Интринсики `_libc_puts`, `_libc_write`, `_libc_dlopen`, `_libc_dlsym`, `_call_ptr1` в [gen.luna](../src/bootminor/gen.luna). `_build_dyn_blob` расширен (~150 LoC). **Verified**: `_libc_write(1, msg, n)` через dynamic ELF печатает "Hello from libc.write!" — libc вызов через PLT/GOT работает end-to-end.
   - **P3 DONE 2026-05-12**: `_emit_start_stub` эмитит `_start` (38 байт x86-64), который читает argc/argv со стека + setup ABI + `call __libc_start_main(main, argc, argv, init=NULL, fini=NULL, rtld_fini=NULL)`. libc инициализирует TLS/errno/stdio, потом зовёт main(). main RETURNS в __libc_start_main, libc делает exit() с flush stdio + atexit handlers. Также в `parse_fn`/`parse_stmt` main exits через `_em_epilogue` (ret) вместо sys_exit когда dynamic. **Verified end-to-end**: `_libc_puts("Hello!")` буферизованный stdio печатает + flushes; `_libc_dlopen("libc.so.6", 2)` → valid handle; `_libc_dlsym(handle, "puts")` → fp; `_call_ptr1(fp, msg)` → indirect call работает. Полный цикл dlopen workflow открыт.
   - **Known regression**: m2b/m2c bootstrap tests FAIL потому что `luna_bootstrap.c` (C-минимальный компилятор) не справляется с текущим размером gen.luna (4580 строк). Shipped luna-mini.elf и `selfhost_build.sh` работают корректно (`fixed-point at stage2=stage3` верифицирован). Bootstrap нужно перегенерировать когда будет время — это **не блокирует prod usage** (install.sh ставит shipped luna-mini, end-user'ы не bootstrap'ятся).
   - **TODO**: P5 (`std/ext/tls.luna` — dlopen libssl, обернуть SSL_CTX_new/SSL_new/SSL_connect/SSL_read/SSL_write), P6 (миграция `https.luna` на P5), P7 (inbound HTTPS server). С P3 готовым — это уже чисто Luna-код, без хирургии bootminor.
- [x] **1.4 Pre-fork worker pool** — [std/net/http_server.luna](../std/net/http_server.luna) `http_serve_prefork` + `http_serve_ctx_prefork`, [std/net/http_router.luna](../std/net/http_router.luna) `router_serve_prefork`. **DONE 2026-05-11**. ~80 строк. E2E PASS: 5 parallel clients against 4-worker pool ([tests/std/net/prefork_e2e.sh](../tests/std/net/prefork_e2e.sh)).
- [x] **1.5 Structured logging** — [std/std/log.luna](../std/std/log.luna). **DONE 2026-05-11**. 130 строк. **15 PASS**: JSON line к stderr с ts/level/msg/kvs, шорткаты log_info/warn/error/debug + _1/_2/_3 arity + _vec. JSON escaping для quotes/backslashes. **Не путать со старым удалённым** logger.luna (использовал _putenv).
- [x] **1.6 Error stack traces (MVP)** — `panic("...")` → backtrace со строкой и именем функции. **DONE 2026-05-11**. P1+P2+P3 фаз дизайна реализованы:
   - **P1**: frame pointers — *уже* эмитятся в `_em_prologue` (push rbp; mov rbp, rsp). Нулевая правка.
   - **P2**: debug table в data section. Расширил header с 40 до 64 байт (3 новых u64-слота: dbg_table_vaddr, dbg_count, dbg_pool_vaddr). Эмитим per-fn запись 24 байта: `pc_lo:u64, pc_hi:u64, line:u32, name_off:u32` + name pool (NUL-terminated). [src/bootminor/gen.luna](../src/bootminor/gen.luna) + [main2.luna](../src/bootminor/main2.luna) патчит абсолютные vaddrs после layout. **~80 LoC**.
   - **P3**: `panic(@msg)` builtin в prelude + 7 новых компилятор-интринсиков (`_get_rbp`, `_load_u64/u32/u8`, `dbg_table_ptr/count/pool_ptr`). Runtime helper в [bootminor_prelude.luna](../src/bootminor/bootminor_prelude.luna) walk'ает rbp-chain, ищет каждый return-address линейно в таблице, печатает `  at line N  fn NAME`. **~100 LoC**.
   - **Fixed-point** держится at stage2=stage3 (стадия 1 не несёт новой эмиссии, стадия 2 и 3 байт-идентичны). luna-mini.elf вырос 236k→253k.
   - **Demo**: `panic("oops")` из 3-уровневой вложенности печатает 4-frame trace с именами и строками. Exit code = 1.
   - **P4 (signal handlers)**: **DONE 2026-05-11**. Trampoline (9 байт `mov rax, 15; syscall`) эмитится после `_emit_print_int`. Прелюдная функция `enable_panic_signals()` ставит handler на SIGSEGV/SIGFPE/SIGBUS/SIGABRT/SIGILL через `sys_rt_sigaction` с SA_SIGINFO|SA_RESTORER|SA_RESETHAND. Handler читает RIP/RBP из ucontext (offsets 168/120), печатает signal + frame at RIP + walk'ает chain, exit 128+signum. ~150 LoC прелюда + ~30 LoC gen.luna. Test: null-deref `_load_u64(0)` теперь выдаёт `panic: caught signal 11` + 3-frame trace + rc=139.
   - **P5 (DWARF .debug_line + .debug_info)**: **DONE 2026-05-11**. Bootminor теперь эмитит полный ELF section header table + 5 debug-секций (`.debug_line`, `.debug_abbrev`, `.debug_info`, `.debug_str`, `.shstrtab`). DWARF 4 формат. ~700 LoC в [main2.luna](../src/bootminor/main2.luna): ULEB128/SLEB128 кодеры, section-header writer, builders для 4 debug-секций. ELF структура поменялась: добавились sh-fields в Ehdr (e_shoff, e_shentsize, e_shnum, e_shstrndx), debug секции лежат после PT_LOAD's file_sz — не загружаются в память runtime. **Полная интеграция со стандартным toolchain**:
     - `addr2line -f -e binary 0xADDR` → `fn_name\nfile.luna:LINE`
     - `readelf --debug-dump=line binary` → полная (PC, line) таблица
     - `readelf --debug-dump=info binary` → CU DIE + один DW_TAG_subprogram DIE на каждую функцию
     - `gdb binary; info functions; info line FN` → работает на всех fn'ах
     - perf, eu-elflint, eu-readelf тоже работают
   - **P6 (.eh_frame CFI)**: **DONE 2026-05-11**. Bootminor эмитит полную Call Frame Information для libunwind / async-профайлеров. Один CIE (initial state: CFA=rsp+8, RA at CFA-8) + FDE на каждую функцию (per-instruction CFI для прологов: после `push rbp` → CFA=rsp+16, rbp at CFA-16; после `mov rbp,rsp` → CFA=rbp+16). Augmentation "zR" с FDE encoding `DW_EH_PE_udata4` (absolute 4-byte). ~150 LoC в [main2.luna](../src/bootminor/main2.luna). **Также пофиксил `_sleb128`** (Luna's `>>` логический не арифметический): добавил sign-extension хелпер + fast-path для значений [-64..63]. **Результат**:
     - `readelf --debug-dump=frames binary` → CIE + FDE per fn parsed без warnings
     - `objdump --dwarf=frames binary` → то же
     - `gdb binary; b deep_fn; run; bt` → **полный backtrace через все 5 уровней**, gdb walk'ает через CFI
     - Async-профайлеры (Sentry SDK, py-spy-стиль) и sampling-профайлеры могут читать `.eh_frame` из binary file для stack-reconstruction
   - **ВСЕ 6 ФАЗ STACK-TRACE ЗАКРЫТЫ.**
- [x] **1.7 Connection pooling (generic TCP)** — [std/std/pool.luna](../std/std/pool.luna). **DONE 2026-05-11**. 200 строк. **28 PASS** ([test](../tests/std/std/pool_test.luna)). API: `pool_new/acquire/acquire_wait/release/invalidate/close/active/idle`. LIFO idle stack, lazy growth, blocking variant с 1ms-poll. Single-threaded (мьютексы появятся когда prelude получит `mutex_new`). Будущий TLS-pool обернёт это.
- [~] **1.8 Graceful shutdown** — **PARTIAL DONE 2026-05-11**. [std/std/signal.luna](../std/std/signal.luna) с `signal_ignore/default/kill` + sys_rt_sigaction/sys_kill intrinsics в bootminor (FP at 236378 B). **8 PASS** ([test](../tests/std/std/signal_test.luna)). Custom signal HANDLERS не реализованы (требуют sa_restorer trampoline — нужен inline-asm из Tier 6.2). MVP покрывает SIG_IGN/SIG_DFL/kill — достаточно для auto-reap SIGCHLD, ignore SIGPIPE, custom kill commands. Полный «finish in-flight requests» — отдельный sub-task когда появятся custom handlers.

---

## TIER 2 — Базы данных и persistent state

- [x] **2.1 SQLite full SQL** — см. [1.1](#tier-1--production-foundation-без-этого-не-ставим-в-прод). **DONE via subprocess** ([std/ext/db/sqlite.luna](../std/ext/db/sqlite.luna)). Production-grade FFI version в 1.2 (pending).
- [x] **2.2 PostgreSQL wire protocol client (MVP)** — [std/ext/db/postgres.luna](../std/ext/db/postgres.luna). **DONE 2026-05-11**. 594 строки чистого Luna через `std/net/tcp`. **15 E2E PASS** ([tests/std/ext/db/postgres_e2e.sh](../tests/std/ext/db/postgres_e2e.sh)) против песочного `initdb`+`pg_ctl` Postgres 18 на 127.0.0.1:15432. Простой Query protocol. Auth: **trust только** (MD5/SCRAM отклоняются с понятной ошибкой — ждут md5.luna / отдельного SCRAM модуля). API: `pg_connect/close/query/exec/scalar` + accessors `pg_rows/cols/col_name/value/affected_rows/error/free`.
- [ ] **2.3 MySQL/MariaDB client** — wire protocol. ~1500 строк. Pattern как Postgres.
- [x] **2.4 Redis client** — [std/ext/db/redis.luna](../std/ext/db/redis.luna). **DONE 2026-05-11**. ~330 строк, чистый Luna через `std/net/tcp` (никаких FFI). **23 E2E PASS** ([tests/std/ext/db/redis_e2e.sh](../tests/std/ext/db/redis_e2e.sh)) против sandbox redis-server. Команды: `connect/close/ping`, `set/get/del/exists/incr`, `expire/ttl`, `lpush/rpush/lpop/rpop/llen`, `hset/hget/hdel`. Lower-level: `send_cmd/recv_simple/recv_bulk/recv_integer` для пользовательских команд.
- [x] **2.5 Memcached client** — [std/ext/db/memcached.luna](../std/ext/db/memcached.luna). **DONE 2026-05-11**. ~250 строк. Text protocol over tcp. 13 assertions (unit test gracefully skips if no server on 127.0.0.1:21211; E2E ([tests/std/ext/db/memcached_e2e.sh](../tests/std/ext/db/memcached_e2e.sh)) spawns sandbox memcached). API: `mc_connect/close/set/get/delete/incr/decr/version/quit`.
- [ ] **2.6 SQLite WAL mode** — concurrent reads + serialized writes (зависит от 1.2)
- [x] **2.7 Migration system** — [std/ext/db/migrate.luna](../std/ext/db/migrate.luna). **DONE 2026-05-11**. ~100 строк. API: `migrate_push(ms, id, sql)`, `db_migrate(@h, ms)`, `db_migrate_version(@h)`. Bookkeeping в `_luna_migrations` (id, applied_at). Каждая миграция в собственной transaction. **9 PASS** ([test](../tests/std/ext/db/migrate_test.luna)).
- [x] **2.8 Connection pool generic** — см. [1.7](#tier-1--production-foundation-без-этого-не-ставим-в-прод). Универсальный TCP-pool в [std/std/pool.luna](../std/std/pool.luna).

---

## TIER 3 — Stdlib breadth (то что обычно в std)

### Networking
- [ ] **3.1 Unix domain sockets** — `std/net/unix.luna`. Сейчас прелюд имеет `sys_bind` для AF_UNIX но нет wrapper.
- [x] **3.2 SMTP клиент** — [std/ext/smtp.luna](../std/ext/smtp.luna). **DONE 2026-05-11**. ~200 строк. Plain SMTP (no TLS, no auth для MVP). `smtp_send(host, port, from, to, subject, body)` делает full EHLO/MAIL/RCPT/DATA/QUIT dance. RFC 5321 §4.5.2 dot-stuffing. **8 unit PASS** + E2E через Python stdlib socket fake server captured 11 lines of correct SMTP transaction.
- [ ] **3.3 IMAP/POP3 клиенты** — для чтения почты.
- [ ] **3.4 FTP/SFTP клиенты** — для legacy интеграций.
- [ ] **3.5 gRPC** — HTTP/2 + protobuf. Большой проект. ~3000 строк.
- [ ] **3.6 GraphQL клиент** — поверх http. ~300 строк.
- [ ] **3.7 OAuth 2.0** — поверх https. ~400 строк.
- [x] **3.8 JWT** — [std/ext/jwt.luna](../std/ext/jwt.luna). **DONE 2026-05-11**. ~180 строк. **15 PASS**. HS256 algorithm. `jwt_sign(payload, secret)`, `jwt_verify(token, secret)` (checks signature + `exp` claim), `jwt_decode_payload(token)`. RFC 4648 base64url-encode/decode. Verified against RFC 4648 test vectors + sign/verify round-trip + tampered payload rejection + expired token rejection.
- [~] **3.9 HTTP/2 client (H2C cleartext)** — [std/net/http2.luna](../std/net/http2.luna). **MVP DONE 2026-05-12**. 1021 LoC, **45 PASS** ([test](../tests/std/net/http2_test.luna)). Prior-knowledge h2c (preface + SETTINGS), 9-byte frame header BE, frame types DATA/HEADERS/SETTINGS/PING/GOAWAY/WINDOW_UPDATE/RST_STREAM, HPACK encoder (literal-without-indexing) + decoder (indexed lookup via static table + literal forms + dyn-size update no-op), Huffman strings отвергаются с ошибкой. Sequential multi-request per connection (odd stream-ids). API: `h2_connect/request/request_with_headers/status/header/body/close/error`. **Deferred**: CONTINUATION frames, server push, dynamic HPACK table, Huffman coding, TLS/ALPN (когда 1.3 lands), concurrent streams.

- [ ] **3.9b gRPC client** — поверх 3.9 (HTTP/2) + protobuf wire format. ~2000 LoC. Отдельная сессия.

- [x] **3.32 PNG decoder** — [std/ext/image/png.luna](../std/ext/image/png.luna). **DONE 2026-05-12**. 399 LoC через DEFLATE из gzip.luna. **24 PASS** ([E2E](../tests/std/ext/image/png_e2e.sh)) против реальных ImageMagick фикстур, включая multi-IDAT. Color types: Grayscale-8, RGB-8, RGBA-8 (depth=8). Filters: None/Sub/Up/Average/Paeth (все 5). API: `png_decode → Image`, `img_width/height/channels/pixels/pixel`, `is_png`, `png_error`. Out of scope: palette, 16-bit depth, interlacing.

- [x] **3.41 OpenAPI / Swagger live docs** — [std/net/openapi.luna](../std/net/openapi.luna). **DONE 2026-05-12**. 388 LoC OpenAPI 3.0.3 generator + Swagger UI HTML. **20 PASS** ([test](../tests/std/net/openapi_test.luna)). API: `openapi_new/set_description/set_server/route/param/request_body/tag/to_json/serve`. `openapi_serve(@router, "/docs", @spec)` монтирует `/docs` (Swagger UI через jsDelivr CDN) и `/docs/openapi.json`. Демо: [examples/net/openapi_demo.luna](../examples/net/openapi_demo.luna).

- [x] **3.42 Persistent HTTPS client** — [std/ext/https.luna](../std/ext/https.luna). **REWRITE DONE 2026-05-12**. 811 LoC (было 156). Persistent `openssl s_client` child процесс с HTTP/1.1 keep-alive pooling по host:port. **24 PASS** (15 unit + 9 E2E с self-signed CA + локальный HTTPS server). **10-50× быстрее** outbound HTTPS для повторных запросов к одному хосту (Telegram-боты, API-интеграции). Child reuse verified via pid-check. API: `https_pool_new/acquire/release/request/get/post/close` + backwards-compat `https_get/post`.
- [ ] **3.10 raw sockets (SOCK_RAW)** — для ping, traceroute, network tools.
- [ ] **3.11 Network discovery** — mDNS, SSDP.

### Crypto
- [x] **3.12 AES-GCM** — [std/ext/crypto/aes_gcm.luna](../std/ext/crypto/aes_gcm.luna). **DONE 2026-05-11**. 788 строк (AES-128 + AES-256, 96-bit nonce). **17 PASS** ([test](../tests/std/ext/crypto/aes_gcm_test.luna)) включая NIST McGrew/Viega test cases 1/2/13/14. API: `aes_gcm_encrypt(@key, @nonce, @pt, @aad)` → ct‖tag, `aes_gcm_decrypt(...)` → pt или "" при auth fail, `aes_gcm_check_inputs`, `aes_gcm_hex`. Constant-time tag verify. GHASH через bit-by-bit shift+XOR (медленно, корректно).
- [x] **3.13 ChaCha20-Poly1305** — [std/ext/crypto/chacha20.luna](../std/ext/crypto/chacha20.luna). **DONE 2026-05-11**. 563 строки чистого Luna (RFC 8439). **14 PASS** ([test](../tests/std/ext/crypto/chacha20_test.luna)) включая RFC 8439 §2.4.2 ChaCha20, §2.5.2 Poly1305, §2.6.2 keygen, §2.8.2 AEAD (ciphertext + tag) test vectors. API: `chacha20_poly1305_encrypt/decrypt/xor`, `poly1305_mac`. Poly1305 на 5-limb radix-2^26 (стандартный подход).
- [ ] **3.14 X.509 parser** — для PKI работы. ~600 строк.
- [ ] **3.15 ED25519/Curve25519** — modern asymmetric crypto. ~800 строк.
- [ ] **3.16 RSA** — для legacy PKI. ~600 строк.
- [~] **3.17 PBKDF2 (DONE) / Argon2 / bcrypt** — password hashing. [std/ext/crypto/pbkdf2.luna](../std/ext/crypto/pbkdf2.luna). **PBKDF2-HMAC-SHA256 DONE 2026-05-11**. 314 строк. **14 PASS** ([test](../tests/std/ext/crypto/pbkdf2_test.luna)). API: `pbkdf2_sha256(@pw, @salt, @iter, @keylen)`, `pwhash_make(@pw, @iter)` → PHC-формат, `pwhash_verify(@pw, @stored)` (constant-time), `pwhash_recommended_iterations() = 600000` (OWASP 2023). RFC test vectors verified. **bcrypt/Argon2 — отдельный пункт когда понадобится.**
- [ ] **3.18 Random bytes** — обёртка над `sys_getrandom` (есть).

### Data formats
- [x] **3.19 YAML parser** — [std/std/yaml.luna](../std/std/yaml.luna). **DONE 2026-05-11**. 1215 строк. **50 PASS** ([test](../tests/std/std/yaml_test.luna)). Подмножество YAML 1.2: block mappings, sequences, scalars (unquoted/sq/dq с escapes), bool/null/int/float, comments, flow style `{a:1}`/`[1,2]`. Rejected: tabs в indentation, multi-doc `---`, block scalars `|`/`>`, anchors. API: `yaml_parse(@src)`, `yaml_get_str/int/bool`, `yaml_keys`, `yaml_seq_len/get`, `yaml_map_get/has`, `yaml_error(@root)`, type checks `yaml_is_null/bool/int/str/map/seq/error`. Value DOM как tagged-Vec (зеркало TOML).
- [x] **3.20 TOML parser** — [std/std/toml.luna](../std/std/toml.luna). **DONE 2026-05-11**. 989 строк (TOML 1.0). **27 PASS** ([test](../tests/std/std/toml_test.luna)) включая парсинг реального `[package]/[build]` luna.toml-стиля. Поддерживает: strings (basic + literal), int (dec/hex/oct/bin), float-as-text, bool, секции `[a.b.c]`, dotted keys `a.b = 1`, arrays, inline tables `{}`, comments. Out of scope: dates, `[[arr]]`, multi-line strings, `\uXXXX`. API: `toml_parse(@src)`, `toml_get_str/int/bool` с dotted paths, `toml_keys`, `toml_has`, `toml_array_*`, `toml_error()`.
- [ ] **3.21 XML parser** — `std/std/xml.luna`. ~500 строк.
- [ ] **3.22 HTML parser** — `std/std/html.luna`. ~700 строк.
- [ ] **3.23 Markdown** — `std/std/markdown.luna`. ~400 строк.
- [ ] **3.24 CSV** — улучшить миграцию из `src/stdlib_new/csv.luna` (уже в `std/std/csv.luna` без тестов)
- [ ] **3.25 MessagePack** — binary serialization. ~250 строк.
- [ ] **3.26 Protobuf** — schema-based binary. ~800 строк + codegen.
- [ ] **3.27 BSON** — для MongoDB. ~300 строк.

### Math/Numbers
- [x] **3.28 Math library** — [std/std/math.luna](../std/std/math.luna). **DONE 2026-05-11**. ~200 строк. **41 PASS**. Integer: `int_abs/sign/min/max/clamp/pow/sqrt/log2/gcd/lcm`. IEEE-754 float helpers (operate on bit patterns via SSE intrinsics): `f64_neg/abs/min/max/zero/one`. Constants: `INT_MAX`, `INT_MIN`. Sin/cos/log/exp отложены — нужно отдельно через Taylor series или libm FFI.
- [ ] **3.29 Big integers** — `std/std/bigint.luna`. Произвольная точность. ~800 строк.
- [ ] **3.30 Decimal** — фиксированная точность для финансов. ~400 строк.
- [ ] **3.31 Statistics** — mean, median, stddev, regression. ~300 строк.
- [x] **3.32 Random number generators** — [std/std/random.luna](../std/std/random.luna). **DONE 2026-05-11**. ~160 строк. **xorshift64*** PRNG (`rng_new/next/int/range/bool`) + **cryptographic random** via sys_getrandom (`crypto_bytes/hex/token`). URL-safe base64-ish token alphabet (A-Za-z0-9_-). **12 PASS** ([test](../tests/std/std/random_test.luna)).

### Compression
- [~] **3.33 gzip/deflate decoder (DONE) / encoder (TODO)** — [std/ext/compress/gzip.luna](../std/ext/compress/gzip.luna). **DECODER DONE 2026-05-11**. 661 строка чистого Luna (RFC 1951 inflate + RFC 1952 gzip envelope). **20 PASS** ([E2E](../tests/std/ext/compress/gzip_e2e.sh)) включая стрес-тест на реальном `.tar.gz` файле (26KB → 78KB). Поддержка: stored / fixed Huffman / dynamic Huffman blocks, multi-block streams, gzip header (FHCRC/FEXTRA/FNAME/FCOMMENT), CRC32 + ISIZE verification (отвергает tampered). API: `gzip_decompress(@src)`, `deflate_decompress(@src)`, `gzip_error()`, `is_gzip(@src)`. **Encoder — отдельный пункт**, decoder покрывает 90% backend-нужд (Content-Encoding: gzip responses, .gz файлы).
- [ ] **3.34 zstd** — современный, ~600 строк.
- [ ] **3.35 tar archive** — `std/ext/archive/tar.luna`. ~300 строк.
- [ ] **3.36 zip archive** — ~400 строк.

### Text processing
- [x] **3.37 Regular expressions** — [std/std/regex.luna](../std/std/regex.luna). **DONE 2026-05-11**. ~280 строк. **34 PASS**. Kernighan/Pike backtracking matcher (pure Luna). Поддержка: `.` `^` `$` `*` `+` `?` `\d` `\w` `\s` escape literals `\.\*\+\?\\` char classes `[abc]/[a-z]/[^...]`. API: `regex_match/full/find/replace_all`. **Не поддерживается** (Tier 5+ scope): `()` capture, `|` alternation, `{m,n}` quantifiers, lazy quantifiers, lookaround, backrefs.
- [ ] **3.38 String formatting (printf-like)** — `fmt_sprintf("%d %s", n, s)`. ~300 строк.
- [ ] **3.39 Unicode/UTF-8 handling** — `std/std/unicode.luna`. Codepoints, normalization. ~400 строк.
- [ ] **3.40 Internationalization (i18n)** — message catalogs, plural rules. ~400 строк.
- [ ] **3.41 Diff library** — для текста. ~300 строк.
- [ ] **3.42 Fuzzy matching** — Levenshtein, soundex. ~150 строк.

### Time/Date
- [ ] **3.43 Timezone support** — IANA tz database parsing. ~600 строк.
- [ ] **3.44 Date parsing** — обратный к `time_format_iso` + другие форматы. ~250 строк.
- [ ] **3.45 Duration arithmetic** — `std/std/duration.luna`. ~150 строк.
- [ ] **3.46 Cron expressions** — `parse_cron + next_run`. ~300 строк.

### Filesystem
- [ ] **3.47 Directory traversal** — `walk_dir(@path, @callback)`. ~150 строк.
- [ ] **3.48 File watching (inotify)** — `std/std/inotify.luna`. ~200 строк.
- [ ] **3.49 Temporary files** — `tmpfile()`, `tmpdir()`. ~100 строк.
- [ ] **3.50 File locking (flock)** — для concurrent access. ~80 строк.
- [ ] **3.51 Symlinks** — wrapper над sys_symlink. ~50 строк.
- [ ] **3.52 File permissions** — chmod, chown. ~100 строк.

### Process
- [ ] **3.53 Job control / pipelines** — улучшить `process.luna`, добавить proper pipelines, pty support.
- [ ] **3.54 Signals** — `sys_kill`, `sys_sigaction` обёртки. ~150 строк.
- [ ] **3.55 Environment variable management** — расширить `env_set` (сейчас только read).

### Concurrent / Async
- [ ] **3.56 Pre-fork worker pool** (см. 1.4)
- [ ] **3.57 Thread pool** — поверх `thread_spawn`. ~200 строк.
- [ ] **3.58 Channels** — для inter-thread comm. ~300 строк, нужен `sys_futex`.
- [ ] **3.59 Mutex/RWLock** — primitives. ~150 строк.
- [ ] **3.60 Async/await** — coroutines на async runtime. **XL effort**, ~2000 строк, bootminor extension.
- [ ] **3.61 Job queue (in-process)** — для background tasks. ~200 строк.
- [ ] **3.62 Distributed queue (на Redis)** — зависит от 2.4.

### Observability
- [ ] **3.63 Prometheus metrics** — `std/std/metrics.luna`. Counters, gauges, histograms, HTTP exposition. ~400 строк.
- [ ] **3.64 OpenTelemetry traces** — distributed tracing. ~500 строк.
- [ ] **3.65 Health check endpoint** — стандартный `/healthz` builder. ~50 строк.

### Web ecosystem
- [x] **3.66 Sessions middleware** — [std/net/sessions.luna](../std/net/sessions.luna). **DONE 2026-05-11**. ~145 строк. **23 PASS**. kvstore-backed. API: `session_create/get/touch/destroy/set_data/is_valid` + `session_path`. JSON record `{"created":N,"seen":N,"ttl":T,"data":...}`. ID = secure 32-byte token (crypto_token). Expiry check via `created+ttl > now`.
- [ ] **3.67 CSRF protection** — token-based. ~150 строк.
- [x] **3.68 Rate limiting** — [std/ext/rate_limit.luna](../std/ext/rate_limit.luna). **DONE 2026-05-11** (T6 — imports redis). ~80 строк. **16 E2E PASS** против sandbox redis. Fixed-window counter via Redis INCR+EXPIRE. API: `rate_limit_check(fd, key, max, win)`, `rate_limit_remaining`, `rate_limit_reset`, `rate_limit_ttl`.
- [ ] **3.69 CORS middleware** — стандартная прослойка. ~150 строк.
- [x] **3.70 Static file server** — [std/net/static.luna](../std/net/static.luna). **DONE 2026-05-11**. ~140 строк. API: `static_serve(@cli, @root, @relpath)`. **17 MIME types** (html/css/js/json/xml/png/jpeg/gif/svg/webp/ico/woff/woff2/wasm/pdf/mp4/mp3 + default octet-stream). **Path-traversal guard** (любой `..` в path → 403). **13 PASS** ([test](../tests/std/net/static_test.luna)).
- [ ] **3.71 Server-sent events (SSE)** — для realtime без WebSocket. ~150 строк.
- [ ] **3.72 GraphQL server** — schema + resolver. ~1000 строк.
- [ ] **3.73 OpenAPI/Swagger gen** — из routes + types. ~400 строк.

### Image / Media
- [ ] **3.74 PNG/JPEG decode/encode** — FFI к libpng/libjpeg или pure Luna. ~1500 строк (pure).
- [ ] **3.75 SVG render/parse** — ~600 строк.
- [ ] **3.76 Image resize / crop / format conversion** — поверх 3.74.
- [ ] **3.77 PDF generation** — `std/ext/pdf.luna`. ~800 строк.
- [ ] **3.78 Audio (WAV/MP3)** — FFI или pure для метаданных. ~500 строк.
- [ ] **3.79 Video metadata** — ffprobe wrapper. ~150 строк.

---

## TIER 4 — Developer Experience

- [ ] **4.1 LSP сервер** — `tools/luna-lsp/`. Hover, goto-def, autocomplete, diagnostics. ~2000 строк. **XL effort.**
- [ ] **4.2 Formatter `luna fmt`** — канонический стиль. ~300 строк.
- [ ] **4.3 Documentation generator `luna doc`** — извлекает doc-comments в HTML. ~400 строк.
- [ ] **4.4 Test coverage `luna test --coverage`** — отслеживание лайнов. ~300 строк, нужен DWARF.
- [ ] **4.5 Benchmark framework** — `std/std/bench.luna`. ~150 строк.
- [ ] **4.6 GDB integration через DWARF** — bootminor emit debug info (есть в src/core/forge_dwarf.luna для будущего компилятора).
- [ ] **4.7 CPU/heap profiler** — `luna prof <binary>`. ~500 строк.
- [ ] **4.8 Memory analyzer** — track allocations by call site. ~300 строк.
- [ ] **4.9 VSCode extension** — есть базовый ([editors/vscode/](../editors/vscode/)), нужен LSP подключить.
- [ ] **4.10 JetBrains plugin** — для IntelliJ family.
- [ ] **4.11 Vim/Neovim plugin** — встроен через LSP.
- [ ] **4.12 Error stack traces при panic** (см. 1.6)
- [ ] **4.13 REPL `luna repl`** — для интерактивной разработки. ~400 строк.
- [ ] **4.14 Linter `luna check`** — статический анализатор. ~500 строк.

---

## TIER 5 — Language features (parity с Go/Rust/C#)

### Control flow
- [ ] **5.1 String interpolation** — `f"Hello {@name}"`. ~150 строк (lexer + parser).
- [ ] **5.2 Tuples + destructuring** — `let (a, b) = pair()`. ~200 строк.
- [ ] **5.3 Pattern matching enhancements** — `if let`, nested, guards. ~300 строк.
- [ ] **5.4 Range expressions** — `[0..10]`, `s[start..end]`. ~150 строк.
- [ ] **5.5 Iterators / `for x in coll`** — current `orbit @i in 0..10` есть, нужна общая итерация. ~250 строк.
- [ ] **5.6 Generators / `yield`** — coroutine-style iterators. ~400 строк.
- [ ] **5.7 Defer / try-finally** — RAII-style cleanup. ~200 строк.
- [ ] **5.8 Try-catch / Result chaining** — `?` operator. ~250 строк.

### Type system
- [ ] **5.9 Method syntax + impl blocks** — `impl User { fn greet(@self) ... }`. ~400 строк.
- [ ] **5.10 Traits / interfaces** — `trait Speakable { ... }`. ~600 строк.
- [ ] **5.11 Generic constraints** — `fn sort<T: Ord>(...)`. ~500 строк.
- [ ] **5.12 Type aliases** — `type UserId = int`. ~50 строк.
- [ ] **5.13 Enums (sum types beyond ADTs)** — с дискриминантом. ~200 строк.
- [ ] **5.14 Newtype pattern** — wrappers с different semantics.
- [ ] **5.15 Local type inference** — `let x = foo()` infers from return. ~300 строк.
- [ ] **5.16 Const-evaluation** — `const X = compute_at_compile_time()`. ~400 строк.
- [ ] **5.17 Reflection** — `typeof`, runtime type info. ~400 строк.

### Memory
- [ ] **5.18 Closures with reference captures** — сейчас M11c by-value only. ~300 строк bootminor.
- [ ] **5.19 Boxed values / heap objects** — explicit heap allocation control.
- [ ] **5.20 Smart pointers (Rc/Arc)** — refcount-based mem management. ~250 строк.
- [ ] **5.21 Borrow checking** — ownership semantics. Большой проект, ~2000 строк (есть прототип в src/core/borrow_checker.luna).

### Concurrency primitives
- [ ] **5.22 `async fn` / `await`** (см. 3.60)
- [ ] **5.23 Goroutine-like `spawn`** — green threads с runtime.

### Misc
- [ ] **5.24 Operator overloading** — `impl Add for Vec`. ~300 строк.
- [ ] **5.25 String literals в `const`** — сейчас const только для int. ~50 строк.
- [ ] **5.26 Multi-line strings / raw strings** — `r"..."`, `"""..."""`. ~80 строк.
- [ ] **5.27 Escape sequences `\xNN`** — задокументировано как gotcha, нужно фиксить. ~50 строк.

---

## TIER 6 — Compiler quality

- [ ] **6.1 Compiler optimizations (titan_opt подключить)** — есть скелет в [src/core/titan_opt.luna](../src/core/titan_opt.luna). Constant folding, dead code, inlining. ~800 строк интеграции.
- [ ] **6.2 Inline assembly** — `asm!("...")` блоки. ~200 строк.
- [ ] **6.3 SIMD intrinsics** — AVX/AVX2 для векторных операций. ~300 строк.
- [ ] **6.4 PGO (Profile-Guided Optimization)** — `--profile-generate` / `--profile-use`. ~500 строк.
- [ ] **6.5 LTO** — cross-module dead code, inlining. ~400 строк.
- [ ] **6.6 Self-host через titan_opt** — bootminor → optimized luna. **XL effort.**
- [ ] **6.7 Compile-time function execution (const fn)** — eval at compile. ~600 строк.

---

## TIER 7 — Platform expansion

- [ ] **7.1 M14b Windows PE64 fix** — exit code bug, **30 минут работы**. WIP.
- [ ] **7.2 macOS Mach-O target** — есть скелет в [src/core/forge_macho.luna](../src/core/forge_macho.luna). ~500 строк адаптации под bootminor.
- [ ] **7.3 ARM64 codegen** — есть скелет в [src/core/forge_arm64.luna](../src/core/forge_arm64.luna). **XL effort.** Открывает: AWS Graviton, Apple Silicon, embedded.
- [ ] **7.4 WebAssembly target** — emit WASM bytecode. ~1500 строк. Открывает: Cloudflare Workers, browser apps, edge.
- [ ] **7.5 musl/Alpine compatibility** — статически линковка для Alpine контейнеров.
- [ ] **7.6 BSD support** — FreeBSD/OpenBSD syscall map.
- [ ] **7.7 RISC-V** — open hardware future.

---

## TIER 8 — Ecosystem / Community

- [ ] **8.1 Central package registry** — `pkg.lunalang.io`. Сейчас pkg тянет с git. ~1500 строк (web service).
- [ ] **8.2 Documentation site** — like pkg.go.dev. ~800 строк.
- [ ] **8.3 Tutorials и cookbook** — Luna by Example. Контент-задача.
- [ ] **8.4 Conference talks / blog posts**. Маркетинг.
- [ ] **8.5 Stack Overflow / Discord** community.
- [ ] **8.6 Quality stdlib reviews** — peer review std/* modules.
- [ ] **8.7 Stability / SemVer** — для public API.
- [ ] **8.8 Security policy** — vulnerability reporting + CVE process.
- [ ] **8.9 GitHub Actions templates** — для CI пользовательских проектов.

---

## TIER 9 — GUI / Desktop / Mobile

- [ ] **9.1 Native GUI** — `std/ext/gui/native.luna`. Скелет в [src/stdlib/gui_native.luna](../src/stdlib/gui_native.luna).
- [ ] **9.2 Web GUI (WASM)** — зависит от 7.4.
- [ ] **9.3 Terminal UI** — `std/std/tui.luna`. ncurses-style. ~600 строк.
- [ ] **9.4 Mobile (iOS/Android)** — Swift/Kotlin bridges. **Многомесячная работа.**

---

## Приоритетный execution order

### Спринт 1 (СЕЙЧАС): SQLite via subprocess
- `std/ext/db/sqlite.luna` — pattern как `https.luna`, через `sqlite3` CLI
- Тесты: create table, insert, query, transaction
- Example: rewrite `examples/net/chat_server.luna` на SQLite вместо kvstore

### Спринт 2-3: SQLite FFI (production)
- Extend bootminor с dlopen-style линкованием (PT_INTERP + DT_NEEDED + dynamic linker stub)
- Replace subprocess версия

### Спринт 4: Redis client
- Redis RESP protocol — простой parsable text
- Поверх tcp.luna

### Спринт 5: structured logging + pre-fork pool

### Спринт 6-8: TLS in-process через libssl FFI (production)

### Спринт 9-10: math + regex

### Спринт 11-15: LSP server (большая работа)

### Спринт 16+: остальное по приоритету пользователя

---

## Метрики готовности (целевые)

| Метрика | Сейчас | Цель Tier 1 done | Цель Go-parity |
|---|---|---|---|
| Stdlib модулей | 21 | 30 | 80+ |
| Бекенд on Luna | chat 200 req, 0 leak | продакшн чат на 10k активных | 1M+ MAU |
| RPS sustained | ~3000 (fork) | ~30000 (pool) | ~50000 |
| Platforms | Linux x86-64 | + Windows + macOS | + ARM64 + WASM |
| BD options | kvstore | SQLite + Redis | + Postgres + MySQL + Mongo |
| Crypto | sha256/512+HMAC | + AES, ED25519, JWT | + full TLS |
| IDE support | syntax highlight | + LSP | + debugger |

---

## Как обновлять этот файл

При завершении пункта:
1. Меняй `[ ]` на `[x]`
2. Указывай дату завершения и commit hash
3. Если open dependencies снимаются — отмечай и в зависимых пунктах

Пример:
```markdown
- [x] **1.1 SQLite через subprocess** — `std/ext/db/sqlite.luna`. **DONE 2026-05-11** (commit a3f2c91). 8 unit tests + 1 E2E.
```
