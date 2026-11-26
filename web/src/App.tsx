import { FormEvent, PointerEvent as ReactPointerEvent, useEffect, useRef, useState } from "react";
import { createPortal } from "react-dom";
import {
  Activity,
  ArrowRight,
  Bot,
  Check,
  ChevronRight,
  CircleHelp,
  KeyRound,
  LogOut,
  Plus,
  RefreshCw,
  ShieldCheck,
  SlidersHorizontal,
  Sparkles,
  Trash2,
  Wallet,
  X,
} from "lucide-react";
import * as api from "./api";
import type { BotConfig, BotDraft, HammerIndicator, RsiIndicator, VolumeIndicator } from "./api";

type View = "overview" | "bots" | "credentials";
type AuthMode = "login" | "register";

const emptyDraft: BotDraft = {
  active: true,
  price_step_percentage: 0,
  take_profit_percentage: 5,
  trade_quantity: 0.001,
  parameters: {
    rsi_period: 14,
    rsi_overbought: 70,
    rsi_oversold: 30,
    timeframe: "1h",
    hammer_multiplier: 1.5,
    volume_surge_multiplier: 1.5,
    trading_side: "BOTH",
    take_profit_scope: "CYCLE",
    rsi_indicators: [{ period: 14, overbought: 70, oversold: 30, timeframe: "1h" }],
    hammer_indicators: [{ timeframe: "1h", multiplier: 1.5 }],
    volume_indicators: [{ timeframe: "1h", multiplier: 1.5 }],
  },
};

function App() {
  const [email, setEmail] = useState<string | null>(
    sessionStorage.getItem("finance-email"),
  );
  const [bots, setBots] = useState<BotConfig[]>([]);
  const [view, setView] = useState<View>("overview");
  const [selectedBot, setSelectedBot] = useState<BotConfig | null>(null);
  const [newStrategySymbol, setNewStrategySymbol] = useState<string | null>(null);
  const [authMode, setAuthMode] = useState<AuthMode>("login");
  const [loading, setLoading] = useState(true);
  const [botsError, setBotsError] = useState<string | null>(null);
  const [retryingBots, setRetryingBots] = useState(false);
  const [notice, setNotice] = useState<{
    type: "success" | "error";
    text: string;
  } | null>(null);

  async function loadBots() {
    setBotsError(null);
    for (let attempt = 0; attempt < 5; attempt += 1) {
      try {
        setBots(await api.getBots());
        setBotsError(null);
        setLoading(false);
        return;
      } catch (error) {
        if (error instanceof api.ApiError && (error.status === 401 || error.status === 403)) {
          try {
            const session = await api.refresh();
            setEmail(session.email);
            sessionStorage.setItem("finance-email", session.email);
            setBots(await api.getBots());
            setLoading(false);
            return;
          } catch {
            setEmail(null);
            setLoading(false);
            return;
          }
        }
        if (attempt < 4) {
          await new Promise((resolve) =>
            window.setTimeout(resolve, 500 * (attempt + 1)),
          );
          continue;
        }
        setBotsError("We couldn't load your strategies. Check your connection and try again.");
      }
    }
    setLoading(false);
  }

  async function retryLoadBots() {
    setRetryingBots(true);
    try {
      await loadBots();
    } finally {
      setRetryingBots(false);
    }
  }

  useEffect(() => {
    void loadBots();
  }, []);

  function showNotice(type: "success" | "error", text: string) {
    setNotice({ type, text });
    window.setTimeout(() => setNotice(null), 4200);
  }

  async function handleAuth(authEmail: string, password: string) {
    try {
      const session =
        authMode === "login"
          ? await api.login(authEmail, password)
          : await api.register(authEmail, password);
      setEmail(session.email);
      sessionStorage.setItem("finance-email", session.email);
      await loadBots();
      showNotice(
        "success",
        authMode === "login"
          ? "Welcome back to your control room."
          : "Your account is ready.",
      );
    } catch (error) {
      showNotice(
        "error",
        error instanceof Error ? error.message : "Authentication failed",
      );
    }
  }

  async function handleLogout() {
    await api.logout().catch(() => undefined);
    sessionStorage.removeItem("finance-email");
    setEmail(null);
    setBots([]);
  }

  async function handleSave(symbol: string, draft: BotDraft) {
    try {
      const saved = await api.saveBot(symbol.trim().toUpperCase(), draft);
      setBots((current) => {
        const existing = current.find((bot) => bot.symbol === saved.symbol);
        return existing
          ? current.map((bot) => (bot.symbol === saved.symbol ? saved : bot))
          : [...current, saved];
      });
      setSelectedBot(null);
      setNewStrategySymbol(null);
      setView("overview");
      showNotice("success", `${saved.symbol} strategy saved.`);
    } catch (error) {
      showNotice(
        "error",
        error instanceof Error ? error.message : "Could not save the strategy",
      );
    }
  }

  async function handleDelete(symbol: string) {
    const confirmed = window.confirm(
      `Delete the ${symbol} strategy and its saved configuration? This does not close or cancel any live Bitunix positions or orders.`,
    );
    if (!confirmed) return;
    try {
      await api.deleteBot(symbol);
      setBots((current) => current.filter((bot) => bot.symbol !== symbol));
      setSelectedBot(null);
      setNewStrategySymbol(null);
      setView("overview");
      showNotice("success", `${symbol} strategy and saved configuration deleted.`);
    } catch (error) {
      showNotice(
        "error",
        error instanceof Error ? error.message : "Could not delete the strategy",
      );
    }
  }

  if (loading) return email ? <AppSkeleton /> : <LoadingScreen />;
  if (!email)
    return (
      <AuthScreen
        mode={authMode}
        onModeChange={setAuthMode}
        onSubmit={handleAuth}
        notice={notice}
      />
    );

  return (
    <div className="app-shell">
      <div className="ambient-background" aria-hidden="true">
        <span className="ambient-orb orb-one" />
        <span className="ambient-orb orb-two" />
        <span className="ambient-orb orb-three" />
      </div>
      <Sidebar
        email={email}
        view={view}
        onViewChange={(nextView) => {
          setView(nextView);
          setNewStrategySymbol(null);
          setSelectedBot(null);
        }}
        onLogout={handleLogout}
      />
      <main className="main-content">
        <div className="mobile-topbar">
          <span className="brand-mark small">A</span>
          <span className="mobile-topbar-title">
            <strong>Averty Control</strong>
            <small>{view === "overview" ? "OVERVIEW" : view === "bots" ? "STRATEGIES" : "EXCHANGE ACCESS"}</small>
          </span>
          <button onClick={handleLogout} aria-label="Log out">
            <LogOut size={17} />
          </button>
        </div>
        {notice && <Notice notice={notice} />}
        {(botsError || retryingBots) && (
          <ControlPlaneError
            message={botsError ?? "Checking the connection and loading your strategies…"}
            retrying={retryingBots}
            onRetry={retryLoadBots}
          />
        )}
        {view === "overview" && (
          <Overview
            bots={bots}
            hasBotsError={Boolean(botsError)}
            onCreate={() => {
              setSelectedBot(null);
              setNewStrategySymbol(null);
              setView("bots");
            }}
            onCreateStrategy={(symbol) => {
              const existingBot = bots.find((bot) => bot.symbol === symbol);
              setSelectedBot(existingBot ?? null);
              setNewStrategySymbol(existingBot ? null : symbol);
              setView("bots");
            }}
            onOpenBot={(bot) => {
              setSelectedBot(bot);
              setNewStrategySymbol(null);
              setView("bots");
            }}
          />
        )}
        {view === "bots" && (
          <BotsView
            bots={bots}
            selectedBot={selectedBot}
            initialSymbol={newStrategySymbol}
            hasBotsError={Boolean(botsError)}
            onSelect={setSelectedBot}
            onSave={handleSave}
            onDelete={handleDelete}
            onCancel={() => {
              setSelectedBot(null);
              setNewStrategySymbol(null);
            }}
          />
        )}
        {view === "credentials" && (
          <CredentialsView
            onSaved={() =>
              showNotice("success", "Exchange credentials encrypted and saved.")
            }
          />
        )}
      </main>
      <MobileNav
        view={view}
        onViewChange={(nextView) => {
          setView(nextView);
          setNewStrategySymbol(null);
          setSelectedBot(null);
        }}
      />
    </div>
  );
}

function LoadingScreen() {
  return (
    <div className="loading-screen">
      <span className="brand-mark">A</span>
      <span>Loading your control room</span>
    </div>
  );
}

function AppSkeleton() {
  return (
    <div className="app-shell app-skeleton" aria-label="Loading control room" aria-busy="true">
      <aside className="sidebar skeleton-sidebar">
        <span className="skeleton-block skeleton-brand" />
        <span className="skeleton-block skeleton-workspace" />
        <span className="skeleton-block skeleton-nav" />
        <span className="skeleton-block skeleton-nav" />
        <span className="skeleton-block skeleton-nav" />
        <span className="skeleton-block skeleton-service" />
      </aside>
      <main className="main-content">
        <div className="skeleton-block skeleton-heading" />
        <div className="skeleton-metrics">
          <span className="skeleton-block" />
          <span className="skeleton-block" />
          <span className="skeleton-block" />
        </div>
        <div className="skeleton-block skeleton-market" />
        <div className="skeleton-block skeleton-section" />
        <div className="skeleton-table">
          <span className="skeleton-block" />
          <span className="skeleton-block" />
          <span className="skeleton-block" />
        </div>
        <span className="sr-only" role="status">Loading your control room…</span>
      </main>
    </div>
  );
}

