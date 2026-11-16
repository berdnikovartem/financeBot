# Spring Java Application: Class and Request-Flow Guide

This guide explains the responsibilities of the Java classes under
`src/main/java/com/example`, why they exist, and how the main authentication,
browser, worker, bot-configuration, and asset flows fit together. It describes
the current implementation; it is not a promise that every endpoint or
configuration value is ready for production.

## 1. Application at a glance

The module is a Spring Boot REST application backed by PostgreSQL and Spring
Data JPA. Its main responsibilities are:

1. Register and authenticate users.
2. Issue short-lived signed access tokens and longer-lived refresh tokens.
3. Store users' exchange API credentials encrypted in the database.
4. Let authorized worker clients retrieve decrypted credentials and bot
   configurations.
5. Store and expose per-symbol bot settings, as well as a small asset list.

Spring discovers classes annotated with `@RestController`, `@Service`,
`@Repository`, `@Entity`, and configuration annotations. Constructor-injected
dependencies make each class's collaborators explicit. Controllers translate
HTTP requests into application operations; services perform reusable
operations; repositories provide database access; entities model persisted
state; records and DTOs describe API payloads.

## 2. Important request flows

### 2.1 Registration and ordinary API login

1. A client sends email and password to `POST /api/v1/auth/register` or
   `POST /api/v1/auth/login`.
2. `AuthController` normalizes the email. Registration requires a password of
   at least 12 characters and rejects an existing email. Passwords are hashed
   with the `PasswordEncoder` bean from `SecurityConfig`; the plaintext
   password is not what the `AppUser` entity stores.
3. On successful registration or login, `AuthController` asks `JwtService` to
   create an access token and a refresh token. It stores the refresh token
   through `RefreshTokenService` and returns an `AuthResponse`.
4. On subsequent requests, `JwtAuthenticationFilter` reads the access token
   from an `Authorization: Bearer ...` header or the `ACCESS_TOKEN` cookie.
   `JwtService` validates and parses it; the filter places the email and
   `ROLE_USER` or `ROLE_WORKER` in Spring Security's request context.
5. `SecurityConfig` applies the route authorization rules. The application is
   configured as stateless: authentication is reconstructed for each request
   from its token rather than stored in a server-side HTTP session.

### 2.2 Refreshing and revoking tokens

1. The client submits a refresh token to `POST /api/v1/auth/refresh`. Browser
   refresh uses `POST /api/v1/auth/browser/refresh` and the `REFRESH_TOKEN`
   cookie instead.
2. `JwtService` checks the JWT signature, expiry, token type, and scope. The
   refresh endpoint finds the user named by the token.
3. `RefreshTokenService.consume` hashes the presented token and looks up its
   stored record. It rejects a type mismatch, revoked token, expired token, or
   unknown token. A valid token is revoked as it is consumed.
4. `AuthController` issues a replacement token pair for the same scope. A
   refresh token can therefore be used once; clients must keep the replacement
   returned by a successful refresh.
5. Logout revokes the refresh token if supplied. Browser logout additionally
   expires the authentication cookies.

### 2.3 Saving exchange credentials and loading worker sessions

1. An authenticated user sends `PUT /api/v1/me/exchange-credentials` with
   `apiKey` and `apiSecret`.
2. `ExchangeCredentialsController` rejects missing or blank fields and uses
   `EncryptionService` to encrypt both values before saving the `AppUser`.
3. A client with the `WORKER` role can call
   `GET /api/v1/worker/session` for the authenticated user's credentials, or
   `GET /api/v1/worker/sessions` for all users with stored credentials.
4. The worker controller decrypts the saved values and returns a
   `WorkerSessionResponse`. The plural endpoint also adds active bot
   configurations belonging to each user. Its repository queries filter out
   users and configs without both credentials in PostgreSQL, rather than
   loading every user and filtering in application memory.

The worker endpoints return exchange secrets in their response bodies. They
must only be reachable by trusted worker clients over a protected connection,
and those clients must avoid logging or exposing the response.

### 2.4 Browser authentication and CSRF

