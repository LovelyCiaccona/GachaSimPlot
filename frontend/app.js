const form = document.getElementById("run-form");
const simulatorSelect = document.getElementById("simulator");
const dynamicFields = document.getElementById("dynamic-fields");
const startBtn = document.getElementById("start-btn");
const runList = document.getElementById("run-list");
const deleteRunBtn = document.getElementById("delete-run-btn");
const currentRun = document.getElementById("current-run");
const runParams = document.getElementById("run-params");
const runStatus = document.getElementById("run-status");
const metricsEl = document.getElementById("metrics");
const logsEl = document.getElementById("logs");
const downloadsEl = document.getElementById("downloads");
const percentileTable = document.getElementById("percentile-table");
const percentileValueHeader = document.getElementById("percentile-value-header");
const chartTitle = document.getElementById("chart-title");
const chart = document.getElementById("distribution-chart");
const chartTooltip = document.getElementById("chart-tooltip");
const ruleNoteBody = document.getElementById("rule-note-body");

let activeRunId = null;
let selectedRunId = null;
let pollTimer = null;
let chartHitAreas = [];
let simulators = [];
let currentFields = [];

function formatNumber(value) {
  if (value === null || value === undefined || Number.isNaN(Number(value))) return "-";
  const n = Number(value);
  if (Math.abs(n) >= 1000) return n.toLocaleString("zh-CN", { maximumFractionDigits: 1 });
  return n.toLocaleString("zh-CN", { maximumFractionDigits: 2 });
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    headers: { "Content-Type": "application/json" },
    ...options,
  });
  const text = await response.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = text;
  }
  if (!response.ok) {
    const message = data && data.error ? data.error : response.statusText;
    throw new Error(message);
  }
  return data;
}

function setStatus(status) {
  runStatus.textContent = status || "idle";
  runStatus.className = `status ${status || "idle"}`;
  startBtn.disabled = status === "running" || status === "queued";
}

function selectedSimulator() {
  return simulators.find((s) => s.id === simulatorSelect.value) || simulators[0];
}

function renderSimulatorOptions() {
  simulatorSelect.innerHTML = simulators
    .map((sim) => `<option value="${sim.id}">${sim.name}</option>`)
    .join("");
}

function fieldId(simId, fieldIdValue) {
  return `f_${simId}_${fieldIdValue}`;
}

function renderDynamicFields(sim) {
  currentFields = sim.fields || [];
  dynamicFields.innerHTML = currentFields
    .map((f) => {
      const id = fieldId(sim.id, f.id);
      if (f.type === "boolean") {
        return `<label><span>${f.label}</span><select id="${id}" data-field-id="${f.id}"><option value="true"${f.default ? " selected" : ""}>是</option><option value="false"${!f.default ? " selected" : ""}>否</option></select></label>`;
      }
      const min = f.min !== undefined ? `min="${f.min}"` : "";
      const step = f.type === "number" ? 'step="1"' : "";
      return `<label><span>${f.label}</span><input id="${id}" data-field-id="${f.id}" type="number" ${min} ${step} value="${f.default ?? 0}" /></label>`;
    })
    .join("");
}

function setFieldValue(simId, fieldName, rawValue) {
  const el = document.getElementById(fieldId(simId, fieldName));
  if (!el || rawValue === undefined || rawValue === null) return;
  if (el.tagName === "SELECT") {
    el.value = typeof rawValue === "boolean" ? (rawValue ? "true" : "false") : String(rawValue);
  } else {
    el.value = String(rawValue);
  }
}

function renderRuleNote(simId) {
  if (!ruleNoteBody) return;
  if (simId === "wuwa") {
    ruleNoteBody.innerHTML = `
      <p>WuWa 模拟流程为先武器池后角色池，主分布固定使用总抽数（武器抽数 + 角色抽数）。</p>
      <p>初始珊瑚可作为起点资源；启用兑换后，在已抽到至少一个限定角色的前提下，达到门槛会触发限定兑换（每样本最多两次）。</p>
    `;
    return;
  }
  ruleNoteBody.innerHTML = `
    <p>联合模拟默认认为武器池消耗的武库配额全部来自角色池产出与初始武库配额。若角色目标已达成但武器仍未达成，会继续抽角色池积累武库配额；额外限定角色只计入溢出统计。</p>
    <p>当目标限定角色为 0 且目标限定武器大于 0 时，分布图和分位数表切换为武器池抽数；其他情况展示角色池抽数。</p>
  `;
}

