import { ChangeEvent, useEffect, useMemo, useRef, useState } from "react";
import type { ReactNode } from "react";

type Profile = Record<string, unknown>;
type ProfileInfo = { id: string; name: string; kind: string; active: boolean };
type Device = { id: string; name: string; description: string; virtualSink: boolean };
type Diagnostics = { pipewireAvailable: boolean; wpctlAvailable: boolean; liveSpeakersPage: boolean; liveHeadphonesPage: boolean; filterChainDetected: boolean; virtualSinkDetected: boolean; message: string };
type PendingSave = { mode: "speakers" | "headphones"; profile: Profile };
type Page = "Dashboard" | "Equalizer" | "Enhancements" | "Profiles" | "Devices" | "Advanced" | "Diagnostics";

declare global { interface Window { __TAURI__?: { core?: { invoke: <T>(command: string, args?: Record<string, unknown>) => Promise<T> } } } }
const invoke = <T,>(command: string, args?: Record<string, unknown>) => {
  const api = window.__TAURI__?.core?.invoke;
  return api ? api<T>(command, args) : Promise.reject(new Error("Desktop backend unavailable. Start this application through Tauri."));
};
const pages: Page[] = ["Dashboard", "Equalizer", "Enhancements", "Profiles", "Devices", "Advanced", "Diagnostics"];
const frequencies = ["31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"];
const icon = (name: string) => <span className={`icon icon-${name}`} aria-hidden="true" />;
const numeric = (profile: Profile, field: string, fallback: number) => typeof profile[field] === "number" ? profile[field] as number : fallback;
const yes = (profile: Profile, field: string, fallback = false) => Number(profile[field] ?? (fallback ? 1 : 0)) !== 0;

function Slider({ label, value, min, max, step = 0.01, unit = "", onChange, disabled = false }: { label: string; value: number; min: number; max: number; step?: number; unit?: string; onChange: (value: number) => void; disabled?: boolean }) {
  return <label className={`control ${disabled ? "disabled" : ""}`}><span>{label}<b>{value.toFixed(step < 1 ? 2 : 0)}{unit}</b></span><input aria-label={label} type="range" min={min} max={max} step={step} value={value} disabled={disabled} onChange={e => onChange(Number(e.target.value))} /></label>;
}
function Toggle({ label, checked, onChange }: { label: string; checked: boolean; onChange: (v: boolean) => void }) { return <label className="toggle"><span>{label}</span><input aria-label={label} type="checkbox" checked={checked} onChange={e => onChange(e.target.checked)} /><i /></label>; }