Browser register/login use the `/api/v1/auth/browser/...` routes. On success,
`AuthController` sets an HTTP-only `ACCESS_TOKEN` cookie (15 minutes) and
`REFRESH_TOKEN` cookie (30 days, limited to the browser-auth path). The
response body contains only the user's email in a `BrowserAuthResponse`.
`BrowserSecurityController` exposes a CSRF-token endpoint; the SPA sends the
token back using the CSRF header expected by Spring Security.

`SpaCsrfTokenRequestHandler` coordinates Spring's XOR-encoded request
attributes with plain values sent in request headers. Exact CSRF exemptions
and authorization rules are defined in `SecurityConfig`.

## 3. Authentication and security classes

### `AuthController`

The HTTP entry point for account registration, login, refresh, logout, browser
authentication, and user-to-worker token issuance. It coordinates repositories
and services instead of implementing JWT signing, password hashing, or token
storage itself.

Its `/register` and `/login` routes return token pairs in an `AuthResponse`.
Browser routes return a `BrowserAuthResponse` and put those tokens in cookies.
`/worker-token` requires an already authenticated user and issues a token pair
with worker scope. The refresh route distinguishes user and worker refresh
tokens by their scope and issues replacement tokens of the same kind.

The controller also contains small helpers for email normalization,
registration validation, cookie construction, and token issuance. Registration
requires a minimum 12-character password. The configured bootstrap email is
used to claim pre-existing bot configurations that have no user assigned.

### `AuthRequest`

A request record containing `email` and `password`, shared by registration and
login endpoints. A record is useful here because the request is a simple
immutable data shape with no independent behavior.

### `AuthResponse`

A response record containing an access `token` and `refreshToken`. It is used
by the non-browser API, where callers receive and manage tokens themselves.
Browser routes use a different response body and set tokens as cookies.

### `BrowserAuthResponse`

A response record containing the authenticated user's email. Browser login,
register, and refresh do not return token values in JSON; the tokens are set
as cookies by `AuthController`.

### `SecurityConfig`

Defines the Spring Security filter chain and the shared security beans:

- A BCrypt `PasswordEncoder` for password hashing and password verification.
- A `UserDetailsService` that deliberately refuses password-based Spring
  authentication; the application's login logic is in `AuthController`.
- A cookie-backed CSRF token repository and `SpaCsrfTokenRequestHandler`.
- Stateless request handling and frame-options denial.
- Public routes, user-only routes, worker-only routes, and routes shared by
  user and worker roles.
- Placement of `JwtAuthenticationFilter` before Spring's username/password
  authentication filter.

The configuration currently permits `/`, registration/login/refresh/logout,
and browser-auth routes without an authenticated principal.
`/api/v1/auth/worker-token` and `/api/v1/me/**` require `USER`;
`/api/v1/worker/**` requires `WORKER`; selected configuration routes permit
either role; all other routes require authentication.

### `JwtService`

Creates and parses signed JSON Web Tokens. It uses the configured
`app.jwt.secret` as an HMAC signing key and refuses to start if the secret is
shorter than 32 bytes. Tokens contain the user's email as subject, a token type
(`access` or `refresh`), a scope (`user` or `worker`), issue time, and
expiration.

Access tokens last 15 minutes. Refresh tokens last 30 days. The methods for
reading an access-token email and refresh-token email check the token type
before returning the subject. Other methods expose expiration and scope after
signature verification. Database-backed single-use refresh-token state is
handled separately by `RefreshTokenService`.

### `JwtAuthenticationFilter`

A `OncePerRequestFilter` that attempts to authenticate each request once. It
first checks for an internal `X-Worker-Token`; if that matches the configured
`app.worker.service-token`, it creates a `ROLE_WORKER` authentication, using
`X-Worker-User` as the principal name when provided. Otherwise it extracts an
access token from a bearer header or the `ACCESS_TOKEN` cookie, asks
`JwtService` to parse it, and assigns `ROLE_WORKER` for worker scope or
`ROLE_USER` otherwise.

If parsing a provided JWT fails, the filter clears the security context and
continues the filter chain; protected routes will then be rejected by Spring
Security. This filter establishes a request identity; `SecurityConfig`
determines whether that identity is authorized for each route.

