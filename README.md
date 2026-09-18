# Finance Bot
-STILL IN PRODUCTION-

## User and worker setup

The API supports separate users and separate bot settings per symbol. A user's worker only receives that user's active bot configurations.

1. Create a `.env` file in the repository root. Generate the two application secrets with:

```bash
cp .env.example .env
openssl rand -base64 32
openssl rand -base64 32
```

Use the first value for `APP_JWT_SECRET` and the second for `APP_ENCRYPTION_KEY`. Add the worker token values later:

Do not leave the placeholder values in `.env`. `APP_ENCRYPTION_KEY` must be the complete Base64 output from `openssl rand -base64 32`.

```env
APP_JWT_SECRET=replace-with-a-long-random-secret
APP_ENCRYPTION_KEY=replace-with-base64-for-32-random-bytes
MAX_TRADE_QUANTITY=1.0
```

2. Start PostgreSQL and the API without starting workers yet:

```bash
docker compose up -d db app
```

3. Register or log in a user. Save the returned `token` as `$TOKEN` for account actions:

```bash
curl -X POST http://localhost:8080/api/v1/auth/register \
	-H 'Content-Type: application/json' \
	-d '{"email":"you@example.com","password":"a-password-at-least-12-characters"}'
```

4. Store that user's Bitunix credentials through the API. They are encrypted before being stored in PostgreSQL and are never included in bot configuration responses:

```bash
curl -X PUT http://localhost:8080/api/v1/me/exchange-credentials \
	-H "Authorization: Bearer $TOKEN" \
	-H 'Content-Type: application/json' \
	-d '{"apiKey":"your-bitunix-api-key","apiSecret":"your-bitunix-api-secret"}'
```

5. Configure a bot for that user:

```bash
curl -X PUT http://localhost:8080/api/v1/config/BTCUSDT \
	-H "Authorization: Bearer $TOKEN" \
	-H 'Content-Type: application/json' \
	-d '{"active":true,"parameters":{"rsi_period":14,"rsi_overbought":70,"rsi_oversold":30,"timeframe":"1h"}}'
```

6. Create restricted worker tokens using the user's access token:

```bash
curl -X POST http://localhost:8080/api/v1/auth/worker-token \
	-H "Authorization: Bearer $TOKEN"
```

The worker uses `APP_WORKER_SERVICE_TOKEN` and `WORKER_USER_EMAIL_USER1` to load that user's encrypted Bitunix credentials and active strategies from the API. Bitunix API keys and secrets must not be placed in `.env`. `MAX_TRADE_QUANTITY` is only a safety cap; the order quantity comes from the strategy's `Order quantity` field:

```bash
docker compose --profile trading up -d --build cpp-fetcher-user1
```

The worker fetches the user's Bitunix credentials through the protected worker session endpoint and refreshes them periodically. For a second user, repeat the process with `USER2` and start `cpp-fetcher-user2`.

Each strategy now also controls its entry direction (`Long only`, `Short only`, or `Long + Short`), hammer/shooting-star shadow multiplier, and volume-surge multiplier. An entry requires the selected RSI threshold, the matching candle pattern, and the configured volume surge.


docker compose --profile trading up -d