function collectPayload() {
  const sim = selectedSimulator();
  const payload = { simulator: sim.id };
  for (const f of currentFields) {
    const el = document.getElementById(fieldId(sim.id, f.id));
    if (!el) continue;
    if (f.type === "boolean") {
      payload[f.id] = el.value === "true";
    } else {
      payload[f.id] = Number(el.value || 0);
    }
  }
  return payload;
}

function renderRunParams(run) {
  const params = run?.request?.params || {};
  const simId = run?.request?.simulator || params.simulator || "endfield-joint";
  const simLabel = simId === "wuwa" ? "鸣潮" : "终末地";
  const extra = simId === "wuwa"
    ? [["初始珊瑚", params.initial_coral], ["兑换", params.exchange_enabled ? "开" : "关"]]
    : [["初始武库配额", params.initial_arsenal_quota], ["种子", params.seed || "随机"]];
  const items = [["模拟器", simLabel], ["样本", params.samples], ["目标角色", params.target_char], ["目标武器", params.target_weapon], ...extra];
  runParams.innerHTML = items.map(([k, v]) => `<span>${k}: ${v ?? "-"}</span>`).join("");
}

function renderMetrics(summary) {
  if (!summary || !summary.metrics) {
    metricsEl.innerHTML = "";
    return;
  }
  const m = summary.metrics;
  const sim = summary.simulator || "endfield-joint";
  const endfieldItems = [
    ["平均角色抽数", m.mean_char_pulls, "达成目标平均需要的角色池抽数。"],
    ["标准差", m.stddev_char_pulls, "达成目标所需角色池抽数的离散程度。"],
    ["P50", m.p50, "50% 样本在该抽数以内达成主分布目标。"],
    ["P75", m.p75, "75% 样本在该抽数以内达成主分布目标。"],
    ["P90", m.p90, "90% 样本在该抽数以内达成主分布目标。"],
    ["P95", m.p95, "95% 样本在该抽数以内达成主分布目标。"],
    ["平均保障配额", m.mean_guarantee_quota ?? m.mean_yellow_tickets, "角色池平均获得的保障配额。"],
    ["平均武器抽数", m.mean_weapon_pulls, "达成武器目标时平均武器池抽数。"],
    ["武器抽数标准差", m.stddev_weapon_pulls, "武器池抽数离散程度。"],
    ["平均武器十连", m.mean_weapon_ten_pulls, "平均武器池十连次数。"],
    ["平均剩余武库配额", m.mean_arsenal_quota_left ?? m.mean_weapon_points_left, "达成目标后平均剩余武库配额。"],
    ["平均产出武库配额", m.mean_arsenal_quota_earned ?? m.mean_weapon_points_earned, "角色池平均产出的武库配额。"],
    ["平均常驻角色", m.mean_char_standard, "角色池平均常驻六星数量。"],
    ["平均溢出限定", m.mean_overflow_limited_chars, "超过目标限定角色的平均溢出数量。"],
  ];
  const wuwaItems = [
    ["平均总抽数", m.mean_total_pulls, "武器池与角色池抽数之和的平均值。"],
    ["总抽数标准差", m.stddev_total_pulls, "总抽数离散程度。"],
    ["P50", m.p50, "50% 样本在该总抽数以内达成目标。"],
    ["P75", m.p75, "75% 样本在该总抽数以内达成目标。"],
    ["P90", m.p90, "90% 样本在该总抽数以内达成目标。"],
    ["P95", m.p95, "95% 样本在该总抽数以内达成目标。"],
    ["平均武器抽数", m.mean_weapon_pulls, "武器池平均抽数。"],
    ["武器抽数标准差", m.stddev_weapon_pulls, "武器池抽数离散程度。"],
    ["平均角色抽数", m.mean_char_pulls, "角色池平均抽数。"],
    ["角色抽数标准差", m.stddev_char_pulls, "角色池抽数离散程度。"],
    ["平均剩余珊瑚", m.mean_coral_left, "达成目标后平均剩余珊瑚。"],
    ["平均兑换限定", m.mean_wuwa_char_exchanged, "平均通过珊瑚兑换得到的限定角色数。"],
    ["平均四星武器", m.mean_four_weapon, "平均四星武器数量。"],
    ["平均四星角色", m.mean_four_character, "平均四星角色数量。"],
  ];
  const items = sim === "wuwa" ? wuwaItems : endfieldItems;
  metricsEl.innerHTML = items
    .map(([label, value, tip]) => `<div class="metric"><span title="${tip}">${label}</span><strong>${formatNumber(value)}</strong></div>`)
    .join("");
}

