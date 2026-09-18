import { FormEvent, useEffect, useState } from "react";
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
  take_profit_percentage: 0,
  trade_quantity: 0.001,
  parameters: {
    rsi_period: 14,
    rsi_overbought: 70,
    rsi_oversold: 30,
    timeframe: "1h",
    hammer_multiplier: 1.5,
    volume_surge_multiplier: 1.5,
    trading_side: "BOTH",
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
  const [authMode, setAuthMode] = useState<AuthMode>("login");
  const [loading, setLoading] = useState(true);
  const [notice, setNotice] = useState<{
    type: "success" | "error";
    text: string;
  } | null>(null);

  async function loadBots() {
    for (let attempt = 0; attempt < 5; attempt += 1) {
      try {
        setBots(await api.getActiveBots());
        setLoading(false);
        return;
      } catch (error) {
        if (error instanceof api.ApiError && (error.status === 401 || error.status === 403)) {
          try {
            const session = await api.refresh();
            setEmail(session.email);
            sessionStorage.setItem("finance-email", session.email);
            setBots(await api.getActiveBots());
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
        showNotice("error", "The control plane is unavailable right now.");
      }
    }
    setLoading(false);
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
      setView("bots");
      showNotice("success", `${saved.symbol} strategy saved.`);
    } catch (error) {
      showNotice(
        "error",
        error instanceof Error ? error.message : "Could not save the strategy",
      );
    }
  }

  if (loading) return <LoadingScreen />;
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
      <Sidebar
        email={email}
        view={view}
        onViewChange={setView}
        onLogout={handleLogout}
      />
      <main className="main-content">
        <div className="mobile-topbar">
          <span className="brand-mark small">A</span>
          <span>Averty Control</span>
          <button onClick={handleLogout} aria-label="Log out">
            <LogOut size={17} />
          </button>
        </div>
        {notice && <Notice notice={notice} />}
        {view === "overview" && (
          <Overview
            bots={bots}
            onCreate={() => {
              setSelectedBot(null);
              setView("bots");
            }}
            onOpenBot={(bot) => {
              setSelectedBot(bot);
              setView("bots");
            }}
          />
        )}
        {view === "bots" && (
          <BotsView
            bots={bots}
            selectedBot={selectedBot}
            onSelect={setSelectedBot}
            onSave={handleSave}
            onCancel={() => setSelectedBot(null)}
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
        <div className="visual-grid" />
        <div className="visual-copy">
          <span className="eyebrow">AVERTY / TRADING SYSTEM</span>
          <h1>Make every signal deliberate.</h1>
          <p>A quiet command center for strategies that run with discipline.</p>
          <div className="signal-card">
            <div>
              <span className="signal-dot" />
              SYSTEM NOMINAL
            </div>
            <strong>
              BTCUSDT <span>+2.84%</span>
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
    <button className={`nav-item ${active ? "active" : ""}`} onClick={onClick}>
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
  onCreate,
  onOpenBot,
}: {
  bots: BotConfig[];
  onCreate: () => void;
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
        <div className="strategy-grid">
          {bots.slice(0, 4).map((bot) => (
            <StrategyCard
              key={bot.id}
              bot={bot}
              onClick={() => onOpenBot(bot)}
            />
          ))}
        </div>
      ) : (
        <EmptyState onCreate={onCreate} />
      )}
    </>
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
function StrategyCard({
  bot,
  onClick,
}: {
  bot: BotConfig;
  onClick: () => void;
}) {
  return (
    <button className="strategy-card" onClick={onClick}>
      <div className="card-top">
        <span className="symbol-badge">{bot.symbol.slice(0, 1)}</span>
        <span className={`status-pill ${bot.active ? "on" : "off"}`}>
          <i />
          {bot.active ? "Active" : "Paused"}
        </span>
      </div>
      <div className="strategy-symbol">{bot.symbol}</div>
      <div className="strategy-meta">
        <span>RSI {bot.parameters?.rsi_period ?? 14}</span>
        <span>{bot.parameters?.timeframe ?? "1h"} frame</span>
      </div>
      <div className="card-footer">
        <span>Step distance</span>
        <strong>{bot.price_step_percentage ?? 0}%</strong>
        <ChevronRight size={16} />
      </div>
    </button>
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
  onSelect,
  onSave,
  onCancel,
}: {
  bots: BotConfig[];
  selectedBot: BotConfig | null;
  onSelect: (bot: BotConfig | null) => void;
  onSave: (symbol: string, draft: BotDraft) => Promise<void>;
  onCancel: () => void;
}) {
  const [newBot, setNewBot] = useState(false);
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
              setNewBot(true);
              onSelect(null);
            }}
          >
            <Plus size={17} />
            New strategy
          </button>
        }
      />
      <div className="bot-layout">
        <div className="bot-list">
          {bots.length ? (
            bots.map((bot) => (
              <button
                key={bot.id}
                className={`bot-row ${selectedBot?.id === bot.id ? "selected" : ""}`}
                onClick={() => {
                  setNewBot(false);
                  onSelect(bot);
                }}
              >
                <span className="symbol-badge small">{bot.symbol[0]}</span>
                <span>
                  <strong>{bot.symbol}</strong>
                  <small>
                    RSI {bot.parameters?.rsi_period ?? 14} ·{" "}
                    {bot.parameters?.timeframe ?? "1h"}
                  </small>
                </span>
                <span className={`status-pill ${bot.active ? "on" : "off"}`}>
                  <i />
                  {bot.active ? "Active" : "Paused"}
                </span>
                <ChevronRight size={16} />
              </button>
            ))
          ) : (
            <div className="list-empty">
              No strategies yet.
              <br />
              Create your first one.
            </div>
          )}
        </div>
        {selectedBot || newBot ? (
          <BotEditor
            bot={selectedBot}
            onSave={onSave}
            onCancel={() => {
              setNewBot(false);
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

function BotEditor({
  bot,
  onSave,
  onCancel,
}: {
  bot: BotConfig | null;
  onSave: (symbol: string, draft: BotDraft) => Promise<void>;
  onCancel: () => void;
}) {
  const normalizeParameters = (parameters: BotDraft["parameters"]) => ({
    ...emptyDraft.parameters,
    ...parameters,
    rsi_indicators: parameters.rsi_indicators?.slice(0, 3) ?? emptyDraft.parameters.rsi_indicators,
    hammer_indicators: parameters.hammer_indicators?.slice(0, 3) ?? emptyDraft.parameters.hammer_indicators,
    volume_indicators: parameters.volume_indicators?.slice(0, 3) ?? emptyDraft.parameters.volume_indicators,
  });
  const [symbol, setSymbol] = useState(bot?.symbol ?? "");
  const [draft, setDraft] = useState<BotDraft>(
    bot
      ? {
          active: bot.active,
          price_step_percentage: bot.price_step_percentage ?? 0,
          take_profit_percentage: bot.take_profit_percentage ?? 0,
          trade_quantity: bot.trade_quantity ?? 0.001,
          parameters: normalizeParameters(bot.parameters),
        }
      : emptyDraft,
  );
  const [saving, setSaving] = useState(false);
  useEffect(() => {
    const nextBot = bot
      ? {
          active: bot.active,
          price_step_percentage: bot.price_step_percentage ?? 0,
          take_profit_percentage: bot.take_profit_percentage ?? 0,
          trade_quantity: bot.trade_quantity ?? 0.001,
          parameters: normalizeParameters(bot.parameters),
        }
      : emptyDraft;
    setSymbol(bot?.symbol ?? "");
    setDraft(nextBot);
  }, [bot]);
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
            min={0}
            max={100}
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
        <button
          type="button"
          className="secondary-button"
          onClick={onCancel}
          disabled={saving}
        >
          Cancel
        </button>
        <button type="submit" className="primary-button" disabled={saving}>
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
        <div><span className="eyebrow">SIGNAL INPUT</span><h3>{title}</h3></div>
        <button type="button" className="secondary-button" onClick={onAdd} disabled={items.length >= 3}><Plus size={15} /> Add ({items.length}/3)</button>
      </div>
      {items.map(render)}
      {!items.length && <small>No indicators configured.</small>}
      {items.map((_, index) => <button type="button" className="text-button" key={`remove-${index}`} onClick={() => onRemove(index)}>Remove indicator {index + 1}</button>)}
    </section>
  );
}

function CredentialsView({ onSaved }: { onSaved: () => void }) {
  const [apiKey, setApiKey] = useState("");
  const [apiSecret, setApiSecret] = useState("");
  const [saving, setSaving] = useState(false);
  async function submit(event: FormEvent) {
    event.preventDefault();
    setSaving(true);
    try {
      await api.saveCredentials(apiKey, apiSecret);
      setApiKey("");
      setApiSecret("");
      onSaved();
    } catch {
      /* handled by the form state below */
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
