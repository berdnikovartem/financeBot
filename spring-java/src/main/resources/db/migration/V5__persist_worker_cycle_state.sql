ALTER TABLE bot_configs
    ADD COLUMN IF NOT EXISTS last_long_entry_price DOUBLE PRECISION;

ALTER TABLE bot_configs
    ADD COLUMN IF NOT EXISTS last_short_entry_price DOUBLE PRECISION;