function detectDistributionKey(rows) {
  const sample = rows && rows[0] ? rows[0] : {};
  if ("total_pulls" in sample) return "total_pulls";
  if ("weapon_pulls" in sample) return "weapon_pulls";
  if ("char_pulls" in sample) return "char_pulls";
  return Object.keys(sample).find((key) => key !== "frequency" && key !== "percentile") || "char_pulls";
}

function distributionLabel(key) {
  if (key === "weapon_pulls") return "武器池抽数";
  if (key === "char_pulls") return "角色池抽数";
  if (key === "total_pulls") return "总抽数";
  return "抽数";
}

function renderPercentiles(rows) {
  const selected = new Set([1, 5, 10, 25, 50, 75, 90, 95, 99, 100]);
  const valueKey = detectDistributionKey(rows);
  percentileValueHeader.textContent = distributionLabel(valueKey);
  percentileTable.innerHTML = rows
    .filter((row) => selected.has(Number(row.percentile)))
    .map((row) => `<tr><td>${row.percentile}%</td><td>${formatNumber(row[valueKey])}</td></tr>`)
    .join("");
}

function hideChartTooltip() {
  chartTooltip.style.display = "none";
}

function drawChart(rows) {
  const ctx = chart.getContext("2d");
  chartHitAreas = [];
  hideChartTooltip();
  const dpr = window.devicePixelRatio || 1;
  const cssWidth = chart.clientWidth || 1000;
  const cssHeight = chart.clientHeight || 420;
  chart.width = Math.floor(cssWidth * dpr);
  chart.height = Math.floor(cssHeight * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cssWidth, cssHeight);
  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0, 0, cssWidth, cssHeight);
  if (!rows || !rows.length) {
    chartTitle.textContent = "抽数分布";
    ctx.fillStyle = "#667085";
    ctx.font = "14px Microsoft YaHei, Segoe UI, sans-serif";
    ctx.fillText("暂无分布数据", 24, 40);
    return;
  }

  const margin = { left: 72, right: 64, top: 34, bottom: 48 };
  const width = cssWidth - margin.left - margin.right;
  const height = cssHeight - margin.top - margin.bottom;
  const valueKey = detectDistributionKey(rows);
  const valueLabel = distributionLabel(valueKey);
  chartTitle.textContent = `${valueLabel}分布`;
  const xs = rows.map((r) => Number(r[valueKey]));
  const ys = rows.map((r) => Number(r.frequency));
  const minX = Math.min(...xs);
  const maxX = Math.max(...xs);
  const maxY = Math.max(...ys);
  const total = ys.reduce((a, b) => a + b, 0);
  const xScale = (x) => (maxX === minX ? margin.left + width / 2 : margin.left + ((x - minX) / (maxX - minX)) * width);
  const yScale = (y) => margin.top + height - (y / maxY) * height;
  const cdfScale = (p) => margin.top + height - p * height;

  ctx.strokeStyle = "#d9e0e8";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(margin.left, margin.top);
  ctx.lineTo(margin.left, margin.top + height);
  ctx.lineTo(margin.left + width, margin.top + height);
  ctx.stroke();

  ctx.fillStyle = "#667085";
  ctx.font = "12px Microsoft YaHei, Segoe UI, sans-serif";
  ctx.textAlign = "center";
  for (let i = 0; i <= 5; i += 1) {
    const x = minX + ((maxX - minX) * i) / 5;
    const px = xScale(x);
    ctx.fillText(Math.round(x).toString(), px, margin.top + height + 24);
  }

  ctx.textAlign = "right";
  for (let i = 0; i <= 4; i += 1) {
    const y = (maxY * i) / 4;
    const py = yScale(y);
    ctx.fillText(Math.round(y).toString(), margin.left - 8, py + 4);
    ctx.strokeStyle = "#eef2f6";
    ctx.beginPath();
    ctx.moveTo(margin.left, py);
    ctx.lineTo(margin.left + width, py);
    ctx.stroke();
  }

  const barWidth = Math.max(1, Math.min(10, width / rows.length));
  ctx.fillStyle = "rgba(31, 138, 112, 0.72)";
  rows.forEach((row) => {
    const xVal = Number(row[valueKey]);
    const fVal = Number(row.frequency);
    const x = xScale(xVal);
    const y = yScale(fVal);
    const left = x - barWidth / 2;
    const barHeight = margin.top + height - y;
    ctx.fillRect(left, y, barWidth, barHeight);
    chartHitAreas.push({ x: left, y, w: barWidth, h: Math.max(2, barHeight), label: `${valueLabel} ${xVal}<br>频数 ${fVal}` });
  });

  let cumulative = 0;
  ctx.strokeStyle = "#2563eb";
  ctx.lineWidth = 2;
  ctx.beginPath();
  rows.forEach((row, idx) => {
    cumulative += Number(row.frequency);
    const xVal = Number(row[valueKey]);
    const prob = cumulative / total;
    const x = xScale(xVal);
    const y = cdfScale(prob);
    if (idx === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
    chartHitAreas.push({ x: x - 6, y: y - 6, w: 12, h: 12, label: `${valueLabel} ${xVal}<br>累计概率 ${(prob * 100).toFixed(2)}%` });
  });
  ctx.stroke();

  ctx.fillStyle = "#2563eb";
  cumulative = 0;
  rows.forEach((row) => {
    cumulative += Number(row.frequency);
    const x = xScale(Number(row[valueKey]));
    const y = cdfScale(cumulative / total);
    ctx.beginPath();
    ctx.arc(x, y, 2.5, 0, Math.PI * 2);
    ctx.fill();
  });
}

