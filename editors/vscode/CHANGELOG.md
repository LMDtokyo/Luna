# Changelog

## [0.3.0] - 2026-02-02

### Security Fixes (Luna Core v1.1.1)
- **CSPRNG**: Fixed weak RNG fallbacks in `oauth.rs` and `cipher.rs` - now returns error instead of insecure fallback
- **Path Traversal**: Added validation in template engine (`view.rs`) and package manager (`commands.rs`, `registry.rs`)
- **LSP Buffer Overflow**: Added 16MB limit on Content-Length to prevent DoS attacks
- **Integer Overflow**: Added allocation size limits in memory functions, network buffers (shadow, tls, beam)
- **Lexer Panics**: Replaced `unwrap()` calls with safe alternatives, added `Error` token type

### Bug Fixes (Luna Core v1.1.1)
- **Bytecode Compiler**: Fixed `panic!` in `patch_jump()` and `emit_loop()` - now returns proper errors
- **Range Expressions**: Implemented `MakeRange` opcode - ranges now compile correctly instead of `None`
- **PostgreSQL**: Added prepared statements support for SQL injection protection (`query_prepared`, `execute_prepared`)

### Improvements
- Added C callback support for web server route handlers
- Extended HMAC support (SHA-384, SHA-512)
- Improved security for CSRF token generation

## [0.2.0] - 2026-01-15

### Features
- Initial LSP integration
- Syntax highlighting for Luna 77 keywords
- Go to Definition support
- Hover information
- Document symbols (Outline)
- Real-time diagnostics

## [0.1.0] - 2026-01-01

### Initial Release
- Basic syntax highlighting
- Language configuration (comments, brackets)
- File icon theme