### `SpaCsrfTokenRequestHandler`

Implements Spring Security's `CsrfTokenRequestHandler` for a browser SPA. It
uses the XOR handler for request attributes (Spring's BREACH-resistant token
representation) and resolves a value from the configured CSRF header using the
plain handler. When the request has no header value, it resolves the
XOR-encoded request parameter. It also forces token access during request
handling so the cookie-backed token can be materialized.

### `BrowserSecurityController`

Exposes `GET /api/v1/auth/browser/csrf`. It retrieves the CSRF token from the
request attribute or cookie repository; if neither exists, it generates and
saves a token. The returned JSON map has a `token` field for the browser to
send on later state-changing requests.

### `RefreshRequest`

A request record whose `refreshToken` field is used by the non-cookie
refresh/logout endpoints. Browser refresh and logout instead read the token
from the `REFRESH_TOKEN` cookie.

### `RefreshTokenService`

Manages the server-side, revocable record associated with a refresh JWT. It
stores a SHA-256 hash of the raw token rather than the bearer value itself.
`consume` finds the hash, validates token scope/type, revocation state, and
expiration, then revokes the record. The repository lookup is pessimistically
locked and `consume` is transactional to reduce the chance of two simultaneous
requests consuming the same token successfully. `revoke` marks a matching
record revoked if one exists.

This database record supplements JWT signature and expiry checks: a signed JWT
by itself cannot be revoked before its expiration.

### `RefreshTokenRepository`

A Spring Data JPA repository for `RefreshToken` rows. The derived
`findByTokenHash` query has a pessimistic write lock, used by the transactional
consume operation to serialize competing refresh attempts for the same row.

### `RefreshToken`

The JPA entity for refresh-token state. It associates a token hash with an
`AppUser`, records its scope/type, expiry, creation time, and optional
revocation time. The raw refresh-token string is not stored in this entity.
`revoke()` stamps the revocation time.

## 4. Exchange credentials and worker classes

### `ExchangeCredentialsController`

Handles `PUT /api/v1/me/exchange-credentials`. It gets the authenticated user
from Spring Security, validates that both credentials are non-null and
non-blank, encrypts them with `EncryptionService`, and saves the changed user.
It returns `400 Bad Request` for invalid input and `204 No Content` on success.
The `/api/v1/me/**` route requires `USER` according to `SecurityConfig`.

### `ExchangeCredentialsRequest`

A request record containing an exchange `apiKey` and `apiSecret`. It separates
the JSON request shape from the entity and prevents controller input from
being treated as an entire `AppUser` object.

### `EncryptionService`

Encrypts and decrypts exchange credentials using AES-256-GCM. It requires
`app.encryption.key` to be Base64 that decodes to exactly 32 bytes. Encryption
generates a fresh 12-byte random IV for each value and stores the IV followed
by the authenticated ciphertext, Base64-encoded. Decryption reverses that
format and authenticates the ciphertext before returning the plaintext.

The encryption key must remain stable and confidential: losing it prevents
the application from decrypting existing credentials; exposing it may expose
all credentials stored with it. It should be supplied through protected
deployment configuration, not committed to source control.

### `WorkerSessionController`

Handles `GET /api/v1/worker/session`, the singular, current-principal endpoint.
It uses the authenticated principal's name (the user's email) to load one
`AppUser`. If either encrypted credential is absent, it returns `404 Not
Found`. Otherwise it decrypts the API key and secret and returns them with an
empty configuration list.

This endpoint is useful when a worker token is scoped to one user's account.
It does not load that user's `BotConfig` records. The path requires the
`WORKER` role.

### `WorkerSessionsController`

Handles `GET /api/v1/worker/sessions`, the plural endpoint for a worker that
needs to initialize multiple user sessions. It queries active bot
configurations whose users have both exchange credentials, groups them by user
ID, then queries only users with both credentials. For each qualifying user it
decrypts the credentials and serializes that user's active configs as
`BotConfigResponse` objects. Users without active configs still appear with an
empty config list. This route also requires `WORKER`.