function ControlPlaneError({
  message,
  retrying,
  onRetry,
}: {
  message: string;
  retrying: boolean;
  onRetry: () => void;
}) {
  return (
    <div className="control-plane-error" role="alert">
      <span className="control-plane-error-icon"><X size={15} /></span>
      <div>
        <strong>Connection issue</strong>
        <p>{message}</p>
      </div>
      <button className="secondary-button" onClick={onRetry} disabled={retrying}>
        <RefreshCw size={14} className={retrying ? "spin" : ""} />
        {retrying ? "Retrying…" : "Try again"}
      </button>
    </div>
  );
}

function AuthScreen({
  mode,
  onModeChange,
  onSubmit,
  notice,
}: {
  mode: AuthMode;
  onModeChange: (mode: AuthMode) => void;
  onSubmit: (email: string, password: string) => void;
  notice: { type: "success" | "error"; text: string } | null;
}) {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [submitting, setSubmitting] = useState(false);
  async function submit(event: FormEvent) {
    event.preventDefault();
    setSubmitting(true);
    await onSubmit(email, password);
    setSubmitting(false);
  }
  return (
    <div className="auth-page">
      <div className="auth-visual">
        <div className="ambient-background" aria-hidden="true">
          <span className="ambient-orb orb-one" />
          <span className="ambient-orb orb-two" />
          <span className="ambient-orb orb-three" />
        </div>
        <div className="visual-grid" />
        <div className="visual-copy">
          <span className="eyebrow">AVERTY / TRADING SYSTEM</span>
          <h1>Make every signal deliberate.</h1>
          <p>A quiet command center for strategies that run with discipline.</p>
          <div className="signal-card">
            <div>
              <span className="signal-dot" />
              STRATEGY PREVIEW
            </div>
            <strong>
              RSI <span>+ VOLUME</span>
            </strong>
            <div className="signal-line">
              <i />
              <i />
              <i />
              <i />
              <i />
              <i />
              <i />
            </div>
          </div>
        </div>
      </div>
      <div className="auth-panel">
        <div className="auth-brand">
          <span className="brand-mark">A</span>
          <span>Averty Control</span>
        </div>
        <div className="auth-heading">
          <span className="eyebrow">PRIVATE WORKSPACE</span>
          <h2>{mode === "login" ? "Welcome back." : "Open your workspace."}</h2>
          <p>
            {mode === "login"
              ? "Sign in to manage your strategies."
              : "Set up your private trading control room."}
          </p>
        </div>
        {notice && <Notice notice={notice} />}
        <form onSubmit={submit} className="auth-form">
          <label>
            Email address
            <input
              type="email"
              required
              autoComplete="email"
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              placeholder="you@example.com"
            />
          </label>
          <label>
            Password
            <input
              type="password"
              required
              minLength={12}
              autoComplete={
                mode === "login" ? "current-password" : "new-password"
              }
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              placeholder="At least 12 characters"
            />
          </label>
          <button className="primary-button wide" disabled={submitting}>
            {submitting
              ? "Securing session..."
              : mode === "login"
                ? "Enter control room"
                : "Create workspace"}
            <ArrowRight size={17} />
          </button>
        </form>
        <div className="auth-switch">
          {mode === "login" ? "New to Averty?" : "Already have an account?"}{" "}
          <button
            onClick={() =>
              onModeChange(mode === "login" ? "register" : "login")
            }
          >
            {mode === "login" ? "Create account" : "Sign in"}
          </button>
        </div>
        <div className="security-note">
          <ShieldCheck size={16} />
          <span>
            Sessions are protected with HttpOnly cookies and encrypted exchange
            credentials.
          </span>
        </div>
      </div>
    </div>
  );
}

function Sidebar({
  email,
  view,
  onViewChange,
  onLogout,
}: {
  email: string;
  view: View;
  onViewChange: (view: View) => void;
  onLogout: () => void;
}) {
  return (
    <aside className="sidebar">
      <div className="sidebar-brand">
        <span className="brand-mark">A</span>
        <div>
          <strong>Averty</strong>
          <small>CONTROL ROOM</small>
        </div>
      </div>
      <div className="workspace-switcher">
        <span className="avatar">{email[0].toUpperCase()}</span>
        <div>
          <strong>{email.split("@")[0]}</strong>
          <small>Personal workspace</small>
        </div>
        <ChevronRight size={16} />
      </div>
      <nav>
        <span className="nav-label">Workspace</span>
        <NavItem
          icon={<Activity size={17} />}
          label="Overview"
          active={view === "overview"}
          onClick={() => onViewChange("overview")}
        />
        <NavItem
          icon={<Bot size={17} />}
          label="Strategies"
          active={view === "bots"}
          onClick={() => onViewChange("bots")}
        />
        <NavItem
          icon={<KeyRound size={17} />}
          label="Exchange access"
          active={view === "credentials"}
          onClick={() => onViewChange("credentials")}
        />
      </nav>
      <div className="sidebar-bottom">
        <div className="service-status">
          <span className="status-dot" />
          <div>
            <strong>Control plane online</strong>
            <small>API connection healthy</small>
          </div>
        </div>
        <button className="logout-button" onClick={onLogout}>
          <LogOut size={16} />
          Log out
        </button>
      </div>
    </aside>
  );
}

function MobileNav({
  view,
  onViewChange,
}: {
  view: View;
  onViewChange: (view: View) => void;
}) {
  return (
    <nav className="mobile-nav" aria-label="Primary navigation">
      <NavItem
        icon={<Activity size={18} />}
        label="Overview"
        active={view === "overview"}
        onClick={() => onViewChange("overview")}
      />
      <NavItem
        icon={<Bot size={18} />}
        label="Strategies"
        active={view === "bots"}
        onClick={() => onViewChange("bots")}
      />
      <NavItem
        icon={<KeyRound size={18} />}
        label="Access"
        active={view === "credentials"}
        onClick={() => onViewChange("credentials")}
      />
    </nav>
  );
}

function NavItem({
  icon,
  label,
  active,
  onClick,
}: {
  icon: React.ReactNode;
  label: string;
  active: boolean;
  onClick: () => void;
}) {
  return (
    <button
      className={`nav-item ${active ? "active" : ""}`}
      onClick={onClick}
      aria-current={active ? "page" : undefined}
    >
      {icon}
      <span>{label}</span>
      {active && <span className="nav-pip" />}
    </button>
  );
}

function Notice({
  notice,
}: {
  notice: { type: "success" | "error"; text: string };
}) {
  return (
    <div className={`notice ${notice.type}`}>
      <span>
        {notice.type === "success" ? <Check size={15} /> : <X size={15} />}
      </span>
      {notice.text}
    </div>
  );
}

function Overview({
  bots,
  hasBotsError,
  onCreate,
  onCreateStrategy,
  onOpenBot,
}: {
  bots: BotConfig[];
  hasBotsError: boolean;
  onCreate: () => void;
  onCreateStrategy: (symbol: string) => void;
  onOpenBot: (bot: BotConfig) => void;
}) {
  const active = bots.filter((bot) => bot.active).length;
  return (
    <>
      <PageHeader
        eyebrow="OVERVIEW"
        title="Good to see you."
        description="A quick read on your automated strategies."
        action={
          <button className="primary-button" onClick={onCreate}>
            <Plus size={17} />
            New strategy
          </button>
        }
      />
      <section className="metric-grid">
        <Metric
          icon={<Activity />}
          label="Active strategies"
          value={String(active).padStart(2, "0")}
          detail={active ? "Running on schedule" : "Ready to configure"}
          tone="green"
        />
        <Metric
          icon={<Bot />}
          label="Tracked symbols"
          value={String(bots.length).padStart(2, "0")}
          detail="Across your workspace"
          tone="amber"
        />
        <Metric
          icon={<ShieldCheck />}
          label="System state"
          value="Ready"
          detail="Authentication protected"
          tone="blue"
        />
      </section>
      <MarketOverview bots={bots} onCreateStrategy={onCreateStrategy} />
      <section className="section-heading">
        <div>
          <span className="eyebrow">YOUR STRATEGIES</span>
          <h3>Strategy pulse</h3>
        </div>
        <button
          className="text-button"
          onClick={() => onOpenBot(bots[0])}
          disabled={!bots.length}
        >
          View all <ArrowRight size={15} />
        </button>
      </section>
      {bots.length ? (
        <div className="strategy-pulse-list">
          {bots.slice(0, 4).map((bot) => (
            <button
              key={bot.id}
              className="strategy-pulse-row"
              onClick={() => onOpenBot(bot)}
              aria-label={`Open ${bot.symbol} strategy`}
            >
              <span className="strategy-pulse-market">
                <span className="strategy-market-icon">{bot.symbol.slice(0, 1)}</span>
                <strong>{bot.symbol}</strong>
              </span>
              <span className={`status-pill ${bot.active ? "on" : "off"}`}>
                <i />
                {bot.active ? "Active" : "Paused"}
              </span>
              <span className="strategy-pulse-signals">
                {formatStrategySignals(bot).slice(0, 3).map((signal, index) => (
                  <span className="strategy-signal-chip" key={`${index}-${signal}`}>{signal}</span>
                ))}
                {!formatStrategySignals(bot).length && (
                  <span className="strategy-no-signals">No signals</span>
                )}
              </span>
              <span className="strategy-pulse-updated">{formatStrategyUpdatedAt(bot.updatedAt)}</span>
              <ChevronRight className="strategy-pulse-chevron" size={15} />
            </button>
          ))}
        </div>
      ) : (
        hasBotsError ? (
          <div className="list-empty strategy-load-placeholder">
            Strategies are hidden until the connection is restored.
          </div>
        ) : (
          <EmptyState onCreate={onCreate} />
        )
      )}
    </>
  );
}