function showChartTooltip(event, area) {
  const wrap = chart.parentElement.getBoundingClientRect();
  const x = event.clientX - wrap.left;
  const y = event.clientY - wrap.top;
  chartTooltip.innerHTML = area.label;
  chartTooltip.style.display = "block";
  const tipWidth = chartTooltip.offsetWidth;
  const tipHeight = chartTooltip.offsetHeight;
  chartTooltip.style.left = `${Math.min(x + 12, wrap.width - tipWidth - 8)}px`;
  chartTooltip.style.top = `${Math.max(8, Math.min(y + 12, wrap.height - tipHeight - 8))}px`;
}

function findChartHit(clientX, clientY) {
  const rect = chart.getBoundingClientRect();
  const x = clientX - rect.left;
  const y = clientY - rect.top;
  for (let i = chartHitAreas.length - 1; i >= 0; i -= 1) {
    const area = chartHitAreas[i];
    if (x >= area.x && x <= area.x + area.w && y >= area.y && y <= area.y + area.h) return area;
  }
  return null;
}

async function loadRuns() {
  const runs = await api("/api/runs");
  runList.innerHTML = runs
    .slice(0, 12)
    .map((run) => {
      const status = run.status?.status || "unknown";
      const params = run.request?.params || {};
      const sim = run.request?.simulator || params.simulator || "endfield-joint";
      const simLabel = sim === "wuwa" ? "鸣潮" : "终末地";
      const resource = sim === "wuwa" ? `珊瑚:${params.initial_coral ?? 0}` : `武库配额:${params.initial_arsenal_quota ?? 0}`;
      const activeClass = selectedRunId === run.id ? "active" : "";
      return `<button class="run-item ${activeClass}" data-run-id="${run.id}">
        <span class="main">${simLabel} · C${params.target_char ?? "-"} W${params.target_weapon ?? "-"} · N${params.samples ?? "-"}</span>
        <small>${run.id} · ${status} · ${resource}</small>
      </button>`;
    })
    .join("");
  deleteRunBtn.disabled = !selectedRunId;
}

async function loadLogs(runId) {
  const logs = await api(`/api/runs/${runId}/logs`);
  logsEl.textContent = [logs.stdout, logs.stderr ? `\n[stderr]\n${logs.stderr}` : ""].join("");
}