Unlike the singular endpoint, the plural endpoint is not scoped to the current
principal: it returns credentials for every qualifying user. The worker
authorization boundary is therefore especially important.

The endpoint still returns all qualifying sessions in one JSON array. The
database-side filters avoid loading unrelated users and configs, but do not
bound response size when many users have credentials. The C++ worker currently
expects an array, so adding pagination would require a coordinated worker
client change. For very large numbers of configured accounts, pagination or a
batch/stream protocol remains a separate scalability improvement.

### `WorkerSessionResponse`

A response record shared by both worker endpoints. Its fields are `email`,
plaintext `apiKey`, plaintext `apiSecret`, and a list of
`BotConfigResponse` configs. Credentials are decrypted only while constructing
the response; the database fields remain encrypted.

## 5. User and persistence classes

### `AppUser`

The JPA entity for the `app_users` table. It stores a generated ID, unique
email, password hash, and nullable encrypted Bitunix API key and secret. Its
credential setters are used after encryption; callers reading those fields
receive the encrypted database representation. The protected no-argument
constructor is for JPA, while the public constructor creates a user from an
email and password hash.

### `AppUserRepository`

A Spring Data repository for `AppUser`. Besides standard `JpaRepository`
operations, it provides `findByEmail` for authentication and principal lookup,
and `existsByEmail` for duplicate-registration checks.

## 6. Bot configuration classes

### `BotConfig`

The JPA entity for a bot's settings for a symbol. It stores the symbol,
active state, optional owning user, price-step percentage, and a JSONB
`parameters` map. The map allows strategy-specific settings (RSI, timeframe,
take-profit, trade quantity, hammer, and volume indicators) to be persisted
without a separate database column for every value.

The user relationship is optional, which supports legacy/unclaimed
configurations. The entity exposes convenience getters with defaults when
parameters are absent; for example, RSI period defaults to 14 and timeframe
defaults to `1h`. Indicator-list getters cap the returned list at three
entries. API-facing setters for percentage and quantity values store the
corresponding values in `parameters`; the separate override fields are
transient and are not persisted. It also tracks average long and short entry
prices, with per-side trade counts persisted separately so averages continue
correctly after the entity is loaded again. Migration
`V3__add_bot_config_trade_counts.sql` adds the count columns. The
`take_profit_percentage` getter accepts the percentage key in existing
parameter maps as well as the normalized `take_profit_pct` fraction.

### `BotConfigRepository`

A Spring Data repository for bot settings. Its derived queries support lookup
by symbol, lookup by user and symbol, active-config listing, finding unowned
legacy configs, and finding active configs that have an owner. The two worker
controllers use these queries to retrieve configurations appropriate to
their endpoints.

### `BotConfigController`

Serves bot-configuration routes under `/api/v1`:

- `GET /config/{symbol}` returns the matching config or `404`.
- `PUT /config/{symbol}` creates or updates a config. For a `ROLE_USER`
  principal it looks up the current user and works with that user's config;
  for a non-user principal (including a worker) it uses the symbol-only
  lookup. It copies parameters and selected top-level values from the request
  body, then saves the result.
- `GET /configs/active` returns active configs converted to
  `BotConfigResponse`.

Security rules for these routes are in `SecurityConfig`; selected config
routes can be used by either user or worker roles. This controller is also
where the stored `BotConfig` entity is translated into a stable API response
shape for the active-config route.

### `BotConfigResponse`

A response DTO for one config. It contains the symbol, active flag, price-step
percentage, take-profit percentage, trade quantity, and nested `ParametersDto`.
The nested DTO carries the strategy parameters and indicator lists used by
clients and workers. A DTO keeps the response contract distinct from the JPA
entity, whose persistence details and JSON mapping need not be exposed
directly.

### `BotConfigRepository` and user scoping

The schema migration creates a partial unique index on `(user_id, symbol)`
for rows with a non-null user, and a foreign key from `bot_configs.user_id` to
`app_users.id`. `AuthController` can attach old user-less configurations to
the configured bootstrap account at registration. This supports migration
from older, globally stored configurations to user-owned settings.

## 7. Asset classes

### `Asset`

