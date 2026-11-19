# Finance Bot pre-migration behavior baseline

**Status:** Frozen Phase 1 baseline
**Scope:** Repository snapshot captured before application analysis; Java API,
PostgreSQL persistence/configuration, web build, and C++ worker behavior relevant
to the AWS deployment migration.
**Safety:** No AWS resources, exchange accounts, trades, Telegram alerts, or
other live external actions were used.

## Baseline execution environment and results

All analysis and commands below used the pre-analysis repository snapshot. The
Java Maven command ran from `spring-java/`; the frontend and C++ build commands
ran from their respective module directories.

| Check | Command | Result |
|---|---|---|
| Java package build | `./mvnw -DskipTests package` | Passed |
| Java test suite | `./mvnw test` | 6 tests discovered: 5 passed, 1 errored |
| Web production build | `npm run build` | Could not run: `tsc` is not installed (`command not found`) |
| C++ worker configure/build | `cmake -S . -B /tmp/financebot-cpp-baseline-build && cmake --build /tmp/financebot-cpp-baseline-build -j2` | Could not run: `cmake` is not installed |

The Java test error is `SpringExampleApplicationTests.contextLoads`: startup
cannot build the JPA entity manager because PostgreSQL at `localhost:5332`
refuses the connection. The other five tests pass. No database was started as
part of this capture. Maven reports the context-load case as an error; it is
counted as one non-passing case in the baseline totals.

The web module declares `build`, `dev`, and `preview` scripts, but no test
script. No C++ test target or C++ test source was found in the snapshot. No
frontend dependencies were installed and no production services were started.

## Executed baseline cases

These six cases are the existing Java test methods executed in the snapshot.
They define the current automated, locally executable baseline; the observed
result column records the actual pre-migration run.

| ID | Behavior / expected result | Observed result |
|---|---|---|
| JAVA-01 | Recording long trade prices 100, 110, and 120 yields an average/last-long price of 110 within the test tolerance. | Passed |
| JAVA-02 | The `take_profit_percentage` parameter is read as 4.0. | Passed |
| JAVA-03 | Encrypting an exchange credential produces ciphertext different from plaintext, and decrypting it recovers the original value. | Passed |
| JAVA-04 | A user access token identifies the user and has `user` scope; a worker token has `worker` scope. | Passed |
| JAVA-05 | The access-token parser rejects a refresh token while the refresh-token parser returns its subject email. | Passed |
| JAVA-06 | The Spring application context loads with its configured persistence dependency available. | Errored: PostgreSQL at `localhost:5332` was unavailable; context initialization failed. |

## API and persistence contract observed in source

These behaviors were recorded from the snapshotted implementation and route
configuration. They were **not** exercised over HTTP in this baseline because
the application requires PostgreSQL and no database service was running.
Post-migration verification should exercise them with an isolated database and
synthetic inputs.

| Area | Existing behavior to preserve |
|---|---|
| Authentication | `POST /api/v1/auth/register` normalizes email, requires a non-empty email and a password of at least 12 characters, returns 400 for invalid input, 409 for an existing user, and 201 with tokens for a new user. `POST /api/v1/auth/login` returns tokens on valid credentials and 401 otherwise. |
| Token renewal | `POST /api/v1/auth/refresh` consumes a refresh token and issues new tokens with its user/worker scope; invalid refresh tokens return 401. Access and refresh tokens are not interchangeable. |
| Browser authentication | Browser register/login/refresh/logout routes use authentication cookies; the browser CSRF route returns a token and stores the CSRF cookie. Cookie secure behavior is configurable. |
| Exchange credentials | `PUT /api/v1/me/exchange-credentials` rejects missing or blank key/secret values with 400; valid values are encrypted for the authenticated user and return 204. |
| Worker session | Worker-only `GET /api/v1/worker/session` returns that user's decrypted exchange credentials when present and 404 when either credential is absent. Worker-only `GET /api/v1/worker/sessions` supplies the worker's session/configuration list. |
| Bot configuration | `/api/v1/config/{symbol}`, `/api/v1/configs/active`, and config update routes require a user or worker role. A missing symbol returns 404; active configurations are returned as a list. |
| Authorization | Registration/login/refresh/logout and browser auth routes are public; user-owned routes require a user role, worker routes require a worker role, and other routes require authentication. |
| Database | PostgreSQL is the persistence dependency. The Compose definition uses PostgreSQL 16; the API's local datasource defaults to `localhost:5332`, and schema migrations V1–V3 are present. The context-load test did not verify schema or repository behavior because the database was unavailable. |

Synthetic request values for future isolated API checks are in
[`testdata/api-fixtures.json`](testdata/api-fixtures.json). They are examples
only, not production credentials.

## Worker and deployment-relevant behavior observed in source

- The Compose stack contains the Java API, web frontend, C++ worker, and
  PostgreSQL. The API and web publish ports 8080 and 3000; PostgreSQL publishes
  5332. The worker and application share the Compose bridge network.
- The worker reads `APP_WORKER_SERVICE_TOKEN`, calls the API's
  `/api/v1/worker/sessions` endpoint, and currently uses the API base URL
  `http://java-api:8080`.
- The worker source also contains outbound Binance market-data,
  Bitunix-trading, and Telegram integrations. The worker was not launched:
  doing so against configured live endpoints could cause external effects.
  Migration tests must mock these integrations and must not place trades or
  send alerts.
- The API datasource defaults in `application.properties` are local
  configuration. Compose supplies its own database environment values and
  requires the JWT, encryption, and worker service-token variables to be set.

## Known baseline limitations

1. API/database runtime behavior is source-inspected but not integration-tested
   because PostgreSQL was unavailable at `localhost:5332`.
2. The frontend build is unverified because TypeScript's `tsc` executable is
   unavailable in the snapshot environment.
3. The C++ worker build and tests are unverified because CMake is unavailable;
   no C++ test suite was found.
4. No live exchange, Telegram, trading, AWS, deployment, or provisioning action
   was performed.