function renderDownloads(runId) {
  downloadsEl.innerHTML = ["summary.json", "distribution.csv", "percentiles.csv", "stats.csv"]
    .map((file) => `<a href="/api/runs/${runId}/files/${file}">${file}</a>`)
    .join("");
}

function clearPoll() {
  if (pollTimer) {
    clearTimeout(pollTimer);
    pollTimer = null;
  }
}

function schedulePoll(runId) {
  clearPoll();
  pollTimer = setTimeout(async () => {
    await loadRuns();
    await loadResult(runId);
  }, 1200);
}

async function loadResult(runId) {
  const run = await api(`/api/runs/${runId}`);
  const status = run.status?.status || "unknown";
  const simId = run?.request?.simulator || run?.request?.params?.simulator || "endfield-joint";
  const sim = simulators.find((s) => s.id === simId) || simulators[0];
  activeRunId = runId;
  selectedRunId = runId;
  currentRun.textContent = runId;
  if (sim && simulatorSelect.value !== sim.id) {
    simulatorSelect.value = sim.id;
    renderDynamicFields(sim);
    renderRuleNote(sim.id);
  }
  const params = run?.request?.params || {};
  for (const field of currentFields) {
    setFieldValue(simulatorSelect.value, field.id, params[field.id]);
  }
  renderRunParams(run);
  setStatus(status);
  renderDownloads(runId);
  await loadLogs(runId);
  await loadRuns();

  if (status === "completed" && run.has_summary) {
    const [summary, distribution, percentiles] = await Promise.all([
      api(`/api/runs/${runId}/summary`),
      api(`/api/runs/${runId}/distribution`),
      api(`/api/runs/${runId}/percentiles`),
    ]);
    renderMetrics(summary);
    renderPercentiles(percentiles);
    drawChart(distribution);
  }
  if (status === "running" || status === "queued") schedulePoll(runId);
  else clearPoll();
}

simulatorSelect.addEventListener("change", () => {
  const sim = selectedSimulator();
  renderDynamicFields(sim);
  renderRuleNote(sim.id);
});

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  clearPoll();
  setStatus("queued");
  logsEl.textContent = "";
  runParams.innerHTML = "";
  metricsEl.innerHTML = "";
  percentileTable.innerHTML = "";
  drawChart([]);
  try {
    const run = await api("/api/runs", {
      method: "POST",
      body: JSON.stringify(collectPayload()),
    });
    activeRunId = run.id;
    await loadRuns();
    await loadResult(run.id);
  } catch (error) {
    setStatus("failed");
    logsEl.textContent = error.message;
  }
});

chart.addEventListener("mousemove", (event) => {
  const area = findChartHit(event.clientX, event.clientY);
  if (area) showChartTooltip(event, area);
  else hideChartTooltip();
});
chart.addEventListener("mouseleave", hideChartTooltip);

runList.addEventListener("click", async (event) => {
  const button = event.target.closest("[data-run-id]");
  if (!button) return;
  await loadResult(button.dataset.runId);
});

deleteRunBtn.addEventListener("click", async () => {
  if (!selectedRunId) return;
  const yes = window.confirm(`确定删除任务 ${selectedRunId} 吗？该操作不可恢复。`);
  if (!yes) return;
  try {
    await api(`/api/runs/${selectedRunId}`, { method: "DELETE" });
    if (activeRunId === selectedRunId) {
      activeRunId = null;
      currentRun.textContent = "未运行";
      runParams.innerHTML = "";
      logsEl.textContent = "";
      downloadsEl.innerHTML = "";
      metricsEl.innerHTML = "";
      percentileTable.innerHTML = "";
      drawChart([]);
      setStatus("idle");
    }
    selectedRunId = null;
    await loadRuns();
  } catch (error) {
    logsEl.textContent = error.message;
  }
});

window.addEventListener("resize", () => {
  if (!activeRunId) return;
  api(`/api/runs/${activeRunId}/distribution`).then(drawChart).catch(() => {});
});

async function init() {
  simulators = await api("/api/simulators");
  renderSimulatorOptions();
  const sim = selectedSimulator();
  renderDynamicFields(sim);
  renderRuleNote(sim.id);
  drawChart([]);
  await loadRuns();
  const first = runList.querySelector("[data-run-id]");
  if (first) await loadResult(first.dataset.runId);
}

init().catch((error) => {
  logsEl.textContent = error.message;
});