type BitunixTicker = {
  symbol: string;
  lastPrice: string;
  open: string;
  quoteVol: string;
};

type BitunixPair = {
  symbol: string;
  base: string;
  quote: string;
  symbolStatus: string;
};

type BitunixResponse<T> = {
  code: number;
  data: T[];
};

type FuturesMarket = {
  symbol: string;
  base: string;
  quote: string;
  price: number | null;
  changePercent: number | null;
  quoteVolume: number;
};

function MarketOverview({
  bots,
  onCreateStrategy,
}: {
  bots: BotConfig[];
  onCreateStrategy: (symbol: string) => void;
}) {
  const [markets, setMarkets] = useState<FuturesMarket[]>([]);
  const [promptMarket, setPromptMarket] = useState<FuturesMarket | null>(null);
  const [updatedAt, setUpdatedAt] = useState<Date | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [refreshing, setRefreshing] = useState(false);
  const [marqueeDuration, setMarqueeDuration] = useState(60);
  const [refreshKey, setRefreshKey] = useState(0);
  const marketTrackRef = useRef<HTMLDivElement>(null);
  const existingStrategy = promptMarket
    ? bots.find((bot) => bot.symbol === promptMarket.symbol)
    : undefined;

  useEffect(() => {
    if (!promptMarket) return;
    function closeOnEscape(event: KeyboardEvent) {
      if (event.key === "Escape") setPromptMarket(null);
    }
    window.addEventListener("keydown", closeOnEscape);
    return () => window.removeEventListener("keydown", closeOnEscape);
  }, [promptMarket]);

  useEffect(() => {
    let disposed = false;
    let timer: number | undefined;
    let activeRequest: AbortController | undefined;

    async function refreshMarkets() {
      const request = new AbortController();
      activeRequest = request;
      setRefreshing(true);
      try {
        const [tickerResponse, pairResponse] = await Promise.all([
          fetch("/market-data/tickers", { cache: "no-store", signal: request.signal }),
          fetch("/market-data/trading_pairs", { cache: "no-store", signal: request.signal }),
        ]);
        if (!tickerResponse.ok || !pairResponse.ok) {
          const response = !tickerResponse.ok ? tickerResponse : pairResponse;
          throw new Error(`Bitunix market data request failed (${response.status})`);
        }

        const [tickerResult, pairResult] = (await Promise.all([
          tickerResponse.json(),
          pairResponse.json(),
        ])) as [BitunixResponse<BitunixTicker>, BitunixResponse<BitunixPair>];
        if (tickerResult.code !== 0 || pairResult.code !== 0) {
          throw new Error("Bitunix returned an unsuccessful market data response.");
        }

        const tickerBySymbol = new Map(tickerResult.data.map((ticker) => [ticker.symbol, ticker]));
        const marketData = pairResult.data
          .filter((pair) => pair.symbolStatus === "OPEN")
          .map((pair) => {
            const ticker = tickerBySymbol.get(pair.symbol);
            const lastPrice = Number(ticker?.lastPrice);
            const openPrice = Number(ticker?.open);
            const quoteVolume = Number(ticker?.quoteVol);
            const hasPrice = Number.isFinite(lastPrice) && lastPrice > 0;
            const hasChange = hasPrice && Number.isFinite(openPrice) && openPrice > 0;
            return {
              symbol: pair.symbol,
              base: pair.base,
              quote: pair.quote,
              price: hasPrice ? lastPrice : null,
              changePercent: hasChange ? ((lastPrice - openPrice) / openPrice) * 100 : null,
              quoteVolume: Number.isFinite(quoteVolume) ? quoteVolume : 0,
            };
          })
          .sort((left, right) => right.quoteVolume - left.quoteVolume);
        if (marketData.length === 0) {
          throw new Error("Bitunix did not return any open futures pairs.");
        }

        if (!disposed) {
          setMarkets(marketData);
          setUpdatedAt(new Date());
          setError(null);
        }
      } catch (marketError) {
        if (!disposed) {
          setError(
            marketError instanceof Error
              ? marketError.message
              : "Live market data is temporarily unavailable.",
          );
        }
      } finally {
        if (!disposed) {
          setRefreshing(false);
          timer = window.setTimeout(() => void refreshMarkets(), 60_000);
        }
      }
    }

    void refreshMarkets();
    return () => {
      disposed = true;
      if (timer !== undefined) window.clearTimeout(timer);
      activeRequest?.abort();
    };
  }, [refreshKey]);

  useEffect(() => {
    const track = marketTrackRef.current;
    if (!track) return;

    const updateDuration = () => {
      setMarqueeDuration(Math.max(track.scrollWidth / 70, 30));
    };
    updateDuration();
    const observer = new ResizeObserver(updateDuration);
    observer.observe(track);
    return () => observer.disconnect();
  }, [markets.length]);

  return (
    <section className="market-overview" aria-label="Live market overview">
      <div className="market-heading">
        <div>
          <span className="eyebrow">BITUNIX · PERPETUAL FUTURES</span>
          <h3>Futures markets <span className="market-count">{markets.length || "—"} pairs</span></h3>
        </div>
        <div className="market-updated">
          <span className={`market-live-dot ${refreshing ? "refreshing" : ""}`} />
          <span>
            {updatedAt
              ? `Updated ${updatedAt.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}`
              : refreshing
                ? "Loading live data"
                : "Waiting for data"}
          </span>
        </div>
      </div>
      {error && (
        <p className="market-error" role="status">
          Live futures data is temporarily unavailable. {error}
          {markets.length > 0 && " Showing the last successfully loaded prices."}
          <button type="button" onClick={() => setRefreshKey((key) => key + 1)}>
            Retry
          </button>
        </p>
      )}
      {markets.length > 0 ? (
        <div
          className="market-marquee"
          role="region"
          aria-label={`${markets.length} open Bitunix futures markets`}
          tabIndex={0}
        >
          <div
            className="market-marquee-track"
            style={{ animationDuration: `${marqueeDuration}s` }}
          >
            <div className="market-marquee-group" role="list" ref={marketTrackRef}>
              {markets.map((market) => (
                <MarketTickerItem
                  key={market.symbol}
                  market={market}
                  onClick={() => setPromptMarket(market)}
                />
              ))}
            </div>
            <div className="market-marquee-group" role="list" aria-hidden="true">
              {markets.map((market) => (
                <MarketTickerItem
                  key={`duplicate-${market.symbol}`}
                  market={market}
                  onClick={() => setPromptMarket(market)}
                  decorative
                />
              ))}
            </div>
          </div>
        </div>
      ) : (
        !error && (
          <div className="market-loading" aria-live="polite">
            <span />
            <span />
            <span />
            <span>Loading Bitunix futures pairs…</span>
          </div>
        )
      )}
      <p className="market-disclaimer">
        Public Bitunix futures data · 24-hour change calculated from daily open · Not a trading signal
      </p>
      {promptMarket && createPortal(
        <div
          className="market-dialog-backdrop"
          onMouseDown={(event) => {
            if (event.target === event.currentTarget) setPromptMarket(null);
          }}
        >
          <section
            className="market-create-dialog"
            role="dialog"
            aria-modal="true"
            aria-labelledby="market-create-title"
            aria-describedby="market-create-description"
          >
            <button
              type="button"
              className="icon-button market-dialog-close"
              onClick={() => setPromptMarket(null)}
              aria-label="Close dialog"
            >
              <X size={18} />
            </button>
            <span className="market-dialog-icon">
              <Plus size={19} />
            </span>
            <span className="eyebrow">BITUNIX FUTURES</span>
            <h3 id="market-create-title">
              {existingStrategy ? "Strategy already exists" : "Create a strategy?"}
            </h3>
            <p id="market-create-description">
              {existingStrategy
                ? `You already have a strategy for ${promptMarket.symbol}. Would you like to open it?`
                : `Start configuring a new strategy for ${promptMarket.symbol}?`}
            </p>
            <div className="market-dialog-actions">
              <button
                type="button"
                className="secondary-button"
                autoFocus
                onClick={() => setPromptMarket(null)}
              >
                Cancel
              </button>
              <button
                type="button"
                className="primary-button"
                onClick={() => {
                  onCreateStrategy(promptMarket.symbol);
                  setPromptMarket(null);
                }}
              >
                {existingStrategy ? "Open strategy" : "Create strategy"}
                <ArrowRight size={15} />
              </button>
            </div>
          </section>
        </div>,
        document.body,
      )}
    </section>
  );
}

