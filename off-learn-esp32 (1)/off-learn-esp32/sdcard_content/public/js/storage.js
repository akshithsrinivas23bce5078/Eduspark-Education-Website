// public/js/storage.js
// Handles file listing, download, and delete for a specific folder (from ?folder= param)
// OR global storage insights if no folder is specified.

document.addEventListener("DOMContentLoaded", function () {
  const folder = getFolder();
  if (folder) {
    setupFolderControls();
    loadFiles();
  } else {
    loadGlobalStorage();
  }
});

function getFolder() {
  var raw = new URLSearchParams(window.location.search).get("folder");
  return raw ? String(raw).trim() : "";
}

function encodeQuery(value) {
  return encodeURIComponent(String(value || ""));
}

function escapeAttr(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function getCurrentUserRole() {
  try {
    var user = JSON.parse(localStorage.getItem("loggedInUser") || "{}");
    return String(user.role || "").toLowerCase();
  } catch (error) {
    return "";
  }
}

function setupFolderControls() {
  var folder = getFolder();
  var label = document.getElementById("currentFolderLabel");
  var deleteBtn = document.getElementById("deleteCurrentFolderBtn");

  if (label) {
    label.textContent = folder ? "Folder: " + folder : "";
  }

  if (!deleteBtn) return;

  if (folder && getCurrentUserRole() === "faculty") {
    deleteBtn.style.display = "inline-flex";
    deleteBtn.addEventListener("click", function () {
      deleteCurrentFolder(folder);
    });
  } else {
    deleteBtn.style.display = "none";
  }
}

async function loadGlobalStorage() {
  try {
    const res = await fetch("list-files?folder=library");
    if (!res.ok) throw new Error("Failed to fetch storage data");
    const files = await res.json();

    const stats = {
      pdf: { count: 0, size: 0 },
      video: { count: 0, size: 0 },
      audio: { count: 0, size: 0 },
      other: { count: 0, size: 0 },
      total: 0
    };

    files.forEach(f => {
      const size = Number(f.size) || 0;
      const fileName = String(f.file || "");
      const ext = fileName.split('.').pop().toLowerCase();
      stats.total += size;

      if (ext === "pdf") {
        stats.pdf.count++;
        stats.pdf.size += size;
      } else if (["mp4", "avi", "mkv", "mov"].includes(ext)) {
        stats.video.count++;
        stats.video.size += size;
      } else if (["mp3", "wav", "aac"].includes(ext)) {
        stats.audio.count++;
        stats.audio.size += size;
      } else {
        stats.other.count++;
        stats.other.size += size;
      }
    });

    updateStorageUI(stats);
  } catch (err) {
    console.error("Storage load error:", err);
  }
}

function updateStorageUI(stats) {
  const totalGB = 4.0; // Mock capacity for Academic Storage Plan
  const totalBytes = stats.total;
  const totalMB = totalBytes / (1024 * 1024);
  const totalPercent = Math.min((totalMB / (totalGB * 1024)) * 100, 100);

  // Update Overall Donut
  const donutPct = document.getElementById("donutPct");
  const donutFill = document.getElementById("donutFill");
  const usageBig = document.getElementById("usageBig");

  if (donutPct) donutPct.textContent = totalPercent.toFixed(1) + "%";
  if (donutFill) {
    const perimeter = 263.9;
    const offset = perimeter - (perimeter * (totalPercent / 100));
    donutFill.style.strokeDashoffset = offset;
  }
  if (usageBig) usageBig.textContent = totalMB.toFixed(1) + " MB";

  // Update Categories
  updateCategoryRow("Pdf", stats.pdf, totalBytes);
  updateCategoryRow("Video", stats.video, totalBytes);
  updateCategoryRow("Audio", stats.audio, totalBytes);
}

function updateCategoryRow(id, data, globalTotal) {
  const countEl = document.getElementById("cat" + id + "Count");
  const sizeEl = document.getElementById("cat" + id + "Size");
  const pctEl = document.getElementById("cat" + id + "Pct");
  const barEl = document.getElementById("cat" + id + "Bar");

  if (countEl) countEl.textContent = data.count;
  if (sizeEl) sizeEl.textContent = (data.size / (1024 * 1024)).toFixed(1) + " MB";

  const pctOfTotal = globalTotal > 0 ? (data.size / globalTotal) * 100 : 0;
  if (pctEl) pctEl.textContent = pctOfTotal.toFixed(0) + "%";
  if (barEl) barEl.style.width = pctOfTotal + "%";
}

async function loadFiles() {
  var listContainer = document.getElementById("fileList");
  if (!listContainer) return;

  try {
    var folder = getFolder();
    if (!folder) return;

    var res = await fetch("list-files?folder=" + encodeQuery(folder));
    if (!res.ok) throw new Error("Failed to list files");
    var files = await res.json();

    listContainer.innerHTML = "";
    var role = getCurrentUserRole();

    if (files.length === 0) {
      listContainer.innerHTML = "<p class='muted'>No files in this folder.</p>";
      return;
    }

    var table = document.createElement("table");
    table.className = "file-table";
    table.innerHTML = "<thead><tr><th>File Name</th><th>Size</th><th>Actions</th></tr></thead><tbody id='fileTableBody'></tbody>";
    listContainer.appendChild(table);

    var tbody = document.getElementById("fileTableBody");

    files.forEach(function (f) {
      const name = (typeof f === 'object') ? f.file : f;
      const size = (typeof f === 'object' && f.size) ? (Number(f.size) / (1024 * 1024)).toFixed(2) + " MB" : "Unknown";

      var row = document.createElement("tr");

      var actions = '<button class="action-btn" onclick="downloadFile(\'' +
        escapeAttr(folder).replace(/'/g, "\\'") +
        "','" +
        escapeAttr(name).replace(/'/g, "\\'") +
        "')\">Download</button>";

      if (role === "faculty") {
        actions += '<button class="action-btn delete-btn" onclick="deleteFile(\'' +
          escapeAttr(folder).replace(/'/g, "\\'") +
          "','" +
          escapeAttr(name).replace(/'/g, "\\'") +
          "')\">Delete</button>";
      }

      row.innerHTML =
        "<td>" + escapeAttr(name) + "</td>" +
        "<td>" + size + "</td>" +
        "<td class='action-cell'>" + actions + "</td>";

      tbody.appendChild(row);
    });
  } catch (error) {
    listContainer.innerHTML = '<p class="error">Failed to load files.</p>';
  }
}

// Download
function downloadFile(folder, file) {
  window.open("download?folder=" + encodeQuery(folder) + "&file=" + encodeQuery(file), "_blank");
}

// Delete (faculty only)
async function deleteFile(folder, file) {
  if (!confirm('Delete "' + file + '"?')) return;

  const res = await fetch("delete?folder=" + encodeQuery(folder) + "&file=" + encodeQuery(file), {
    method: "POST",
    headers: { "X-User-Role": "faculty" }
  });

  if (res.ok) {
    loadFiles();
  } else {
    alert("Delete failed");
  }
}

async function deleteCurrentFolder(folder) {
  if (!folder) return;
  if (!confirm('Delete folder "' + folder + '" and all files inside it?')) return;

  var response = await fetch("deleteFolder", {
    method: "POST",
    headers: {
      "Content-Type": "application/x-www-form-urlencoded",
      "X-User-Role": "faculty"
    },
    body: "folder=" + encodeQuery(folder)
  });

  var data = await response.json().catch(function () { return {}; });

  if (!response.ok) {
    alert(data.message || "Failed to delete folder");
    return;
  }

  try {
    sessionStorage.removeItem("subjectsFoldersCache");
  } catch (e) {}

  alert(data.message || "Folder deleted");
  window.location.href = "subjects.html";
}