A small JPA entity with a generated integer ID and a name. It models rows in
the `asset` table. It includes ordinary accessors and value-based `equals`
and `hashCode` implementations.

### `AssetRepository`

A Spring Data JPA repository for `Asset`. It inherits standard operations such
as `findAll` and `save`; no custom query methods are currently declared.

### `AssetService`

A thin service around `AssetRepository`. It provides `getAssets()` and
`insertNewAsset()` so the controller does not directly own database access.
The service is a natural place for asset-related business rules if those are
added later.

### `AssetController`

Exposes `GET /api/assets` to list assets and `POST /api/assets` to save a
submitted asset. The controller delegates to `AssetService`. These routes are
not listed among the public exceptions in `SecurityConfig`, so the general
authenticated-request rule applies.

## 8. Application bootstrap

### `SpringExampleApplication`

The `@SpringBootApplication` entry point. Its `main` method starts the Spring
application and component scanning. The class also exposes a simple `GET /`
route returning a greeting; `/` is permitted by `SecurityConfig`.

## 9. Route and role summary

| Route | Purpose | Access behavior |
|---|---|---|
| `POST /api/v1/auth/register` | Register API client | Public |
| `POST /api/v1/auth/login` | API login | Public |
| `POST /api/v1/auth/refresh` | Rotate a submitted refresh token | Public endpoint; token is validated by application logic |
| `POST /api/v1/auth/logout` | Revoke submitted refresh token | Public endpoint; token is optional |
| `POST /api/v1/auth/browser/register` | Browser registration and cookies | Public |
| `POST /api/v1/auth/browser/login` | Browser login and cookies | Public |
| `POST /api/v1/auth/browser/refresh` | Refresh using browser cookie | Public endpoint; token is validated by application logic |
| `POST /api/v1/auth/browser/logout` | Revoke token and expire cookies | Public |
| `GET /api/v1/auth/browser/csrf` | Get or create browser CSRF token | Public |
| `POST /api/v1/auth/worker-token` | Issue worker token pair | `USER` |
| `/api/v1/me/**` | Current user's endpoints, including credentials | `USER` |
| `GET /api/v1/worker/session` | Current principal's worker session | `WORKER` |
| `GET /api/v1/worker/sessions` | All configured worker sessions | `WORKER` |
| `/api/v1/config/**`, `/api/v1/configs/**` | Bot config operations | `USER` or `WORKER` |
| `/api/assets` | List or add assets | Authenticated by default |
| `/` | Greeting | Public |

This table summarizes the current `SecurityConfig`; endpoint-specific CSRF
rules are separate from role authorization.

## 10. Configuration and operational notes

The code expects these application properties:

- `app.jwt.secret`: signing secret, at least 32 bytes.
- `app.encryption.key`: Base64-encoded 32-byte AES key.
- `app.worker.service-token`: secret for internal worker authentication.
- `app.cookies.secure`: whether browser cookies use the `Secure` attribute.
- `app.bootstrap.email`: optional email allowed to claim legacy unowned bot
  configs; the source currently defines a default.

Database connection properties are also required for the PostgreSQL-backed
repositories. In deployment, credentials and cryptographic keys should come
from a secret manager or protected environment configuration. Do not copy
production values into this guide or commit them to source control.

`spring-java/src/main/resources/db/migration` contains SQL migrations for the
application tables and user-scoped bot configuration constraints. When
changing entities or response formats, check both the migration/schema and
the clients consuming those API fields.

## 11. Reading the code in a useful order

For the authentication path, start with `SecurityConfig`, then
`JwtAuthenticationFilter`, `JwtService`, and `AuthController`. For refresh
token persistence, continue with `RefreshTokenService`, `RefreshToken`, and
`RefreshTokenRepository`. For worker setup, read `ExchangeCredentialsController`
and `EncryptionService`, then `WorkerSessionController` and
`WorkerSessionsController`. Finally, inspect `BotConfig`, its repository,
controller, and response DTO to understand the configs attached to plural
worker sessions.

The API request/response records and the user repository/entity are useful
references alongside those flows. The asset classes form a separate, smaller
CRUD path.