function MarketTickerItem({
  market,
  onClick,
  decorative = false,
}: {
  market: FuturesMarket;
  onClick: () => void;
  decorative?: boolean;
}) {
  const [imageFailed, setImageFailed] = useState(false);
  const positive = (market.changePercent ?? 0) >= 0;
  const iconUrl = `https://assets.coincap.io/assets/icons/${encodeURIComponent(market.base.toLowerCase())}@2x.png`;

  return (
    <button
      type="button"
      className="market-ticker-item"
      role="listitem"
      aria-label={`Choose ${market.symbol} futures pair`}
      aria-hidden={decorative || undefined}
      tabIndex={decorative ? -1 : 0}
      title={`${market.symbol} · Bitunix futures`}
      onClick={onClick}
    >
      <span className="market-coin-icon">
        {!imageFailed ? (
          <img
            src={iconUrl}
            alt=""
            loading="lazy"
            decoding="async"
            onError={() => setImageFailed(true)}
          />
        ) : (
          <span>{market.base.slice(0, 1)}</span>
        )}
      </span>
      <span className="market-ticker-symbol">{market.base}<small>/{market.quote}</small></span>
      <span className="market-ticker-price">
        {market.price === null
          ? "—"
          : market.price.toLocaleString(undefined, { maximumSignificantDigits: 7 })}
      </span>
      <span className={`market-ticker-change ${positive ? "positive" : "negative"}`}>
        {market.changePercent === null
          ? "—"
          : `${positive ? "+" : ""}${market.changePercent.toFixed(2)}%`}
      </span>
    </button>
  );
}

function Metric({
  icon,
  label,
  value,
  detail,
  tone,
}: {
  icon: React.ReactNode;
  label: string;
  value: string;
  detail: string;
  tone: string;
}) {
  return (
    <div className="metric-card">
      <span className={`metric-icon ${tone}`}>{icon}</span>
      <span className="metric-label">{label}</span>
      <strong>{value}</strong>
      <small>{detail}</small>
    </div>
  );
}
function EmptyState({ onCreate }: { onCreate: () => void }) {
  return (
    <div className="empty-state">
      <div className="empty-icon">
        <Sparkles size={22} />
      </div>
      <h3>Your desk is clear.</h3>
      <p>Start with one strategy and tune it as the market teaches you.</p>
      <button className="secondary-button" onClick={onCreate}>
        Configure first strategy <ArrowRight size={16} />
      </button>
    </div>
  );
}

function PageHeader({
  eyebrow,
  title,
  description,
  action,
}: {
  eyebrow: string;
  title: string;
  description: string;
  action?: React.ReactNode;
}) {
  return (
    <header className="page-header">
      <div>
        <span className="eyebrow">{eyebrow}</span>
        <h1>{title}</h1>
        <p>{description}</p>
      </div>
      {action}
    </header>
  );
}

