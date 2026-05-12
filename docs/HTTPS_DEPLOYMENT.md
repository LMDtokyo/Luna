# HTTPS / TLS in Luna

Luna has **two TLS stories**, depending on whether you need *outbound*
HTTPS (calling APIs) or *inbound* HTTPS (serving users).

---

## Outbound — `std/ext/https`

Available today. Calls the system `openssl s_client` as a subprocess
to tunnel a single HTTP request through TLS. Works for one-shot API
calls; not suitable for long-lived connections or streaming.

```luna
import https

fn main() -> int
    let @r: HttpResponse = https_get("api.telegram.org", "/bot<TOKEN>/getMe")
    if @r.status == 200
        shine(@r.body)
    return 0
```

```luna
fn post_msg() -> int
    @body = "{\"chat_id\":123,\"text\":\"hi\"}"
    let @r: HttpResponse = https_post("api.telegram.org", "/bot<TOKEN>/sendMessage", @body, "application/json")
    return @r.status
```

**Requirements on the host**: `openssl` CLI (every mainstream Linux
distro ships it). The module is tagged `# ffi: openssl-cli` so the
tier linter knows it shells out — bootminor itself stays pure-Luna.

**Performance**: each call forks + execs openssl (~10–50ms overhead).
For bursty traffic this is fine; for thousands of req/s switch to the
in-Luna TLS implementation once that lands (see *Roadmap* below).

---

## Inbound — reverse proxy (nginx / stunnel / Caddy)

For serving HTTPS to browsers, **don't reinvent TLS in Luna**. Run a
production-grade terminator in front and pipe plain HTTP to your
Luna server on localhost. This is what every messenger backend
actually does — Telegram, Signal, Matrix all terminate TLS at a CDN
or load balancer, not in the application.

### nginx — minimal config

```nginx
server {
    listen 443 ssl;
    server_name chat.example.com;

    ssl_certificate     /etc/letsencrypt/live/chat.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/chat.example.com/privkey.pem;

    location / {
        proxy_pass         http://127.0.0.1:18088;
        proxy_set_header   Host              $host;
        proxy_set_header   X-Real-IP         $remote_addr;
        proxy_set_header   X-Forwarded-For   $proxy_add_x_forwarded_for;
        proxy_set_header   X-Forwarded-Proto https;

        # WebSocket upgrade — required for std/ext/websocket
        proxy_http_version 1.1;
        proxy_set_header   Upgrade $http_upgrade;
        proxy_set_header   Connection "upgrade";
        proxy_read_timeout 86400;
    }
}

# Force HTTP -> HTTPS
server {
    listen 80;
    server_name chat.example.com;
    return 301 https://$host$request_uri;
}
```

Certificates from [Let's Encrypt](https://letsencrypt.org/):

```sh
sudo certbot --nginx -d chat.example.com
```

Renewal is automatic via the certbot cron timer.

### stunnel — lighter alternative

For embedded / single-binary deployments where nginx is overkill:

```ini
# /etc/stunnel/luna.conf
[chat]
accept  = 443
connect = 127.0.0.1:18088
cert    = /etc/letsencrypt/live/chat.example.com/fullchain.pem
key     = /etc/letsencrypt/live/chat.example.com/privkey.pem
```

```sh
stunnel /etc/stunnel/luna.conf
```

### Caddy — zero-config HTTPS

```caddy
chat.example.com {
    reverse_proxy 127.0.0.1:18088
}
```

Caddy fetches Let's Encrypt certs itself — one line, no certbot.

---

## What about pure-Luna TLS?

Two paths:

**Path A — libssl FFI (T6).** Add a `dlopen`/`dlsym` story to
bootminor: emit a `.dynamic` section, set `DT_NEEDED` for libssl,
expose `SSL_CTX_new`, `SSL_connect`, `SSL_read`, `SSL_write`.
Estimated work: 2–3 days of bootminor changes + a wrapper in
`std/ext/tls.luna`. Production-grade because TLS itself is delegated
to OpenSSL.

**Path B — TLS 1.3 in pure Luna.** ~3000–4000 lines: X.509 parser,
ECDHE key exchange (Curve25519/P-256), AEAD ciphers (AES-128-GCM /
ChaCha20-Poly1305), HKDF, certificate verification. Big project, but
the goal of "no crutches" lines up. Realistic to ship as a follow-up
module rather than a one-week sprint.

Both will land as `std/ext/tls.luna` (T6, `# ffi: none` for path B,
`# ffi: libssl` for path A) once the bootminor groundwork is in.

---

## Recommendation

- Use **`std/ext/https`** for outbound calls to Telegram / Stripe /
  GitHub / etc — it works today.
- Use **nginx + Let's Encrypt** in front of `router_serve_forking`
  for serving HTTPS to browsers — it's what production does anyway.
- Wait for **`std/ext/tls`** when you need raw HTTPS sockets from
  Luna with no subprocess overhead.
