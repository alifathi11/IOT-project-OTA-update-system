const OPEN_STATES = ["pending", "downloading", "verified", "installing"];

async function loadChoices() {
  try {
    const [devices, firmwares] = await Promise.all([api.devices(), api.firmwares()]);
    el("device").innerHTML = devices.length
      ? devices.map((d) => `<option value="${d.id}">${d.device_uid} (v${d.current_version})</option>`).join("")
      : `<option value="">no device registered</option>`;
    el("firmware").innerHTML = firmwares.length
      ? firmwares.map((f) => `<option value="${f.id}">v${f.version}</option>`).join("")
      : `<option value="">no firmware uploaded</option>`;
  } catch (err) {
    toast(err.message);
  }
}

function jobRow(j) {
  return `
    <tr>
      <td>#${j.id}</td>
      <td><code>${j.device_uid}</code></td>
      <td>${j.from_version} → <strong>${j.target_version}</strong></td>
      <td>${j.trigger_type}</td>
      <td>${badge(j.status)}</td>
      <td>${fmtTime(j.requested_at)}</td>
    </tr>`;
}

async function loadJobs() {
  const body = el("jobs-body");
  try {
    const open = (await api.updates()).filter((j) => OPEN_STATES.includes(j.status));
    body.innerHTML = open.length
      ? open.map(jobRow).join("")
      : `<tr><td class="empty" colspan="6">No update is in progress.</td></tr>`;
  } catch (err) {
    body.innerHTML = `<tr><td class="empty" colspan="6">Could not reach the server.</td></tr>`;
    toast(err.message);
  }
}

el("push-form").addEventListener("submit", async (e) => {
  e.preventDefault();
  const deviceId = el("device").value;
  const firmwareId = el("firmware").value;
  if (!deviceId || !firmwareId) return toast("pick a device and a firmware first");

  try {
    const job = await api.pushUpdate(deviceId, Number(firmwareId));
    toast(`job #${job.update_id} created for v${job.target_version}`, "ok");
    loadJobs();
  } catch (err) {
    toast(err.message);
  }
});

loadChoices();
loadJobs();
setInterval(loadJobs, 10000);