function BotsView({
  bots,
  selectedBot,
  initialSymbol,
  hasBotsError,
  onSelect,
  onSave,
  onDelete,
  onCancel,
}: {
  bots: BotConfig[];
  selectedBot: BotConfig | null;
  initialSymbol: string | null;
  hasBotsError: boolean;
  onSelect: (bot: BotConfig | null) => void;
  onSave: (symbol: string, draft: BotDraft) => Promise<void>;
  onDelete: (symbol: string) => Promise<void>;
  onCancel: () => void;
}) {
  const [newBot, setNewBot] = useState(Boolean(initialSymbol));
  const [draftSymbol, setDraftSymbol] = useState(initialSymbol ?? "");

  useEffect(() => {
    if (initialSymbol) {
      setDraftSymbol(initialSymbol);
      setNewBot(true);
    }
  }, [initialSymbol]);

  return (
    <>
      <PageHeader
        eyebrow="STRATEGIES"
        title="Your strategies."
        description="Tune one market at a time, with clear intent."
        action={
          <button
            className="primary-button"
            onClick={() => {
              setDraftSymbol("");
              setNewBot(true);
              onSelect(null);
            }}
          >
            <Plus size={17} />
            New strategy
          </button>
        }
      />
      <div className="bot-layout strategy-dashboard-layout">
        <div className="bot-list strategy-dashboard-list">
          {bots.length ? (
            <div className="strategy-table-scroll">
              <table className="strategy-table">
                <thead>
                  <tr>
                    <th scope="col">Market</th>
                    <th scope="col">State</th>
                    <th scope="col">Signal settings</th>
                    <th scope="col">Side / frame</th>
                    <th scope="col">Last update</th>
                    <th scope="col"><span className="sr-only">Edit</span></th>
                  </tr>
                </thead>
                <tbody>
                  {bots.map((bot) => (
                    <tr
                      key={bot.id}
                      className={selectedBot?.id === bot.id ? "selected" : ""}
                      tabIndex={0}
                      aria-label={`Edit ${bot.symbol} strategy`}
                      onClick={() => {
                        setNewBot(false);
                        setDraftSymbol("");
                        onSelect(bot);
                      }}
                      onKeyDown={(event) => {
                        if (event.key === "Enter" || event.key === " ") {
                          event.preventDefault();
                          setNewBot(false);
                          setDraftSymbol("");
                          onSelect(bot);
                        }
                      }}
                    >
                      <td className="strategy-market-cell">
                        <span className="strategy-market-icon">{bot.symbol.slice(0, 1)}</span>
                        <span>
                          <strong>{bot.symbol}</strong>
                          <small>BITUNIX FUTURES</small>
                        </span>
                      </td>
                      <td>
                        <span className={`status-pill ${bot.active ? "on" : "off"}`}>
                          <i />
                          {bot.active ? "Active" : "Paused"}
                        </span>
                      </td>
                      <td>
                        <div className="strategy-signal-list">
                          {formatStrategySignals(bot).map((signal, index) => (
                            <span className="strategy-signal-chip" key={`${index}-${signal}`}>{signal}</span>
                          ))}
                          {!formatStrategySignals(bot).length && (
                            <span className="strategy-no-signals">No signals</span>
                          )}
                        </div>
                      </td>
                      <td className="strategy-side-cell">
                        <strong>{bot.parameters?.trading_side ?? "BOTH"}</strong>
                        <small>{bot.parameters?.timeframe ?? "1h"}</small>
                      </td>
                      <td className="strategy-updated-cell" title={bot.updatedAt || undefined}>
                        {formatStrategyUpdatedAt(bot.updatedAt)}
                      </td>
                      <td className="strategy-edit-cell"><ChevronRight size={15} /></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          ) : hasBotsError ? (
            <div className="list-empty strategy-load-placeholder">
              Strategies are hidden until the connection is restored.
            </div>
          ) : (
            <div className="strategy-empty">
              <span className="empty-icon"><Bot size={19} /></span>
              <div>
                <strong>No strategies yet</strong>
                <p>Create a strategy to start monitoring a futures market.</p>
              </div>
              <button
                type="button"
                className="secondary-button"
                onClick={() => {
                  setDraftSymbol("");
                  setNewBot(true);
                  onSelect(null);
                }}
              >
                <Plus size={15} /> Create strategy
              </button>
            </div>
          )}
        </div>
        {selectedBot || newBot ? (
          <BotEditor
            bot={selectedBot}
            initialSymbol={draftSymbol}
            onSave={onSave}
            onDelete={onDelete}
            onCancel={() => {
              setNewBot(false);
              setDraftSymbol("");
              onCancel();
            }}
          />
        ) : (
          <div className="editor-placeholder">
            <SlidersHorizontal size={24} />
            <h3>Select a strategy</h3>
            <p>Choose a market from the list or create a new configuration.</p>
          </div>
        )}
      </div>
    </>
  );
}

function formatStrategySignals(bot: BotConfig): string[] {
  const parameters = bot.parameters;
  const signals: string[] = [];

  for (const indicator of parameters?.rsi_indicators ?? []) {
    signals.push(`RSI ${indicator.period} · ${indicator.overbought}/${indicator.oversold}`);
  }
  for (const indicator of parameters?.hammer_indicators ?? []) {
    signals.push(`HAM ${indicator.timeframe} · ×${indicator.multiplier}`);
  }
  for (const indicator of parameters?.volume_indicators ?? []) {
    signals.push(`VOL ${indicator.timeframe} · ×${indicator.multiplier}`);
  }

  if (!signals.length && parameters && !Array.isArray(parameters.rsi_indicators)) {
    signals.push(`RSI ${parameters.rsi_period} · ${parameters.rsi_overbought}/${parameters.rsi_oversold}`);
  }
  return signals;
}

function formatStrategyUpdatedAt(value?: string): string {
  if (!value) return "Unknown";
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return "Unknown";
  return new Intl.DateTimeFormat(undefined, {
    month: "short",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
  }).format(date);
}

type MarketCandle = {
  open: number;
  high: number;
  low: number;
  close: number;
  quoteVolume: number;
  time: number;
};

type MarketCandleResponse = {
  code: number;
  data: Array<{
    open: string;
    high: string;
    low: string;
    close: string;
    quoteVol: string;
    time: string;
  }> | null;
};

const MARKET_CHART_RANGES = [
  { id: "1D", label: "1D", interval: "15m", intervalMs: 15 * 60_000, durationMs: 24 * 60 * 60_000 },
  { id: "5D", label: "5D", interval: "1h", intervalMs: 60 * 60_000, durationMs: 5 * 24 * 60 * 60_000 },
  { id: "1M", label: "1M", interval: "4h", intervalMs: 4 * 60 * 60_000, durationMs: 30 * 24 * 60 * 60_000 },
  { id: "3M", label: "3M", interval: "12h", intervalMs: 12 * 60 * 60_000, durationMs: 90 * 24 * 60 * 60_000 },
  { id: "6M", label: "6M", interval: "1d", intervalMs: 24 * 60 * 60_000, durationMs: 180 * 24 * 60 * 60_000 },
  { id: "YTD", label: "YTD", interval: "1d", intervalMs: 24 * 60 * 60_000, durationMs: null },
  { id: "1Y", label: "1Y", interval: "1d", intervalMs: 24 * 60 * 60_000, durationMs: 365 * 24 * 60 * 60_000 },
  { id: "5Y", label: "5Y", interval: "1w", intervalMs: 7 * 24 * 60 * 60_000, durationMs: 5 * 365 * 24 * 60 * 60_000 },
  { id: "ALL", label: "All", interval: "1w", intervalMs: 7 * 24 * 60 * 60_000, durationMs: null },
] as const;

type MarketChartRangeId = (typeof MARKET_CHART_RANGES)[number]["id"];

function MarketChart({ symbol }: { symbol: string }) {
  const [candles, setCandles] = useState<MarketCandle[]>([]);
  const [rangeId, setRangeId] = useState<MarketChartRangeId>("1M");
  const [viewStart, setViewStart] = useState(0);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [refreshKey, setRefreshKey] = useState(0);
  const [expanded, setExpanded] = useState(true);
  const [dragging, setDragging] = useState(false);
  const loadedHistoryKey = useRef("");
  const dragState = useRef<{ pointerId: number; startX: number; startIndex: number } | null>(null);
  const normalizedSymbol = symbol.trim().toUpperCase();
  const validSymbol = /^[A-Z0-9]{2,30}$/.test(normalizedSymbol);
  const selectedRange = MARKET_CHART_RANGES.find((range) => range.id === rangeId) ?? MARKET_CHART_RANGES[2];
  const interval = selectedRange.interval;
  const durationMs = selectedRange.id === "YTD"
    ? Math.ceil((Date.now() - new Date(new Date().getFullYear(), 0, 1).getTime()) / selectedRange.intervalMs)
      * selectedRange.intervalMs
    : selectedRange.durationMs;
  const visibleCandleCount = durationMs === null
    ? 1000
    : Math.max(2, Math.ceil(durationMs / selectedRange.intervalMs));
  const targetCandleCount = durationMs === null
    ? 1000
    : Math.min(1000, visibleCandleCount + 80);
  const visibleCandles = candles.slice(viewStart, viewStart + visibleCandleCount);
  const maxViewStart = Math.max(0, candles.length - visibleCandles.length);

  useEffect(() => {
    if (!expanded) {
      return;
    }

    if (!validSymbol) {
      setCandles([]);
      setError(null);
      setLoading(false);
      loadedHistoryKey.current = "";
      return;
    }

    let disposed = false;
    let timer: number | undefined;
    const request = new AbortController();
    const historyKey = `${normalizedSymbol}:${rangeId}`;
    if (loadedHistoryKey.current !== historyKey) {
      setCandles([]);
      setError(null);
      setViewStart(0);
    }
    const refreshWhenVisible = () => {
      if (document.visibilityState === "visible") {
        if (timer !== undefined) window.clearTimeout(timer);
        setRefreshKey((key) => key + 1);
      }
    };
    document.addEventListener("visibilitychange", refreshWhenVisible);

    async function loadCandles() {
      setLoading(true);
      try {
        const historyLoaded = loadedHistoryKey.current === historyKey;
        const pageCandles: MarketCandle[] = [];
        let pageEndTime = Date.now();
        const rangeStartTime = rangeId === "YTD"
          ? new Date(new Date().getFullYear(), 0, 1).getTime()
          : durationMs === null ? 0 : pageEndTime - durationMs;
        const historyStartTime = Math.max(0, rangeStartTime - 80 * selectedRange.intervalMs);

        do {
          const query = new URLSearchParams({
            symbol: normalizedSymbol,
            interval,
            limit: historyLoaded ? "48" : "200",
          });
          if (!historyLoaded) {
            query.set("startTime", String(historyStartTime));
            query.set("endTime", String(pageEndTime));
          }
          const response = await fetch(`/market-data/kline?${query}`, {
            cache: "no-store",
            signal: request.signal,
          });
          if (!response.ok) {
            throw new Error(`Bitunix returned HTTP ${response.status}`);
          }

          const result = (await response.json()) as MarketCandleResponse;
          if (result.code !== 0 || !Array.isArray(result.data)) {
            throw new Error("Bitunix did not return candle data for this pair.");
          }

          const nextPage = result.data
            .map((candle) => ({
              open: Number(candle.open),
              high: Number(candle.high),
              low: Number(candle.low),
              close: Number(candle.close),
              quoteVolume: Number(candle.quoteVol),
              time: Number(candle.time),
            }))
            .filter((candle) =>
              [candle.open, candle.high, candle.low, candle.close, candle.quoteVolume, candle.time].every(Number.isFinite)
              && candle.open > 0
              && candle.high > 0
              && candle.low > 0
              && candle.close > 0
              && candle.time > 0,
            );
          pageCandles.push(...nextPage);
          if (historyLoaded || nextPage.length < 200) {
            break;
          }
          const oldestTime = Math.min(...nextPage.map((candle) => candle.time));
          if (oldestTime <= historyStartTime || pageCandles.length >= targetCandleCount) {
            break;
          }
          pageEndTime = oldestTime - 1;
        } while (pageCandles.length < targetCandleCount);

        const nextCandles = [...new Map(pageCandles.map((candle) => [candle.time, candle])).values()]
          .sort((left, right) => left.time - right.time);
        if (nextCandles.length < 2) {
          throw new Error("Not enough valid candles are available to draw this chart.");
        }
        if (!disposed) {
          if (!historyLoaded) {
            setCandles(nextCandles);
            loadedHistoryKey.current = historyKey;
            setViewStart(Math.max(0, nextCandles.length - visibleCandleCount));
          } else {
            setCandles((previousCandles) => {
              const candlesByTime = new Map(
                previousCandles.map((candle) => [candle.time, candle]),
              );
              nextCandles.forEach((candle) => candlesByTime.set(candle.time, candle));
              return [...candlesByTime.values()]
                .sort((left, right) => left.time - right.time)
                .slice(-1000);
            });
          }
          setError(null);
        }
      } catch (requestError) {
        if (!disposed && !request.signal.aborted) {
          setError(
            requestError instanceof Error
              ? requestError.message
              : "Could not load market candles.",
          );
        }
      } finally {
        if (!disposed) {
          setLoading(false);
          timer = window.setTimeout(() => {
            if (document.visibilityState === "visible") {
              setRefreshKey((key) => key + 1);
            }
          }, 10_000);
        }
      }
    }

    void loadCandles();
    return () => {
      disposed = true;
      request.abort();
      document.removeEventListener("visibilitychange", refreshWhenVisible);
      if (timer !== undefined) window.clearTimeout(timer);
    };
  }, [
    normalizedSymbol,
    interval,
    rangeId,
    refreshKey,
    validSymbol,
    expanded,
    durationMs,
    selectedRange.intervalMs,
    targetCandleCount,
    visibleCandleCount,
  ]);

  const latest = candles.at(-1);
  const first = visibleCandles[0];
  const visibleLatest = visibleCandles.at(-1);
  const isAtPresent = viewStart >= maxViewStart;
  const changePercent = visibleLatest && first ? ((visibleLatest.close - first.close) / first.close) * 100 : null;
  const displayedPrice = isAtPresent ? latest?.close : visibleLatest?.close;
  const priceLabel = displayedPrice;
  const formattedPriceLabel = priceLabel !== undefined && priceLabel !== null
    ? priceLabel.toLocaleString(undefined, { maximumSignificantDigits: 8 })
    : "—";

  const handleChartPointerDown = (event: ReactPointerEvent<SVGSVGElement>) => {
    if (!visibleCandles.length || event.button !== 0) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    dragState.current = { pointerId: event.pointerId, startX: event.clientX, startIndex: viewStart };
    setDragging(true);
  };
  const handleChartPointerMove = (event: ReactPointerEvent<SVGSVGElement>) => {
    const drag = dragState.current;
    if (!drag || drag.pointerId !== event.pointerId) return;
    const chartWidth = event.currentTarget.getBoundingClientRect().width;
    const candlesPerPixel = (visibleCandles.length * 760 / 684) / Math.max(chartWidth, 1);
    const shift = Math.round((drag.startX - event.clientX) * candlesPerPixel);
    setViewStart(Math.min(maxViewStart, Math.max(0, drag.startIndex + shift)));
  };
  const handleChartPointerEnd = (event: ReactPointerEvent<SVGSVGElement>) => {
    if (dragState.current?.pointerId !== event.pointerId) return;
    dragState.current = null;
    setDragging(false);
    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId);
    }
  };

  return (
    <section className="market-chart" aria-label={`${normalizedSymbol || "Market"} price chart`}>
      <header className="market-chart-header">
        <div>
          <span className="eyebrow">MARKET PREVIEW · BITUNIX FUTURES</span>
          <div className="market-chart-title">
            <strong>{normalizedSymbol || "Select a market"}</strong>
            <span>{formattedPriceLabel}</span>
            {changePercent !== null && (
              <small className={changePercent >= 0 ? "positive" : "negative"}>
                {changePercent >= 0 ? "+" : ""}{changePercent.toFixed(2)}%
              </small>
            )}
          </div>
        </div>
        <div className="market-chart-controls">
          {expanded && (
            <span className={`market-chart-status ${loading ? "loading" : ""}`}>
              {loading ? "Updating" : candles.length ? `${interval} · LIVE` : ""}
            </span>
          )}
          <button
            type="button"
            className="market-chart-toggle"
            aria-expanded={expanded}
            aria-controls="market-chart-content"
            onClick={() => setExpanded((isExpanded) => !isExpanded)}
          >
            {expanded ? "Hide chart" : "Show chart"}
            <span aria-hidden="true">{expanded ? "−" : "+"}</span>
          </button>
        </div>
      </header>
      {expanded && (
        <div id="market-chart-content">
          {!validSymbol ? (
            <div className="market-chart-message">Enter a futures symbol to preview its price chart.</div>
          ) : error && candles.length === 0 ? (
            <div className="market-chart-message error" role="alert">
              <span>{error}</span>
              <button type="button" onClick={() => setRefreshKey((key) => key + 1)}>Retry</button>
            </div>
          ) : candles.length ? (
            <>
              {error && <p className="market-chart-stale" role="status">Refresh failed. Showing the last loaded candles.</p>}
              <CandlestickChart
                candles={visibleCandles}
                symbol={normalizedSymbol}
                interval={interval}
                rangeId={rangeId}
                currentPrice={isAtPresent ? latest?.close ?? null : null}
                dragging={dragging}
                onPointerDown={handleChartPointerDown}
                onPointerMove={handleChartPointerMove}
                onPointerUp={handleChartPointerEnd}
                onPointerCancel={handleChartPointerEnd}
              />
              <div className="market-chart-range-bar" aria-label="Chart date range">
                {MARKET_CHART_RANGES.map((range) => (
                  <button
                    type="button"
                    key={range.id}
                    className={rangeId === range.id ? "active" : ""}
                    aria-pressed={rangeId === range.id}
                    onClick={() => setRangeId(range.id)}
                  >
                    {range.label}
                  </button>
                ))}
                <span className="market-chart-range-interval">{interval} candles</span>
              </div>
              <div className="market-chart-footer">
                <span>{visibleCandles.length} candles · drag chart to pan</span>
                <span>Public market data · visual reference only</span>
              </div>
            </>
          ) : (
            <div className="market-chart-skeleton" aria-label="Loading market chart" aria-busy="true">
              <span />
              <span />
              <span />
            </div>
          )}
        </div>
      )}
    </section>
  );
}

function CandlestickChart({
  candles,
  symbol,
  interval,
  rangeId,
  currentPrice,
  dragging,
  onPointerDown,
  onPointerMove,
  onPointerUp,
  onPointerCancel,
}: {
  candles: MarketCandle[];
  symbol: string;
  interval: string;
  rangeId: MarketChartRangeId;
  currentPrice: number | null;
  dragging: boolean;
  onPointerDown: (event: ReactPointerEvent<SVGSVGElement>) => void;
  onPointerMove: (event: ReactPointerEvent<SVGSVGElement>) => void;
  onPointerUp: (event: ReactPointerEvent<SVGSVGElement>) => void;
  onPointerCancel: (event: ReactPointerEvent<SVGSVGElement>) => void;
}) {
  const width = 760;
  const height = 390;
  const left = 4;
  const right = 688;
  const axisDivider = 692;
  const axisLabelX = 700;
  const top = 8;
  const priceBottom = 297;
  const volumeBottom = 350;
  const dataMinPrice = Math.min(...candles.map((candle) => candle.low), ...(currentPrice ? [currentPrice] : []));
  const dataMaxPrice = Math.max(...candles.map((candle) => candle.high), ...(currentPrice ? [currentPrice] : []));
  const dataPriceRange = dataMaxPrice - dataMinPrice || dataMaxPrice * 0.001 || 1;
  const minPrice = Math.max(0, dataMinPrice - dataPriceRange * 0.02);
  const maxPrice = dataMaxPrice + dataPriceRange * 0.02;
  const priceRange = maxPrice - minPrice || 1;
  const maxVolume = Math.max(0, ...candles.map((candle) => candle.quoteVolume));
  const slot = (right - left) / candles.length;
  const bodyWidth = Math.max(2, Math.min(9, slot * 0.58));
  const yForPrice = (price: number) => top + ((maxPrice - price) / priceRange) * (priceBottom - top);
  const rawTickStep = priceRange / 6;
  const tickMagnitude = 10 ** Math.floor(Math.log10(rawTickStep));
  const normalizedTickStep = rawTickStep / tickMagnitude;
  const tickFactor = normalizedTickStep <= 1 ? 1
    : normalizedTickStep <= 2 ? 2
    : normalizedTickStep <= 2.5 ? 2.5
    : normalizedTickStep <= 5 ? 5
    : 10;
  const priceTickStep = tickFactor * tickMagnitude;
  const pricePrecision = Math.max(0, Math.min(8, -Math.floor(Math.log10(priceTickStep)) + 1));
  const priceTicks: number[] = [];
  for (let price = Math.ceil(minPrice / priceTickStep) * priceTickStep; price <= maxPrice; price += priceTickStep) {
    priceTicks.push(price);
  }
  const latestCandle = candles[candles.length - 1];
  const latestPrice = currentPrice ?? latestCandle.close;
  const latestPriceY = yForPrice(latestPrice);
  const latestPriceClass = latestPrice >= latestCandle.open ? "up" : "down";
  const formatPrice = (price: number) => price.toLocaleString(undefined, {
    minimumFractionDigits: 0,
    maximumFractionDigits: pricePrecision,
  });
  const firstTime = candles[0].time;
  const lastTime = candles[candles.length - 1].time;
  const timeRange = Math.max(1, lastTime - firstTime);
  const monthRange = timeRange > 35 * 24 * 60 * 60_000;
  const monthStep = timeRange > 3 * 365 * 24 * 60 * 60_000 ? 3 : timeRange > 365 * 24 * 60 * 60_000 ? 2 : 1;
  const timeTicks: Array<{ time: number; label: string }> = [];
  if (monthRange) {
    const firstDate = new Date(firstTime);
    const firstMonth = new Date(firstDate.getFullYear(), firstDate.getMonth(), 1);
    let tickDate = firstMonth.getTime() < firstTime
      ? new Date(firstDate.getFullYear(), firstDate.getMonth() + monthStep, 1)
      : firstMonth;
    if (tickDate.getTime() - firstTime > timeRange / 4) {
      timeTicks.push({
        time: firstTime,
        label: firstDate.toLocaleDateString([], { month: "short" }),
      });
    }
    while (tickDate.getTime() <= lastTime) {
      timeTicks.push({
        time: tickDate.getTime(),
        label: tickDate.getMonth() === 0
          ? tickDate.toLocaleDateString([], { month: "short", year: "2-digit" })
          : tickDate.toLocaleDateString([], { month: "short" }),
      });
      tickDate = new Date(tickDate.getFullYear(), tickDate.getMonth() + monthStep, 1);
    }
  } else {
    const tickCount = 5;
    for (let index = 0; index <= tickCount; index += 1) {
      const time = firstTime + (timeRange * index) / tickCount;
      const date = new Date(time);
      const label = rangeId === "1D"
        ? date.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })
        : date.toLocaleDateString([], { month: "short", day: "numeric" });
      timeTicks.push({ time, label });
    }
  }
  const xForTime = (time: number) => left + ((time - firstTime) / timeRange) * (right - left);

  return (
    <svg
      className={`market-candlestick-svg ${dragging ? "dragging" : ""}`}
      viewBox={`0 0 ${width} ${height}`}
      role="img"
      aria-label={`${symbol} ${interval} candlestick chart, ${candles.length} candles`}
      preserveAspectRatio="none"
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerCancel}
    >
      {priceTicks.map((price) => {
        const y = yForPrice(price);
        return (
          <g key={price}>
            <line className="chart-grid-line" x1={left} x2={right} y1={y} y2={y} />
            <text className="chart-axis-label chart-price-label" x={axisLabelX} y={y + 3} textAnchor="start">
              {formatPrice(price)}
            </text>
          </g>
        );
      })}
      <line className="chart-price-axis-divider" x1={axisDivider} x2={axisDivider} y1={top} y2={priceBottom} />
      {timeTicks.map((tick) => {
        const x = xForTime(tick.time);
        return (
          <g key={`${tick.time}-${tick.label}`}>
            <line className="chart-time-grid-line" x1={x} x2={x} y1={top} y2={priceBottom} />
            <line className="chart-time-tick" x1={x} x2={x} y1={356} y2={362} />
            <text className="chart-axis-label chart-time-label" x={x} y={382} textAnchor="middle">
              {tick.label}
            </text>
          </g>
        );
      })}
      {candles.map((candle, index) => {
        const x = left + (index + 0.5) * slot;
        const openY = yForPrice(candle.open);
        const closeY = yForPrice(candle.close);
        const rising = candle.close >= candle.open;
        const bodyY = Math.min(openY, closeY);
        const bodyHeight = Math.max(1.5, Math.abs(openY - closeY));
        const volumeHeight = maxVolume > 0 ? (candle.quoteVolume / maxVolume) * 58 : 0;
        const color = rising ? "up" : "down";
        return (
          <g className={`chart-candle ${color}`} key={candle.time}>
            <line x1={x} x2={x} y1={yForPrice(candle.high)} y2={yForPrice(candle.low)} />
            <rect
              x={x - bodyWidth / 2}
              y={bodyY}
              width={bodyWidth}
              height={bodyHeight}
              rx=".6"
            />
            {volumeHeight > 0 && (
              <rect
                className="chart-volume"
                x={x - bodyWidth / 2}
                y={volumeBottom - volumeHeight}
                width={bodyWidth}
                height={volumeHeight}
                rx=".6"
              />
            )}
          </g>
        );
      })}
      <line className="chart-volume-divider" x1={left} x2={right} y1={volumeBottom - 60} y2={volumeBottom - 60} />
      <line className="chart-time-axis-divider" x1={left} x2={right} y1={362} y2={362} />
      <line className={`chart-current-price-line ${latestPriceClass}`} x1={left} x2={axisDivider} y1={latestPriceY} y2={latestPriceY} />
      <rect
        className={`chart-current-price-tag ${latestPriceClass}`}
        x={axisDivider + 3}
        y={latestPriceY - 11}
        width={width - axisDivider - 7}
        height={22}
        rx="3"
      />
      <text className="chart-current-price-text" x={axisDivider + 7} y={latestPriceY + 4}>
        {formatPrice(latestPrice)}
      </text>
    </svg>
  );
}

