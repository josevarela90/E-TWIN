/* E-TWIN 2.0 OT. Contrato: docs/DATA_CONTRACT.md.
 * Pegar este archivo completo en JavaScript del plugin.
 * No almacena claves. No publica ni escribe datos en ThingSpeak.
 * Estado de nube = instantánea. e/v son validez al publicar, no un latido actual.
 */
(function () {
  "use strict";

  const POLL_MS = 20000;
  const STALE_MS = 65000;
  const MAX_GAP_MS = 32000;
  const STATES = Object.freeze({ WAIT: "SIN LECTURA", STOP: "BAJO UMBRAL", RUN: "OPERANDO", WARN: "ADVERTENCIA", ALARM: "ALARMA", PARTIAL: "PARCIAL", BASE: "REFERENCIA" });
  const KEYS = new Set(["ver", "state", "e", "v", "ea", "va", "alarm", "ref", "dp", "vr", "boot", "seq", "vf", "run", "ot", "rc", "ct", "vx", "vy", "vz", "cf", "fs", "ns", "mi", "rj", "rp"]);

  function finiteNumber(value) {
    if (typeof value === "number") return Number.isFinite(value) ? value : null;
    if (typeof value !== "string" || !/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i.test(value.trim())) return null;
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : null;
  }

  function parseStatus(raw) {
    const out = Object.create(null);
    if (typeof raw !== "string" || raw.length > 1024) return out;
    for (const part of raw.split(";")) {
      const split = part.indexOf("=");
      if (split <= 0) continue;
      const key = part.slice(0, split).trim();
      const value = part.slice(split + 1).trim();
      if (!KEYS.has(key)) continue;
      out[key] = key === "state" ? value : finiteNumber(value);
    }
    return out;
  }

  function parseFeed(raw) {
    if (!raw || typeof raw !== "object") return null;
    const time = Date.parse(raw.created_at);
    if (!Number.isFinite(time)) return null;
    const status = parseStatus(raw.status);
    const metaValid = status.ver === 2 && Object.prototype.hasOwnProperty.call(STATES, status.state) &&
      (status.e === 0 || status.e === 1) && (status.v === 0 || status.v === 1) &&
      (status.ref === 0 || status.ref === 1) && Number.isInteger(status.alarm) && status.alarm >= 0 && status.alarm <= 63 && (status.run === 0 || status.run === 1) && (status.ot === 0 || status.ot === 1);
    const values = Array.from({ length: 8 }, (_, i) => {
      const value = finiteNumber(raw["field" + (i + 1)]);
      return value !== null && value >= 0 && (i !== 5 || value <= 1) ? value : null;
    });
    const energyValid = metaValid && status.e === 1 && values.slice(0, 6).every(x => x !== null);
    const vibrationValid = metaValid && status.v === 1 && values[6] !== null && values[7] !== null && values[7] + 1e-5 >= values[6];
    if (!energyValid) values.fill(null, 0, 6);
    if (!vibrationValid) values.fill(null, 6, 8);
    return { time, entryId: finiteNumber(raw.entry_id), status, metaValid, energyValid, vibrationValid, values };
  }

  function normalizeFeeds(feeds) {
    if (!Array.isArray(feeds)) return [];
    return feeds.slice(-100).map(parseFeed).filter(Boolean).sort((a, b) => a.time - b.time || (a.entryId || 0) - (b.entryId || 0));
  }

  function freshness(sample, now) {
    if (!sample) return { fresh: false, age: null, energyAge: null, vibrationAge: null, future: false };
    const difference = now - sample.time;
    const age = Math.max(0, difference);
    const nodeAge = value => value !== null && Number.isFinite(value) && value >= 0 ? value + age : null;
    return { fresh: difference >= -15000 && difference <= STALE_MS, age, energyAge: nodeAge(sample.status.ea), vibrationAge: nodeAge(sample.status.va), future: difference < -15000 };
  }

  function segments(samples, index) {
    const result = [];
    let current = [];
    let previous = null;
    for (const sample of samples) {
      const value = sample.values[index];
      if (value === null || (previous !== null && sample.time - previous > MAX_GAP_MS)) {
        if (current.length) result.push(current);
        current = [];
      }
      if (value !== null) current.push({ time: sample.time, value });
      previous = sample.time;
    }
    if (current.length) result.push(current);
    return result;
  }

  function csvCell(value) {
    if (value === null || value === undefined) return "";
    let text = String(value);
    if (/^[=+@\t\r]/.test(text) || /^-\D/.test(text)) text = "'" + text;
    return '"' + text.replace(/"/g, '""') + '"';
  }

  // Export only the pure data functions when run by the included Node checks.
  if (typeof module !== "undefined" && module.exports) module.exports = { finiteNumber, parseStatus, parseFeed, normalizeFeeds, freshness, segments, csvCell, STALE_MS };
  if (typeof window === "undefined" || typeof document === "undefined") return;

  const byId = id => document.getElementById(id);
  const text = (id, value) => { byId(id).textContent = value; };
  const format = (value, decimals) => value === null || value === undefined || !Number.isFinite(value) ? "—" : value.toLocaleString("es-EC", { minimumFractionDigits: decimals, maximumFractionDigits: decimals });
  const ageLabel = milliseconds => milliseconds === null ? "sin recepción" : milliseconds < 60000 ? Math.floor(milliseconds / 1000) + " s" : Math.floor(milliseconds / 60000) + " min " + Math.floor(milliseconds % 60000 / 1000) + " s";
  const localTime = time => new Date(time).toLocaleString("es-EC", { day: "2-digit", month: "short", hour: "2-digit", minute: "2-digit", second: "2-digit" });
  const storageKey = "etwin.channel.v1";
  let channel = "";
  let readKey = "";
  let mode = "idle";
  let history = [];
  let networkError = "";
  let pending = null;
  let requestGeneration = 0;
  let busy = false;
  let pollTimer = null;
  let demoTimer = null;
  let demoSequence = 100;
  let lastChartsSignature = "";

  try { const saved = window.localStorage.getItem(storageKey); if (/^[1-9]\d{0,11}$/.test(saved || "")) channel = saved; } catch (_) { /* sandboxed iframe can deny storage */ }

  function setIndicator(id, state) { byId(id).className = "node-indicator" + (state === true ? " ok" : state === false ? " bad" : ""); }
  function setAssessment(title, description, style) { text("assessment-title", title); text("assessment-copy", description); byId("assessment").className = "assessment" + (style ? " " + style : ""); }

  function alarmDescription(mask) {
    const labels = [];
    if (mask & 1) labels.push("tensión fuera del rango configurado");
    if (mask & 2) labels.push("factor de potencia bajo");
    if (mask & 4) labels.push("potencia desviada respecto a referencia");
    if (mask & 8) labels.push("vibración elevada respecto a referencia");
    if (mask & 16) labels.push("límite absoluto de vibración superado");
    if (mask & 32) labels.push("parámetros de máquina pendientes de configurar");
    return labels.length ? labels.join("; ") + "." : "Sin umbrales activados en la última publicación.";
  }

  function render() {
    const sample = history.length ? history[history.length - 1] : null;
    const fresh = freshness(sample, Date.now());
    const stale = sample && !fresh.fresh;
    const values = sample ? sample.values : Array(8).fill(null);
    const metadata = sample && sample.metaValid;
    const state = metadata ? sample.status.state : "WAIT";
    const energy = sample ? sample.energyValid : false;
    const vibration = sample ? sample.vibrationValid : false;
    const incomplete = metadata && ((sample.status.e === 1 && !energy) || (sample.status.v === 1 && !vibration));
    const connected = sample && fresh.fresh && !networkError;
    document.body.classList.toggle("data-stale", Boolean(stale || networkError));
    byId("demo-notice").hidden = mode !== "demo";
    byId("mode-pill").className = "mode-pill" + (mode === "demo" ? " demo" : connected ? " live" : "");
    text("mode-pill", mode === "demo" ? "DEMO · SIMULACIÓN" : connected ? "DATOS DEL CANAL" : mode === "live" ? "CANAL REAL" : "SIN CONEXIÓN");
    text("configure-button", channel && mode === "live" ? "Canal " + channel : "Conectar canal");
    byId("error-notice").hidden = !networkError;
    text("error-notice", networkError);
    byId("connection-dot").className = "connection-dot" + (connected ? " online" : stale ? " warning" : networkError ? " error" : "");
    if (mode === "demo") {
      text("connection-title", "Demostración local");
      text("connection-detail", "Mediciones simuladas. No hay conexión con tus nodos.");
    } else if (networkError) {
      text("connection-title", "No se pudo consultar el canal");
      text("connection-detail", sample ? "La última lectura se conserva con su antigüedad." : "Revisa el canal, la clave de lectura y la conexión.");
    } else if (stale) {
      text("connection-title", fresh.future ? "Revisar reloj del dispositivo" : "Telemetría antigua");
      text("connection-detail", fresh.future ? "El canal tiene una fecha futura respecto a este navegador." : "Última publicación hace " + ageLabel(fresh.age) + ".");
    } else if (sample) {
      text("connection-title", "Última instantánea recibida");
      text("connection-detail", "Antigüedad " + ageLabel(fresh.age) + " · consulta cada 20 s.");
    } else {
      text("connection-title", busy ? "Consultando ThingSpeak…" : mode === "live" ? "Canal sin muestras" : "Esperando configuración");
      text("connection-detail", mode === "live" ? "Esperando la primera publicación del maestro." : "Conecta tu canal de ThingSpeak para comenzar.");
    }

    [["voltage-value", 0, 1], ["current-value", 1, 3], ["power-value", 2, 1], ["energy-value", 3, 3], ["vib-rms-value", 6, 3], ["vib-peak-value", 7, 3]].forEach(item => text(item[0], format(values[item[1]], item[2])));
    text("pf-value", format(values[5], 2));
    text("frequency-value", format(values[4], 1) + " Hz");
    text("apparent-value", format(energy ? values[0] * values[1] : null, 1) + " VA");
    const dp = metadata && energy && sample.status.ref === 1 ? sample.status.dp : null;
    const vr = metadata && vibration && sample.status.ref === 1 ? sample.status.vr : null;
    text("relative-power-value", dp === null || dp === undefined ? "Sin comparación" : (dp > 0 ? "+" : "") + format(dp, 1) + " %");
    text("relative-vibration-value", vr === null || vr === undefined ? "Sin comparación" : format(vr, 2) + " × referencia");

    const nodeLabel = (valid, age) => !sample ? "Sin datos" : valid ? (stale ? "Dato antiguo · " : "Válido al publicar · ") + ageLabel(age) : "Inválido o ausente";
    text("energy-status", nodeLabel(energy, fresh.energyAge));
    text("vibration-status", nodeLabel(vibration, fresh.vibrationAge));
    text("master-status", !sample ? "Sin datos" : stale ? "Última publicación antigua" : mode === "demo" ? "Simulado" : networkError ? "Sin consulta actual" : "Última publicación · " + ageLabel(fresh.age));
    setIndicator("energy-indicator", sample ? energy && fresh.fresh : null);
    setIndicator("vibration-indicator", sample ? vibration && fresh.fresh : null);
    setIndicator("master-indicator", sample ? Boolean(connected) : null);
    // Condicion de funcionamiento declarada por el Core2 (umbral configurado).
    // No implica RPM medidas ni permite actuar sobre la maquina.
    const running = sample && sample.status.run === 1 && energy && fresh.fresh && !networkError && !incomplete;
    byId("twin-drawing").className = "twin-drawing" + (energy && connected ? " energy-ok" : "") + (vibration && connected ? " vibration-ok" : "") + (connected ? " master-ok" : "") + (running ? " machine-running" : "") + (state === "ALARM" && connected ? " machine-alarm" : "");
    // Nunca se muestra un valor stale/invalid como si fuese instantaneo.
    const currEnergy = energy && connected;
    const currVib = vibration && connected;
    const safeFormat = (ok,v,n,unit) => ok ? format(v,n)+(unit ? " "+unit : "") : "—";
    text("scada-voltage", safeFormat(currEnergy,values[0],1,"V"));
    text("scada-current", safeFormat(currEnergy,values[1],3,"A"));
    text("scada-power", safeFormat(currEnergy,values[2],1,"W"));
    text("scada-kwh", safeFormat(currEnergy,values[3],3,"kWh"));
    text("scada-hz", safeFormat(currEnergy,values[4],1,"Hz"));
    text("scada-pf", safeFormat(currEnergy,values[5],2,""));
    text("scada-rms", safeFormat(currVib,values[6],3,"m/s²"));
    text("scada-peak", safeFormat(currVib,values[7],3,"m/s²"));
    text("scada-electric-line", currEnergy ? format(values[0],1)+" V · "+format(values[1],2)+" A · "+format(values[2],0)+" W" : "SENSOR ELECTRICO SIN LECTURA ACTUAL");
    text("scada-vibration-line", currVib ? format(values[6],3)+" RMS · "+format(values[7],3)+" PICO m/s²" : "SENSOR DE VIBRACION SIN LECTURA ACTUAL");
    const extra = (key,decimals) => currVib && Number.isFinite(sample.status[key]) && sample.status[key] >= 0 ? format(sample.status[key],decimals) : "—";
    text("axis-x",extra("vx",3)); text("axis-y",extra("vy",3)); text("axis-z",extra("vz",3));
    text("crest-value",extra("cf",2)); text("sample-rate",extra("fs",1));
    text("window-samples",extra("ns",0)); text("missed-samples",extra("mi",0));
    text("vib-flags",currVib && Number.isInteger(sample.status.vf) ? "0x"+sample.status.vf.toString(16).toUpperCase() : "—");
    text("reference-progress",metadata && connected ? (sample.status.ref === 1 ? (sample.status.rc ?? "—") + " / " + (sample.status.rc ?? "—") : (sample.status.ct ?? 0) + " / " + (sample.status.rc ?? "—")) : "—");
    text("rejected-auth",metadata && connected && Number.isFinite(sample.status.rj) ? format(sample.status.rj,0) : "—");
    text("rejected-replay",metadata && connected && Number.isFinite(sample.status.rp) ? format(sample.status.rp,0) : "—");
    text("data-lag",sample ? ageLabel(fresh.age) : "—");
    const otReported = metadata && sample.status.ot === 1 && connected;
    text("security-status",otReported ? "OT DECLARADO EN MAESTRO" : "OT NO CONFIRMADO");
    byId("security-status").className = "security-pill" + (otReported ? " protected" : "");
    byId("machine-state").className = "state-chip" + (stale || !metadata ? "" : state === "RUN" ? " running" : ["WARN", "ALARM", "PARTIAL"].includes(state) ? " warning" : "");
    text("machine-state", stale ? "DATO ANTIGUO" : !sample ? "SIN DATOS" : !metadata ? "FORMATO INVÁLIDO" : incomplete ? "DATOS INCOMPLETOS" : STATES[state]);

    if (!sample) setAssessment("A la espera de datos", "Conecta un canal o explora la demo para ver cómo se presenta la telemetría.");
    else if (!metadata) setAssessment("Contrato de datos no reconocido", "El campo status debe contener la versión 1 y los metadatos E-TWIN. No se infiere un estado a partir de campos incompletos.", "warning");
    else if (stale || networkError) setAssessment("Último estado: " + STATES[state].toLowerCase(), "Esta lectura no confirma el estado actual de la máquina. Revisa la conexión del maestro y la antigüedad de la publicación.", "warning");
    else if (incomplete) setAssessment("Mediciones incompletas", "La validez declarada en status no coincide con los ocho campos recibidos. Revisa que todos los campos estén habilitados y que el maestro use el mismo contrato de datos.", "warning");
    else if (state === "WAIT") setAssessment("Sin lectura eléctrica válida", "El maestro no dispone de una medición eléctrica actual. No se interpreta como consumo cero.", "warning");
    else if (state === "STOP") setAssessment("Potencia bajo el umbral", "Estado inferido con el umbral de potencia configurado. No demuestra ausencia de tensión ni desconexión física.");
    else if (state === "BASE") setAssessment("Capturando referencia", "Mantén una carga y un montaje estables. El maestro construye una referencia para comparar condiciones equivalentes.");
    else if (state === "PARTIAL") setAssessment("Monitoreo parcial", "La lectura eléctrica está disponible; la vibración es inválida, está ausente o caducó al publicar.", "warning");
    else if (state === "ALARM" || state === "WARN") setAssessment(state === "ALARM" ? "Umbral alto superado" : "Desviación detectada", alarmDescription(sample.status.alarm), state === "ALARM" ? "alarm" : "warning");
    else setAssessment(sample.status.ref === 1 ? "Operación observada" : "Operando · sin referencia", sample.status.alarm ? alarmDescription(sample.status.alarm) : sample.status.ref === 1 ? "No se activaron los umbrales del maestro en esta instantánea. Compara siempre con la misma carga, velocidad y montaje." : "Captura una referencia estable en el Core2 para habilitar la comparación de potencia y vibración.", sample.status.alarm ? "warning" : "good");

    text("history-count", history.length + " muestras");
    byId("export-button").disabled = history.length === 0;
    text("last-update", sample ? (mode === "demo" ? "Simulación · " : "Canal " + channel + " · ") + localTime(sample.time) + " · hace " + ageLabel(fresh.age) : "Sin mediciones recibidas.");
    const signature = history.length + ":" + (sample ? sample.entryId + ":" + sample.time + ":" + sample.values.join(",") : "empty");
    if (signature !== lastChartsSignature) { lastChartsSignature = signature; drawCharts(); }
  }

  function drawChart(id, field, color, decimals) {
    const canvas = byId(id);
    const rect = canvas.getBoundingClientRect();
    const width = Math.max(240, rect.width || 500);
    const height = Math.max(130, rect.height || 180);
    const ratio = Math.min(2, window.devicePixelRatio || 1);
    canvas.width = Math.round(width * ratio); canvas.height = Math.round(height * ratio);
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    ctx.scale(ratio, ratio);
    const left = 42, right = 10, top = 8, bottom = 25;
    const plotWidth = width - left - right, plotHeight = height - top - bottom;
    const all = history.flatMap(sample => sample.values[field] === null ? [] : [sample.values[field]]);
    const maximum = Math.max(field === 6 ? 0.01 : 1, ...(all.length ? all : [1])) * 1.18;
    ctx.font = "9px system-ui, sans-serif";
    ctx.textBaseline = "middle";
    for (let i = 0; i <= 3; i++) {
      const y = top + plotHeight * i / 3;
      ctx.strokeStyle = "#293b4680"; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(width - right, y); ctx.stroke();
      ctx.fillStyle = "#8198a6"; ctx.textAlign = "right"; ctx.fillText(format(maximum * (1 - i / 3), decimals), left - 8, y);
    }
    if (!history.length || !all.length) { ctx.fillStyle = "#718b9a"; ctx.textAlign = "center"; ctx.fillText(history.length ? "Sin mediciones válidas en esta ventana" : "Las mediciones aparecerán aquí", left + plotWidth / 2, top + plotHeight / 2); return; }
    const minTime = history[0].time, maxTime = history[history.length - 1].time;
    const range = Math.max(POLL_MS, maxTime - minTime);
    const x = time => left + (maxTime === minTime ? plotWidth / 2 : (time - minTime) / range * plotWidth);
    const y = value => top + plotHeight * (1 - value / maximum);
    for (const segment of segments(history, field)) {
      ctx.strokeStyle = color; ctx.lineWidth = 1.9; ctx.lineJoin = "round"; ctx.beginPath();
      segment.forEach((point, index) => index ? ctx.lineTo(x(point.time), y(point.value)) : ctx.moveTo(x(point.time), y(point.value)));
      ctx.stroke();
      if (segment.length === 1) { ctx.fillStyle = color; ctx.beginPath(); ctx.arc(x(segment[0].time), y(segment[0].value), 2.3, 0, Math.PI * 2); ctx.fill(); }
    }
    const positions = maxTime === minTime ? [0.5] : [0, 0.5, 1];
    for (const p of positions) { ctx.fillStyle = "#8198a6"; ctx.textAlign = p === 0 ? "left" : p === 1 ? "right" : "center"; const time = maxTime === minTime ? minTime : minTime + (maxTime - minTime) * p; ctx.fillText(new Date(time).toLocaleTimeString("es-EC", { hour: "2-digit", minute: "2-digit" }), left + plotWidth * p, height - 8); }
  }

  function drawCharts() { drawChart("power-chart", 2, "#75e0c6", 0); drawChart("vibration-chart", 6, "#ffa184", 2); }

  function stopRequests() {
    requestGeneration++;
    if (pending) pending.abort();
    pending = null; busy = false;
    window.clearTimeout(pollTimer); window.clearInterval(demoTimer);
    pollTimer = null; demoTimer = null;
  }

  async function poll() {
    if (mode !== "live" || busy || !channel) return;
    const generation = requestGeneration;
    busy = true;
    pending = new AbortController();
    const controller = pending;
    const timeout = window.setTimeout(() => controller.abort(), 12000);
    const url = new URL("https://api.thingspeak.com/channels/" + channel + "/feeds.json");
    url.searchParams.set("results", "100");
    url.searchParams.set("status", "true");
    if (readKey) url.searchParams.set("api_key", readKey);
    render();
    try {
      const response = await fetch(url.toString(), { signal: controller.signal, cache: "no-store", credentials: "omit", referrerPolicy: "no-referrer" });
      if (!response.ok) throw new Error("http:" + response.status);
      const payload = await response.json();
      if (generation !== requestGeneration || mode !== "live") return;
      if (!payload || !Array.isArray(payload.feeds)) throw new Error("format");
      const next = normalizeFeeds(payload.feeds);
      if (payload.feeds.length && !next.length) throw new Error("timestamps");
      history = next;
      networkError = "";
    } catch (error) {
      if (generation !== requestGeneration || mode !== "live") return;
      const message = error && error.message ? error.message : "";
      networkError = message === "http:401" || message === "http:403" || message === "http:404" ? "No se puede acceder al canal. Comprueba el Channel ID y la Read API Key, y que los campos estén habilitados." : message === "format" ? "La respuesta no contiene un feed válido. Revisa el canal y su clave de lectura." : message === "timestamps" ? "El canal devolvió marcas de tiempo no válidas. No se interpretan como mediciones actuales." : "No se pudo leer ThingSpeak. Revisa Internet, el acceso al canal y las políticas del navegador. Se reintentará automáticamente.";
    } finally {
      window.clearTimeout(timeout);
      if (generation === requestGeneration) { busy = false; pending = null; render(); pollTimer = window.setTimeout(poll, POLL_MS); }
    }
  }

  function startLive(nextChannel, nextKey) {
    stopRequests(); mode = "live"; channel = nextChannel; readKey = nextKey; history = []; networkError = ""; lastChartsSignature = "";
    try { window.localStorage.setItem(storageKey, channel); } catch (_) { /* optional convenience only */ }
    render(); poll();
  }

  function demoFeed(time, index) {
    const phase = index / 7;
    const voltage = 220 + 1.4 * Math.sin(phase);
    const power = 1120 + 64 * Math.sin(phase * 0.8) + 18 * Math.sin(phase * 3);
    const pf = 0.88 + 0.025 * Math.sin(phase * 0.4);
    const vibration = 0.43 + 0.05 * Math.sin(phase * 0.7) + 0.015 * Math.sin(phase * 3);
    return parseFeed({ created_at: new Date(time).toISOString(), entry_id: index, field1: voltage, field2: power / (voltage * pf), field3: power, field4: 145.3 + index * 1120 / 1000 * 20 / 3600, field5: 60 + 0.05 * Math.sin(phase), field6: pf, field7: vibration, field8: vibration * 2.5, status: "ver=2;state=RUN;e=1;v=1;ea=200;va=100;alarm=0;ref=1;run=1;ot=1;rc=10;ct=0;vx=0.19;vy=0.21;vz=0.32;cf=2.4;fs=500.0;ns=500;mi=0;rj=0;rp=0;dp=" + ((power / 1120 - 1) * 100).toFixed(1) + ";vr=" + (vibration / 0.43).toFixed(2) + ";boot=100;seq=" + index + ";vf=1" });
  }

  function startDemo() {
    stopRequests(); mode = "demo"; history = []; networkError = ""; lastChartsSignature = "";
    const now = Date.now();
    for (let index = 0; index < 100; index++) history.push(demoFeed(now - (99 - index) * POLL_MS, index + 1));
    demoSequence = 100;
    render();
    demoTimer = window.setInterval(() => { history.push(demoFeed(Date.now(), ++demoSequence)); history = history.slice(-100); render(); }, POLL_MS);
  }

  function stopDemo() { stopRequests(); mode = "idle"; history = []; networkError = ""; lastChartsSignature = ""; render(); }

  function openConfig() { byId("channel-input").value = channel; byId("key-input").value = readKey; text("form-error", ""); const dialog = byId("config-dialog"); if (typeof dialog.showModal === "function") dialog.showModal(); else dialog.setAttribute("open", ""); }
  function closeConfig() { const dialog = byId("config-dialog"); if (typeof dialog.close === "function") dialog.close(); else dialog.removeAttribute("open"); }

  byId("configure-button").addEventListener("click", openConfig);
  byId("close-dialog").addEventListener("click", closeConfig);
  byId("demo-button").addEventListener("click", startDemo);
  byId("stop-demo").addEventListener("click", stopDemo);
  byId("config-form").addEventListener("submit", event => {
    event.preventDefault();
    const nextChannel = byId("channel-input").value.trim();
    const nextKey = byId("key-input").value.trim();
    if (!/^[1-9]\d{0,11}$/.test(nextChannel)) { text("form-error", "Introduce un Channel ID numérico válido."); return; }
    if (nextKey && !/^[A-Za-z0-9_-]{8,128}$/.test(nextKey)) { text("form-error", "La Read API Key tiene un formato no válido."); return; }
    closeConfig(); startLive(nextChannel, nextKey);
  });
  byId("reset-button").addEventListener("click", () => {
    stopRequests(); mode = "idle"; channel = ""; readKey = ""; history = []; networkError = ""; lastChartsSignature = "";
    try { window.localStorage.removeItem(storageKey); } catch (_) { /* optional storage */ }
    byId("channel-input").value = ""; byId("key-input").value = ""; text("form-error", "Configuración borrada."); render();
  });
  byId("export-button").addEventListener("click", () => {
    if (!history.length) return;
    const header = ["origen", "fecha_UTC", "entry_id", "voltaje_V", "corriente_A", "potencia_W", "energia_kWh", "frecuencia_Hz", "factor_potencia", "vibracion_RMS_ms2", "vibracion_pico_ms2", "estado", "electrico_valido", "vibracion_valida", "rms_x", "rms_y", "rms_z", "factor_cresta", "muestreo_hz", "muestras", "flags", "perdidas", "rechazos_ot", "replays_ot"];
    const rows = history.map(sample => [mode === "demo" ? "DEMO_SIMULADA" : "ThingSpeak_" + channel, new Date(sample.time).toISOString(), sample.entryId, ...sample.values, sample.metaValid ? sample.status.state : "INVALID", sample.energyValid ? 1 : 0, sample.vibrationValid ? 1 : 0, ...["vx","vy","vz","cf","fs","ns","vf","mi","rj","rp"].map(k => Number.isFinite(sample.status[k]) ? sample.status[k] : null)]);
    const csv = "\ufeff" + [header, ...rows].map(row => row.map(csvCell).join(",")).join("\r\n");
    const url = URL.createObjectURL(new Blob([csv], { type: "text/csv;charset=utf-8" }));
    const link = document.createElement("a"); link.href = url; link.download = "E-TWIN_" + (mode === "demo" ? "DEMO" : channel) + "_" + new Date().toISOString().slice(0, 10) + ".csv";
    document.body.appendChild(link); link.click(); link.remove(); window.setTimeout(() => URL.revokeObjectURL(url), 1000);
  });
  window.addEventListener("resize", drawCharts);
  document.addEventListener("visibilitychange", () => { if (!document.hidden && mode === "live" && !busy) { window.clearTimeout(pollTimer); poll(); } render(); });
  window.addEventListener("pagehide", () => { stopRequests(); readKey = ""; byId("key-input").value = ""; });
  window.setInterval(render, 1000);
  render();
})();