export default function App() {
  const [page, setPage] = useState<Page>("Dashboard");
  const [profiles, setProfiles] = useState<ProfileInfo[]>([]);
  const [mode, setMode] = useState<"speakers" | "headphones">("speakers");
  const [profile, setProfile] = useState<Profile>({});
  const [devices, setDevices] = useState<Device[]>([]);
  const [diagnostic, setDiagnostic] = useState<Diagnostics | null>(null);
  const [notice, setNotice] = useState("Connecting to the DSP service…");
  const [busy, setBusy] = useState(false);
  const pendingSave = useRef<PendingSave | null>(null);
  const saveTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const saveInFlight = useRef(false);

  const activeProfile = profiles.find(p => p.id === mode);
  const refresh = async () => {
    try {
      const [allProfiles, active, status, audioDevices] = await Promise.all([invoke<ProfileInfo[]>("list_profiles"), invoke<Profile>("load_profile", { id: mode }), invoke<Diagnostics>("diagnostics"), invoke<Device[]>("list_devices").catch(() => [])]);
      setProfiles(allProfiles); setProfile(active); setDiagnostic(status); setDevices(audioDevices); setNotice("Connected. Changes are published to the live DSP page.");
    } catch (error) { setNotice(error instanceof Error ? error.message : "Unable to connect to DTS Control Center."); }
  };
  useEffect(() => { void refresh(); }, [mode]);
  const flushSave = async () => {
    if (saveInFlight.current || !pendingSave.current) return;
    const pending = pendingSave.current;
    pendingSave.current = null;
    saveInFlight.current = true;
    setBusy(true);
    try {
      await invoke("save_profile", { id: pending.mode, profile: pending.profile });
      if (!pendingSave.current && saveTimer.current === null) setNotice("Saved and published to the active DSP profile.");
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Could not publish profile.");
    } finally {
      saveInFlight.current = false;
      setBusy(false);
      if (pendingSave.current) void flushSave();
    }
  };
  const save = (next: Profile) => {
    setProfile(next);
    pendingSave.current = { mode, profile: next };
    if (saveTimer.current !== null) clearTimeout(saveTimer.current);
    saveTimer.current = setTimeout(() => {
      saveTimer.current = null;
      void flushSave();
    }, 150);
  };
  const set = (field: string, value: unknown) => void save({ ...profile, [field]: value });
  const sourceProfiles = useMemo(() => profiles.filter(p => p.id !== "speakers" && p.id !== "headphones"), [profiles]);

  const dashboard = <><section className="hero"><div><p className="eyebrow">DTS:X INSPIRED PROCESSING</p><h1>Make every detail feel closer.</h1><p className="lede">The control surface for the independent PipeWire + LADSPA chain on this machine.</p><div className="hero-actions"><button className="primary" onClick={() => set("bypass", yes(profile, "bypass") ? 0 : 1)}>{yes(profile, "bypass") ? "Enable processing" : "Bypass processing"}</button><button onClick={() => setPage("Devices")}>Choose output</button></div></div><div className="orb"><div>{icon("wave")}<strong>{yes(profile, "bypass") ? "BYPASSED" : "READY"}</strong><small>{mode === "speakers" ? "Speaker path" : "Headphone path"}</small></div></div></section><section className="metrics"><Metric label="DSP page" value={diagnostic?.liveSpeakersPage || diagnostic?.liveHeadphonesPage ? "Available" : "Not created"} good={Boolean(diagnostic?.liveSpeakersPage || diagnostic?.liveHeadphonesPage)} /><Metric label="PipeWire" value={diagnostic?.pipewireAvailable ? "Online" : "Unavailable"} good={Boolean(diagnostic?.pipewireAvailable)} /><Metric label="Profile" value={String(profile.name ?? activeProfile?.name ?? "Not loaded")} /><Metric label="Ceiling" value={`${numeric(profile, "ceiling_db", -1).toFixed(1)} dBFS`} /></section><section className="grid two"><Panel title="Quick enhancement"><Slider label="Bass amount" value={numeric(profile, "bass_amount", 0)} min={0} max={0.55} onChange={v => set("bass_amount", v)} /><Slider label="Stereo width" value={numeric(profile, "width", 1)} min={0} max={2} onChange={v => set("width", v)} /></Panel><Panel title="Active profile"><p className="quiet">{String(profile.name ?? "No active profile")}</p><div className="profile-row">{sourceProfiles.slice(0, 5).map(item => <button key={item.id} onClick={() => activate(item.id)}>{item.id.replace("speakers-", "").replace("headphones-", "")}</button>)}</div></Panel></section></>;
  const equalizer = <><Header title="Graphic equalizer" subtitle="Ten independent user bands sit after the OEM-derived correction EQ." /><Panel title="User EQ" action={<Toggle label="Enable user EQ" checked={yes(profile, "user_eq_enable")} onChange={v => set("user_eq_enable", v ? 1 : 0)} />}><div className="eq">{(Array.isArray(profile.user_eq_g) ? profile.user_eq_g : Array(10).fill(0)).map((gain, index) => <label className="eq-band" key={index}><b>{Number(gain).toFixed(1)}<small>dB</small></b><input aria-label={`${frequencies[index]} Hz EQ`} type="range" min="-12" max="12" step="0.5" value={Number(gain)} onChange={e => { const next = Array.isArray(profile.user_eq_g) ? [...profile.user_eq_g] : Array(10).fill(0); next[index] = Number(e.target.value); void save({ ...profile, user_eq_enable: 1, user_eq_g: next }); }} /><span>{frequencies[index]}</span></label>)}</div><div className="panel-footer"><button onClick={() => void save({ ...profile, user_eq_enable: 1, user_eq_g: Array(10).fill(0) })}>Reset flat</button><span>Live · ±12 dB safety range</span></div></Panel></>;
  const enhancements = <><Header title="Enhancements" subtitle="Controls marked Live are applied at the next audio block boundary." /><div className="grid two"><Panel title="Bass enhancement" badge="LIVE"><Toggle label="Harmonic bass" checked={yes(profile, "bass_enable")} onChange={v => set("bass_enable", v ? 1 : 0)} /><Slider label="Bass amount" value={numeric(profile, "bass_amount", 0)} min={0} max={0.55} onChange={v => set("bass_amount", v)} /><Slider label="Drive" value={numeric(profile, "bass_drive", 1.2)} min={0.5} max={4} onChange={v => set("bass_drive", v)} /><Slider label="Extension" value={numeric(profile, "bass_extend_db", 0)} min={0} max={12} unit=" dB" onChange={v => set("bass_extend_db", v)} /></Panel><Panel title="Loudness & dynamics" badge="LIVE"><Toggle label="Loudness compensation" checked={yes(profile, "loud_enable")} onChange={v => set("loud_enable", v ? 1 : 0)} /><Slider label="Loudness amount" value={numeric(profile, "loud_amount", .6)} min={0} max={1} onChange={v => set("loud_amount", v)} /><Toggle label="Multiband dynamics" checked={yes(profile, "mbhl_enable")} onChange={v => set("mbhl_enable", v ? 1 : 0)} /><p className="quiet">Dynamics parameters are applied as a complete, bounded DSP update.</p></Panel><Panel title="Spatial" badge="LIVE"><Slider label="Stereo width" value={numeric(profile, "width", 1)} min={0} max={2} onChange={v => set("width", v)} /><Slider label="Crossfeed" value={numeric(profile, "crossfeed", 0)} min={0} max={1} onChange={v => set("crossfeed", v)} /></Panel></div></>;
  const profilePage = <><Header title="Sound profiles" subtitle="Original profile keys are retained; saving creates a backup before replacing a user file." /><section className="profile-list">{profiles.map(item => <article key={item.id} className={`profile-card ${item.active ? "active" : ""}`}><div>{icon("profile")}<span><b>{item.name}</b><small>{item.kind}</small></span></div>{item.active ? <span className="chip">ACTIVE</span> : <button onClick={() => activate(item.id)}>Use for {mode}</button>}</article>)}</section><Panel title="Profile actions"><div className="action-row"><button onClick={() => { const target = prompt("New profile name (letters, digits, - or _):"); if (target) void invoke("duplicate_profile", { sourceId: mode, targetId: target }).then(refresh).catch(show); }}>Duplicate active</button><button onClick={() => void invoke("restore_default", { mode }).then(refresh).catch(show)}>Restore default</button><button onClick={() => void invoke<string>("export_profile", { id: mode }).then(text => navigator.clipboard.writeText(text)).then(() => setNotice("Profile JSON copied to clipboard.")).catch(show)}>Copy JSON</button></div></Panel></>;
  const devicesPage = <><Header title="Audio devices" subtitle="Only PipeWire sinks discovered from pw-dump can be selected." /><Panel title="Available outputs" action={<button onClick={() => void refresh()}>Refresh</button>}>{devices.length ? <div className="device-list">{devices.map(device => <button className="device" key={device.id} onClick={() => void invoke("select_device", { id: device.id }).then(() => setNotice(`Default output changed to ${device.description}.`)).catch(show)}><span>{icon(device.virtualSink ? "dts" : "speaker")}</span><span><b>{device.description}</b><small>{device.name} · node {device.id}</small></span>{device.virtualSink && <em>VIRTUAL</em>}</button>)}</div> : <Empty text="No selectable sinks are currently reported by PipeWire." />}</Panel></>;
  const advanced = <><Header title="Advanced DSP" subtitle="Limiter timing remains reload-only because it controls allocated lookahead storage." /><div className="grid two"><Panel title="Gain & protection"><Slider label="Input gain" value={numeric(profile, "input_gain_db", 0)} min={-36} max={24} step={0.5} unit=" dB" onChange={v => set("input_gain_db", v)} /><Slider label="Output gain" value={numeric(profile, "output_gain_db", 0)} min={-36} max={24} step={0.5} unit=" dB" onChange={v => set("output_gain_db", v)} /><Slider label="Output ceiling" value={numeric(profile, "ceiling_db", -1)} min={-12} max={-0.1} step={0.1} unit=" dBFS" onChange={v => set("ceiling_db", v)} /></Panel><Panel title="Reload-only"><Readout label="Sample rate" value={`${numeric(profile, "fs", 48000)} Hz`} /><Readout label="Lookahead" value={`${numeric(profile, "lookahead_ms", 5)} ms`} /><Readout label="Limiter attack" value={`${numeric(profile, "limiter_attack_ms", 2)} ms`} /><p className="quiet">Edit these in a profile and restart PipeWire to recreate the limiter safely.</p></Panel></div></>;
  const diagnosticsPage = <><Header title="Diagnostics" subtitle="Status is derived from PipeWire graph data and the shared-memory transport." /><Panel title="System status"><div className="diagnostics"><Status label="pw-dump" ok={diagnostic?.pipewireAvailable} /><Status label="wpctl" ok={diagnostic?.wpctlAvailable} /><Status label="Speakers live page" ok={diagnostic?.liveSpeakersPage} /><Status label="Headphones live page" ok={diagnostic?.liveHeadphonesPage} /><Status label="DTS sink" ok={diagnostic?.virtualSinkDetected} /></div><p className="notice">{diagnostic?.message ?? "Checking PipeWire…"}</p><div className="action-row"><button onClick={() => void refresh()}>Run again</button><button onClick={() => setPage("Devices")}>Open devices</button></div></Panel></>;
  const render = { Dashboard: dashboard, Equalizer: equalizer, Enhancements: enhancements, Profiles: profilePage, Devices: devicesPage, Advanced: advanced, Diagnostics: diagnosticsPage }[page];
  function show(error: unknown) { setNotice(error instanceof Error ? error.message : "Operation failed."); }
  async function activate(id: string) { try { setBusy(true); await invoke("activate_profile", { mode, sourceId: id }); await refresh(); setNotice("Profile activated and published to the DSP."); } catch (error) { show(error); } finally { setBusy(false); } }
  return <div className="shell"><aside><div className="brand"><div className="brand-mark">D</div><span><b>DTS</b><small>CONTROL CENTER</small></span></div><nav>{pages.map(item => <button key={item} className={page === item ? "selected" : ""} onClick={() => setPage(item)}>{icon(item.toLowerCase())}{item}</button>)}</nav><div className="sidebar-footer"><label>Processing path<select value={mode} onChange={(e: ChangeEvent<HTMLSelectElement>) => setMode(e.target.value as "speakers" | "headphones")}><option value="speakers">Speakers</option><option value="headphones">Headphones</option></select></label><span className={busy ? "busy" : "ready"}>{busy ? "Publishing" : "Live control"}</span></div></aside><main><div className="topbar"><span className="crumb">DTS Control Center <i>/</i> {page}</span><span className="connection">{icon("dot")} {notice}</span></div>{render}</main></div>;
}
function Header({ title, subtitle }: { title: string; subtitle: string }) { return <header className="page-header"><p className="eyebrow">CONTROL SURFACE</p><h1>{title}</h1><p>{subtitle}</p></header>; }
function Panel({ title, children, action, badge }: { title: string; children: ReactNode; action?: ReactNode; badge?: string }) { return <section className="panel"><header><h2>{title} {badge && <span className="chip">{badge}</span>}</h2>{action}</header>{children}</section>; }
function Metric({ label, value, good }: { label: string; value: string; good?: boolean }) { return <article className="metric"><small>{label}</small><b className={good === false ? "bad" : ""}>{value}</b></article>; }
function Readout({ label, value }: { label: string; value: string }) { return <div className="readout"><span>{label}</span><b>{value}</b></div>; }
function Status({ label, ok }: { label: string; ok?: boolean }) { return <div className="status"><i className={ok ? "ok" : "no"} /> <span>{label}</span><b>{ok ? "Detected" : "Not detected"}</b></div>; }
function Empty({ text }: { text: string }) { return <div className="empty">{icon("speaker")}<p>{text}</p></div>; }