function BotEditor({
  bot,
  initialSymbol,
  onSave,
  onDelete,
  onCancel,
}: {
  bot: BotConfig | null;
  initialSymbol: string;
  onSave: (symbol: string, draft: BotDraft) => Promise<void>;
  onDelete: (symbol: string) => Promise<void>;
  onCancel: () => void;
}) {
  const normalizeParameters = (parameters: BotDraft["parameters"]) => ({
    ...emptyDraft.parameters,
    ...parameters,
    rsi_indicators: parameters.rsi_indicators?.slice(0, 3) ?? emptyDraft.parameters.rsi_indicators,
    hammer_indicators: parameters.hammer_indicators?.slice(0, 3) ?? emptyDraft.parameters.hammer_indicators,
    volume_indicators: parameters.volume_indicators?.slice(0, 3) ?? emptyDraft.parameters.volume_indicators,
  });
  const [symbol, setSymbol] = useState(bot?.symbol ?? initialSymbol);
  const [draft, setDraft] = useState<BotDraft>(
    bot
      ? {
          active: bot.active,
          price_step_percentage: bot.price_step_percentage ?? 0,
          take_profit_percentage: bot.take_profit_percentage ?? 0,
          trade_quantity: bot.trade_quantity ?? 0.001,
          parameters: {
            ...normalizeParameters(bot.parameters),
            take_profit_scope: bot.parameters.take_profit_scope ?? "CYCLE",
          },
        }
      : emptyDraft,
  );
  const [saving, setSaving] = useState(false);
  const [deleting, setDeleting] = useState(false);
  useEffect(() => {
    const nextBot = bot
      ? {
          active: bot.active,
          price_step_percentage: bot.price_step_percentage ?? 0,
          take_profit_percentage: bot.take_profit_percentage ?? 0,
          trade_quantity: bot.trade_quantity ?? 0.001,
          parameters: {
            ...normalizeParameters(bot.parameters),
            take_profit_scope: bot.parameters.take_profit_scope ?? "CYCLE",
          },
        }
      : emptyDraft;
    setSymbol(bot?.symbol ?? initialSymbol);
    setDraft(nextBot);
  }, [bot, initialSymbol]);
  function updateParam<K extends keyof BotDraft["parameters"]>(
    key: K,
    value: BotDraft["parameters"][K],
  ) {
    setDraft((current) => ({
      ...current,
      parameters: { ...current.parameters, [key]: value },
    }));
  }
  function updateIndicator<K extends "rsi_indicators" | "hammer_indicators" | "volume_indicators">(
    key: K,
    index: number,
    value: BotDraft["parameters"][K][number],
  ) {
    setDraft((current) => ({
      ...current,
      parameters: {
        ...current.parameters,
        [key]: current.parameters[key].map((item, itemIndex) =>
          itemIndex === index ? value : item,
        ),
      },
    }));
  }
  function addIndicator(key: "rsi_indicators" | "hammer_indicators" | "volume_indicators") {
    setDraft((current) => {
      if (current.parameters[key].length >= 3) return current;
      const value =
        key === "rsi_indicators"
          ? { period: 14, overbought: 70, oversold: 30, timeframe: "1h" }
          : { timeframe: "1h", multiplier: 1.5 };
      return { ...current, parameters: { ...current.parameters, [key]: [...current.parameters[key], value] } };
    });
  }
  function removeIndicator(key: "rsi_indicators" | "hammer_indicators" | "volume_indicators", index: number) {
    setDraft((current) => ({
      ...current,
      parameters: { ...current.parameters, [key]: current.parameters[key].filter((_, itemIndex) => itemIndex !== index) },
    }));
  }
  async function submit(event: FormEvent) {
    event.preventDefault();
    if (saving) return;
    setSaving(true);
    try {
      await onSave(symbol, draft);
    } finally {
      setSaving(false);
    }
  }
  async function deleteStrategy() {
    if (!bot || deleting) return;
    setDeleting(true);
    try {
      await onDelete(bot.symbol);
    } finally {
      setDeleting(false);
    }
  }
  return (
    <form className="editor-panel" onSubmit={submit}>
      <div className="editor-title">
        <div>
          <span className="eyebrow">
            {bot ? "EDIT STRATEGY" : "NEW STRATEGY"}
          </span>
          <h2>{bot?.symbol ?? "Configure market"}</h2>
        </div>
        <button
          type="button"
          className="icon-button"
          onClick={onCancel}
          aria-label="Close editor"
        >
          <X size={18} />
        </button>
      </div>
      <label className="field full">
        Market symbol
        <input
          required
          value={symbol}
          onChange={(event) => setSymbol(event.target.value)}
          placeholder="BTCUSDT"
          disabled={Boolean(bot) || saving}
        />
      </label>
      <MarketChart symbol={symbol} />
      <div className="field-grid">
        <label className="field">
          Trading side
          <select
            value={draft.parameters.trading_side}
            onChange={(event) =>
              updateParam(
                "trading_side",
                event.target.value as BotDraft["parameters"]["trading_side"],
              )
            }
            disabled={saving}
          >
            <option value="BOTH">Long + Short</option>
            <option value="LONG">Long only</option>
            <option value="SHORT">Short only</option>
          </select>
        </label>
        <label className="field">
          Take profit %
          <input
            type="number"
            min={0.1}
            max={99.9}
            step={0.1}
            value={draft.take_profit_percentage}
            onChange={(event) =>
              setDraft((current) => ({
                ...current,
                take_profit_percentage: Number(event.target.value),
              }))
            }
            disabled={saving}
          />
        </label>
        <label className="field">
          Apply take profit to
          <select
            value={draft.parameters.take_profit_scope}
            onChange={(event) =>
              updateParam(
                "take_profit_scope",
                event.target.value as BotDraft["parameters"]["take_profit_scope"],
              )
            }
            disabled={saving}
          >
            <option value="CYCLE">Whole cycle (average entry)</option>
            <option value="TRADE">Each separate trade</option>
          </select>
          <small>Take-profit exits use limit orders, which may not fill immediately.</small>
        </label>
        <label className="field">
          Order quantity
          <input
            type="number"
            min={0.000001}
            max={100}
            step={0.000001}
            value={draft.trade_quantity}
            onChange={(event) =>
              setDraft((current) => ({
                ...current,
                trade_quantity: Number(event.target.value),
              }))
            }
            disabled={saving}
          />
        </label>
      </div>
      <OrderMinimumPreview symbol={symbol} quantity={draft.trade_quantity} />
      <div className="field-grid">
        <label className="field">
          Step distance %
          <input
            type="number"
            min={0}
            max={100}
            step={0.1}
            value={draft.price_step_percentage}
            onChange={(event) =>
              setDraft((current) => ({
                ...current,
                price_step_percentage: Number(event.target.value),
              }))
            }
            disabled={saving}
          />
        </label>
      </div>
      <IndicatorEditor
        title="RSI indicators"
        items={draft.parameters.rsi_indicators}
        onAdd={() => addIndicator("rsi_indicators")}
        onRemove={(index) => removeIndicator("rsi_indicators", index)}
        render={(item, index) => (
          <div className="field-grid" key={`rsi-${index}`}>
            <label className="field">RSI period<input type="number" min={2} max={200} value={item.period} onChange={(e) => updateIndicator("rsi_indicators", index, { ...item, period: Number(e.target.value) })} /></label>
            <label className="field">Timeframe<select value={item.timeframe} onChange={(e) => updateIndicator("rsi_indicators", index, { ...item, timeframe: e.target.value })}><TimeframeOptions /></select></label>
            <label className="field">Overbought<input type="number" min={20} max={100} value={item.overbought} onChange={(e) => updateIndicator("rsi_indicators", index, { ...item, overbought: Number(e.target.value) })} /></label>
            <label className="field">Oversold<input type="number" min={0} max={80} value={item.oversold} onChange={(e) => updateIndicator("rsi_indicators", index, { ...item, oversold: Number(e.target.value) })} /></label>
          </div>
        )}
      />
      <IndicatorEditor
        title="Hammer indicators"
        items={draft.parameters.hammer_indicators}
        onAdd={() => addIndicator("hammer_indicators")}
        onRemove={(index) => removeIndicator("hammer_indicators", index)}
        render={(item, index) => <div className="field-grid" key={`hammer-${index}`}><label className="field">Timeframe<select value={item.timeframe} onChange={(e) => updateIndicator("hammer_indicators", index, { ...item, timeframe: e.target.value })}><TimeframeOptions /></select></label><label className="field">Multiplier<input type="number" min={0.1} max={10} step={0.1} value={item.multiplier} onChange={(e) => updateIndicator("hammer_indicators", index, { ...item, multiplier: Number(e.target.value) })} /></label></div>}
      />
      <IndicatorEditor
        title="Volume surge indicators"
        items={draft.parameters.volume_indicators}
        onAdd={() => addIndicator("volume_indicators")}
        onRemove={(index) => removeIndicator("volume_indicators", index)}
        render={(item, index) => <div className="field-grid" key={`volume-${index}`}><label className="field">Timeframe<select value={item.timeframe} onChange={(e) => updateIndicator("volume_indicators", index, { ...item, timeframe: e.target.value })}><TimeframeOptions /></select></label><label className="field">Multiplier<input type="number" min={0.1} max={10} step={0.1} value={item.multiplier} onChange={(e) => updateIndicator("volume_indicators", index, { ...item, multiplier: Number(e.target.value) })} /></label></div>}
      />
      <div className="toggle-row">
        <div>
          <strong>Run this strategy</strong>
          <small>Allow the worker to act on signals for this market.</small>
        </div>
        <button
          type="button"
          className={`toggle ${draft.active ? "on" : ""}`}
          onClick={() =>
            setDraft((current) => ({ ...current, active: !current.active }))
          }
          aria-label="Toggle strategy"
          disabled={saving}
        >
          <span />
        </button>
      </div>
      <div className="editor-actions">
        {bot && (
          <button
            type="button"
            className="danger-button"
            onClick={() => void deleteStrategy()}
            disabled={saving || deleting}
          >
            <Trash2 size={15} />
            {deleting ? "Deleting…" : "Delete strategy"}
          </button>
        )}
        <button
          type="button"
          className="secondary-button"
          onClick={onCancel}
          disabled={saving || deleting}
        >
          Cancel
        </button>
        <button type="submit" className="primary-button" disabled={saving || deleting}>
          {saving ? (
            <>
              <RefreshCw size={16} className="spin" />
              Saving...
            </>
          ) : (
            <>
              <Check size={16} />
              Save strategy
            </>
          )}
        </button>
      </div>
    </form>
  );
}

