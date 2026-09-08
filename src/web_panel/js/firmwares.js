function firmwareRow(f) {
  return `
    <tr>
      <td>${f.id}</td>
      <td><strong>${f.version}</strong></td>
      <td>${fmtSize(f.file_size)}</td>
      <td><code title="${f.sha256}">${f.sha256.slice(0, 12)}…</code></td>
      <td>${f.changelog || "—"}</td>
      <td><button onclick="toggleFirmware(${f.id}, ${!f.is_active})">${f.is_active ? "Deactivate" : "Activate"}</button></td>
      <td>${fmtTime(f.created_at)}</td>
    </tr>`;
}

async function loadFirmwares() {
  const body = el("firmwares-body");
  try {
    const items = await api.firmwares();
    body.innerHTML = items.length
      ? items.map(firmwareRow).join("")
      : `<tr><td class="empty" colspan="6">No firmware uploaded yet.</td></tr>`;
  } catch (err) {
    body.innerHTML = `<tr><td class="empty" colspan="6">Could not reach the server.</td></tr>`;
    toast(err.message);
  }
}

el("upload-form").addEventListener("submit", async (e) => {
  e.preventDefault();
  const button = e.target.querySelector("button");
  button.disabled = true;
  try {
    const created = await api.uploadFirmware(new FormData(e.target));
    toast(`version ${created.version} uploaded`, "ok");
    e.target.reset();
    loadFirmwares();
  } catch (err) {
    toast(err.message);
  } finally {
    button.disabled = false;
  }
});

loadFirmwares();


async function toggleFirmware(id, active) {
  try {
    await fetch(`/api/firmwares/${id}?is_active=${active}`, {method:"PATCH"});
    toast(active ? "Firmware activated" : "Firmware deactivated", "ok");
    loadFirmwares();
  } catch (err) { toast(err.message); }
}
