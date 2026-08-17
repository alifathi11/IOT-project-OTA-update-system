function deviceRow(d) {
  const online = isOnline(d.last_seen_at);
  return `
    <tr>
      <td>${d.id}</td>
      <td><span class="dot ${online ? "online" : ""}"></span><code>${d.device_uid}</code></td>
      <td>${d.current_version}</td>
      <td>
        <select data-device="${d.id}" class="mode-select">
          <option value="manual"${d.update_mode === "manual" ? " selected" : ""}>manual</option>
          <option value="automatic"${d.update_mode === "automatic" ? " selected" : ""}>automatic</option>
        </select>
      </td>
      <td>${badge(d.last_update_status)}</td>
      <td>${fmtTime(d.last_seen_at)}</td>
    </tr>`;
}

async function loadDevices() {
  const body = el("devices-body");
  try {
    const devices = await api.devices();
    body.innerHTML = devices.length
      ? devices.map(deviceRow).join("")
      : `<tr><td class="empty" colspan="6">No device has registered yet.</td></tr>`;
  } catch (err) {
    body.innerHTML = `<tr><td class="empty" colspan="6">Could not reach the server.</td></tr>`;
    toast(err.message);
  }
}

document.addEventListener("change", async (e) => {
  if (!e.target.classList.contains("mode-select")) return;
  try {
    await api.setMode(e.target.dataset.device, e.target.value);
    toast(`mode set to ${e.target.value}`, "ok");
  } catch (err) {
    toast(err.message);
    loadDevices();
  }
});

loadDevices();
setInterval(loadDevices, 10000);