function TimeframeOptions() {
  return <><option>1m</option><option>5m</option><option>15m</option><option>1h</option><option>4h</option></>;
}

function OrderMinimumPreview({ symbol, quantity }: { symbol: string; quantity: number }) {
  const [market, setMarket] = useState<{ base: string; price: number; minimumQuantity: number } | null>(null);
  const [error, setError] = useState<string | null>(null);
  const normalizedSymbol = symbol.trim().toUpperCase();

  useEffect(() => {
    if (!/^[A-Z0-9]{2,30}$/.test(normalizedSymbol)) {
      setMarket(null);
      setError(null);
      return;
    }
    let disposed = false;
    let controller: AbortController | undefined;
    async function refresh() {
      controller?.abort();
      controller = new AbortController();
      try {
        const query = new URLSearchParams({ symbols: normalizedSymbol });
        const [tickerResponse, pairResponse] = await Promise.all([
          fetch(`/market-data/tickers?${query}`, { cache: "no-store", signal: controller.signal }),
          fetch(`/market-data/trading_pairs?${query}`, { cache: "no-store", signal: controller.signal }),
        ]);
        if (!tickerResponse.ok || !pairResponse.ok) {
          throw new Error("Bitunix market rules are temporarily unavailable.");
        }
        const [tickerResult, pairResult] = await Promise.all([
          tickerResponse.json() as Promise<BitunixResponse<{ symbol: string; lastPrice: string }>>,
          pairResponse.json() as Promise<BitunixResponse<{ symbol: string; base: string; minTradeVolume: string }>>,
        ]);
        const ticker = tickerResult.data.find((item) => item.symbol === normalizedSymbol);
        const pair = pairResult.data.find((item) => item.symbol === normalizedSymbol);
        const price = Number(ticker?.lastPrice);
        const minimumQuantity = Number(pair?.minTradeVolume);
        if (tickerResult.code !== 0 || pairResult.code !== 0 ||
            !Number.isFinite(price) || price <= 0 ||
            !Number.isFinite(minimumQuantity) || minimumQuantity <= 0 ||
            !pair?.base) {
          throw new Error("Bitunix did not provide valid market sizing rules.");
        }
        if (!disposed) {
          setMarket({ base: pair.base, price, minimumQuantity });
          setError(null);
        }
      } catch (requestError) {
        if (!disposed && !(requestError instanceof DOMException && requestError.name === "AbortError")) {
          setMarket(null);
          setError(requestError instanceof Error ? requestError.message : "Market sizing data is unavailable.");
        }
      }
    }
    void refresh();
    const timer = window.setInterval(() => void refresh(), 30_000);
    return () => {
      disposed = true;
      window.clearInterval(timer);
      controller?.abort();
    };
  }, [normalizedSymbol]);

  const notional = market && Number.isFinite(quantity) && quantity > 0
    ? quantity * market.price
    : null;
  const minimumNotional = market ? market.minimumQuantity * market.price : null;
  const belowMinimum = Boolean(market && quantity < market.minimumQuantity);

  return (
    <div className={`order-size-preview${belowMinimum ? " below-minimum" : ""}`} aria-live="polite">
      {market && notional !== null && minimumNotional !== null ? (
        <>
          <div>
            <span>Estimated order value</span>
            <strong>≈ ${notional.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })}</strong>
          </div>
          <small>
            Bitunix minimum: {market.minimumQuantity} {market.base}
            {" "}≈ ${minimumNotional.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })}
          </small>
          {belowMinimum && <p role="alert">This quantity is below Bitunix’s current minimum order size.</p>}
        </>
      ) : (
        <small>{error ?? "Loading live price and Bitunix minimum order size…"}</small>
      )}
    </div>
  );
}

