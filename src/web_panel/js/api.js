const API_BASE = "/api";

async function request(path, options = {}) {
  const res = await fetch(API_BASE + path, options);
  if (!res.ok) {
    let detail = res.statusText;
    try {
      detail = (await res.json()).detail || detail;
    } catch (_) {}
    throw new Error(detail);
  }
  return res.status === 204 ? null : res.json();
}

const api = {
  health: () => request("/health"),

  devices: () => request("/devices"),
  setMode: (id, mode) =>
    request(`/devices/${id}/mode`, {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ update_mode: mode }),
    }),
  pushUpdate: (id, firmwareId) =>
    request(`/devices/${id}/update`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ firmware_id: firmwareId }),
    }),

  firmwares: () => request("/firmwares"),
  uploadFirmware: (formData) =>
    request("/firmwares", { method: "POST", body: formData }),

  updates: (deviceId) =>
    request("/updates" + (deviceId ? `?device_id=${encodeURIComponent(deviceId)}` : "")),
};

function el(id) {
  return document.getElementById(id);
}

function fmtTime(value) {
  if (!value) return "—";
  const d = new Date(value.includes("Z") ? value : value.replace(" ", "T") + "Z");
  return isNaN(d) ? value : d.toLocaleString();
}

function fmtSize(bytes) {
  if (!bytes && bytes !== 0) return "—";
  return bytes < 1024 * 1024
    ? `${(bytes / 1024).toFixed(1)} KB`
    : `${(bytes / 1024 / 1024).toFixed(2)} MB`;
}

function badge(status) {
  const known = ["success", "failed", "rolled_back", "pending", "idle"];
  const cls = known.includes(status) ? status : "running";
  return `<span class="badge ${cls}">${status || "idle"}</span>`;
}

function isOnline(lastSeen, windowSeconds = 120) {
  if (!lastSeen) return false;
  const t = new Date(lastSeen.replace(" ", "T") + "Z").getTime();
  return Date.now() - t < windowSeconds * 1000;
}

function toast(message, kind = "error") {
  const box = el("toast");
  if (!box) return;
  box.textContent = message;
  box.className = `toast ${kind} show`;
  setTimeout(() => (box.className = "toast"), 4000);
}

async function pingServer() {
  const dot = el("server-state");
  if (!dot) return;
  try {
    await api.health();
    dot.textContent = "server online";
    dot.className = "server-state ok";
  } catch (_) {
    dot.textContent = "server unreachable";
    dot.className = "server-state down";
  }
}

document.addEventListener("DOMContentLoaded", pingServer);
