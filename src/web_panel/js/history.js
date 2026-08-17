function eventList(events) {
  if (!events.length) return "—";
  return `<ul class="events">${events
    .map((e) => `<li>${fmtTime(e.created_at)} — ${e.status}${e.message ? `: ${e.message}` : ""}</li>`)
    .join("")}</ul>`;
}

function historyRow(j) {
  return `
    <tr>
      <td>#${j.id}</td>
      <td><code>${j.device_uid}</code></td>
      <td>${j.from_version} → <strong>${j.target_version}</strong></td>
      <td>${j.trigger_type}</td>
      <td>${badge(j.status)}${j.error_message ? `<div class="events">${j.error_message}</div>` : ""}</td>
      <td>${eventList(j.events || [])}</td>
    </tr>`;
}

async function loadHistory() {
  const body = el("history-body");
  try {
    const jobs = await api.updates(el("device").value);
    body.innerHTML = jobs.length
      ? jobs.map(historyRow).join("")
      : `<tr><td class="empty" colspan="6">No update has been requested yet.</td></tr>`;
  } catch (err) {
    body.innerHTML = `<tr><td class="empty" colspan="6">Could not reach the server.</td></tr>`;
    toast(err.message);
  }
}

async function loadDeviceFilter() {
  try {
    const devices = await api.devices();
    el("device").insertAdjacentHTML(
      "beforeend",
      devices.map((d) => `<option value="${d.id}">${d.device_uid}</option>`).join("")
    );
  } catch (_) {}
}

el("filter-form").addEventListener("submit", (e) => {
  e.preventDefault();
  loadHistory();
});

el("device").addEventListener("change", loadHistory);

loadDeviceFilter();
loadHistory();