function IndicatorEditor<T>({
  title,
  items,
  onAdd,
  onRemove,
  render,
}: {
  title: string;
  items: T[];
  onAdd: () => void;
  onRemove: (index: number) => void;
  render: (item: T, index: number) => React.ReactNode;
}) {
  return (
    <section className="indicator-section">
      <div className="section-heading">
        <div><span className="eyebrow">SIGNAL SETTINGS</span><h3>{title}</h3></div>
        <button type="button" className="secondary-button" onClick={onAdd} disabled={items.length >= 3}><Plus size={15} /> Add ({items.length}/3)</button>
      </div>
      <div className="indicator-config-list">
        {items.map((item, index) => (
          <div className="indicator-config-row" key={`indicator-${index}`}>
            {render(item, index)}
            <button
              type="button"
              className="indicator-remove"
              onClick={() => onRemove(index)}
              aria-label={`Remove ${title} ${index + 1}`}
            >
              <X size={14} />
              <span>Remove</span>
            </button>
          </div>
        ))}
      </div>
      {!items.length && <small>No indicators configured.</small>}
    </section>
  );
}

function CredentialsView({ onSaved }: { onSaved: () => void }) {
  const [apiKey, setApiKey] = useState("");
  const [apiSecret, setApiSecret] = useState("");
  const [saving, setSaving] = useState(false);
  const [saveError, setSaveError] = useState<string | null>(null);
  async function submit(event: FormEvent) {
    event.preventDefault();
    setSaving(true);
    setSaveError(null);
    try {
      await api.saveCredentials(apiKey, apiSecret);
      setApiKey("");
      setApiSecret("");
      onSaved();
    } catch (error) {
      setSaveError(
        error instanceof Error
          ? error.message
          : "We couldn't save the credentials. Check your connection and try again.",
      );
    } finally {
      setSaving(false);
    }
  }
  return (
    <>
      <PageHeader
        eyebrow="EXCHANGE ACCESS"
        title="Connect Bitunix."
        description="Your credentials are encrypted before they reach storage."
      />
      <div className="credentials-layout">
        <div className="security-card">
          <div className="security-card-icon">
            <ShieldCheck size={22} />
          </div>
          <span className="eyebrow">PRIVATE BY DESIGN</span>
          <h2>Credentials stay in the vault.</h2>
          <p>
            The worker retrieves your exchange session through a restricted
            token. Your API secret is never returned in strategy responses.
          </p>
          <div className="security-list">
            <span>
              <Check size={15} />
              Encrypted at rest
            </span>
            <span>
              <Check size={15} />
              Worker-scoped access
            </span>
            <span>
              <Check size={15} />
              No browser persistence
            </span>
          </div>
        </div>
        <form className="credential-form" onSubmit={submit}>
          {saveError && <p className="credential-error" role="alert">{saveError}</p>}
          <div className="form-section-title">
            <KeyRound size={18} />
            <div>
              <strong>Bitunix API credentials</strong>
              <small>Use a key with only the permissions this bot needs.</small>
            </div>
          </div>
          <label className="field">
            API key
            <input
              required
              value={apiKey}
              onChange={(event) => setApiKey(event.target.value)}
              autoComplete="off"
            />
          </label>
          <label className="field">
            API secret
            <input
              required
              type="password"
              value={apiSecret}
              onChange={(event) => setApiSecret(event.target.value)}
              autoComplete="new-password"
            />
          </label>
          <div className="credential-warning">
            <CircleHelp size={16} />
            <span>Never reuse a key that has withdrawal permissions.</span>
          </div>
          <button className="primary-button" disabled={saving}>
            {saving ? "Encrypting..." : "Save encrypted credentials"}
            <ArrowRight size={16} />
          </button>
        </form>
      </div>
    </>
  );
}

export default App;
