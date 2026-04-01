// public/js/library.js
// Renders ALL files from ALL subject folders with download for everyone
// and delete only for faculty.

function escapeHtml(str) {
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function getFileExtension(filename) {
  var parts = filename.split(".");
  return parts.length > 1 ? parts.pop().toLowerCase() : "";
}

function getFileTypeLabel(ext) {
  switch (ext) {
    case "pdf": return "PDF";
    case "doc": case "docx": return "Word";
    case "mp3": case "wav": return "Audio";
    case "html": case "htm": return "Web";
    case "pptx": case "ppt": return "Presentation";
    case "mp4": return "Video";
    default: return ext.toUpperCase() || "File";
  }
}

function getFileIconClass(ext) {
  switch (ext) {
    case "pdf": return "pdf";
    case "doc": case "docx": return "word";
    case "html": return "word"; // code icon missing, using generic blue
    case "pptx": return "word";
    case "mp3": case "wav": return "video";
    case "mp4": return "video";
    default: return "zip";
  }
}

function getCurrentRole() {
  try {
    var user = JSON.parse(localStorage.getItem("loggedInUser") || "{}");
    return String(user.role || "").toLowerCase();
  } catch (e) {
    return "";
  }
}

async function loadLibrary() {
  var table = document.getElementById("libraryTableBody");
  var emptyState = document.getElementById("emptyLibraryState");
  var tableCard = document.getElementById("libraryTableCard");
  var totalStat = document.getElementById("statTotalBooks");
  var roleStat = document.getElementById("statRoleAccess");

  var role = getCurrentRole();

  if (roleStat) {
    roleStat.textContent = role === "faculty" ? "Faculty: Full Access" : "Student: View & Download";
  }

  if (role === "student") {
    var uploadBtn = document.getElementById("uploadBookBtn");
    var uploadFirst = document.getElementById("uploadFirstBookBtn");
    var deleteLibBtn = document.getElementById("deleteLibraryFolderBtn");
    if (uploadBtn) uploadBtn.style.display = "none";
    if (uploadFirst) uploadFirst.style.display = "none";
    if (deleteLibBtn) deleteLibBtn.style.display = "none";
  }

  try {
    // Aggregated request is much faster than per-folder loop
    var filesRes = await fetch("list-files?folder=library");
    if (!filesRes.ok) throw new Error("Failed to fetch library files");
    var files = await filesRes.json();

    table.innerHTML = "";
    var totalFiles = 0;

    files.forEach(function (item) {
      var file = item;
      var folderName = "";

      if (item && typeof item === 'object') {
        file = item.file || '';
        folderName = item.folder || '';
      }

      if (!file) return;
      totalFiles++;

      var ext = getFileExtension(file);
      var typeLabel = getFileTypeLabel(ext);
      var iconClass = getFileIconClass(ext);
      var escapedFolder = escapeHtml(folderName);
      var escapedFile = escapeHtml(file);

      var actionHtml =
        '<button class="action-btn" onclick="handleDownload(\'' +
        escapedFolder.replace(/'/g, "\\'") + "','" +
        escapedFile.replace(/'/g, "\\'") +
        "')\">Download</button>";

      if (role === "faculty") {
        actionHtml +=
          '<button class="action-btn delete-btn" onclick="handleDelete(\'' +
          escapedFolder.replace(/'/g, "\\'") + "','" +
          escapedFile.replace(/'/g, "\\'") +
          "')\">Delete</button>";
      }

      var row = document.createElement("tr");
      row.innerHTML =
        "<td>" +
        '<div class="file-name">' +
        '<div class="file-icon ' + iconClass + '">' +
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><polyline points="14 2 14 8 20 8"/></svg>' +
        "</div>" +
        "<span>" + escapedFile + "</span>" +
        "</div>" +
        "</td>" +
        "<td>" + typeLabel + "</td>" +
        "<td>" + escapedFolder + "</td>" +
        "<td>—</td>" +
        "<td class='action-cell'>" + actionHtml + "</td>";

      table.appendChild(row);
    });

    if (totalStat) totalStat.textContent = totalFiles;
    if (totalFiles === 0) {
      if (tableCard) tableCard.style.display = "none";
      if (emptyState) emptyState.style.display = "flex";
    } else {
      if (tableCard) tableCard.style.display = "block";
      if (emptyState) emptyState.style.display = "none";
    }
  } catch (err) {
    console.error("Library error:", err);
  }
}

async function handleDownload(folder, file) {
  var url = "download?folder=" + encodeURIComponent(folder) + "&file=" + encodeURIComponent(file);
  window.open(url, "_blank");
}

async function handleDelete(folder, file) {
  if (!confirm('Delete "' + file + '" from ' + (folder || "library") + "?")) return;

  try {
    var res = await fetch("delete?folder=" + encodeURIComponent(folder) + "&file=" + encodeURIComponent(file), {
      method: "POST",
      headers: { "X-User-Role": "faculty" }
    });
    var data = await res.json().catch(function () { return {}; });
    if (!res.ok) {
      alert(data.message || "Delete failed");
      return;
    }
    alert("File deleted");
    loadLibrary();
  } catch (err) {
    alert("Delete error: " + err.message);
  }
}


document.addEventListener("DOMContentLoaded", function () {
  loadLibrary();

  var refreshBtn = document.getElementById("refreshBooksBtn");
  if (refreshBtn) {
    refreshBtn.addEventListener("click", loadLibrary);
  }

  // ================= UPLOAD LIBRARY BOOKS =================
  var uploadBtn = document.getElementById("uploadBookBtn");
  var uploadFirstBtn = document.getElementById("uploadFirstBookBtn");
  var fileInput = document.getElementById("bookFileInput");

  if (fileInput) {
    var triggerFileInput = function () {
      if (getCurrentRole() !== "faculty") {
        alert("Only faculty can upload books.");
        return;
      }
      fileInput.click();
    };

    if (uploadBtn) uploadBtn.addEventListener("click", triggerFileInput);
    if (uploadFirstBtn) uploadFirstBtn.addEventListener("click", triggerFileInput);

    fileInput.addEventListener("change", async function () {
      var files = Array.from(fileInput.files || []);
      if (files.length === 0) return;

      var folder = "library"; // default hardcoded folder for library

      const CHUNK_SIZE = 512;
      const CHUNK_DELAY_MS = 80;
      const FIRST_CHUNK_DELAY_MS = 250;

      const sleep = ms => new Promise(r => setTimeout(r, ms));

      async function uploadInChunks(folderName, file) {
        let offset = 0;
        let first = true;
        while (offset < file.size) {
          if (first) {
            await sleep(FIRST_CHUNK_DELAY_MS);
            first = false;
          }

          const end = Math.min(offset + CHUNK_SIZE, file.size);
          const chunkBlob = file.slice(offset, end);
          const chunkBuffer = await chunkBlob.arrayBuffer();

          const chunkUrl = "/upload?folder=" + encodeURIComponent(folderName) +
            "&file=" + encodeURIComponent(file.name) +
            "&offset=" + offset +
            "&totalSize=" + file.size;

          let chunkResponse = null;
          for (let attempt = 0; attempt < 2; attempt++) {
            const form = new FormData();
            form.append("chunk", new Blob([chunkBuffer]));
            try {
              chunkResponse = await fetch(chunkUrl, {
                method: "POST",
                headers: { "X-User-Role": "faculty" },
                body: form
              });
            } catch (err) {
              chunkResponse = null;
            }

            if (chunkResponse && chunkResponse.ok) break;
            if (attempt === 0) await sleep(200);
          }

          let chunkData = null;
          try {
            if (chunkResponse) chunkData = await chunkResponse.json();
          } catch (err) {}

          if (!chunkResponse || !chunkResponse.ok || !chunkData || chunkData.status !== "ok") {
            throw new Error((chunkData && chunkData.message) || "Chunk upload failed");
          }

          offset = end;
          await sleep(CHUNK_DELAY_MS);
        }
      }

      for (var k = 0; k < files.length; k++) {
        var file = files[k];
        if (file.size > 3 * 1024 * 1024) {
          alert("File too large: " + file.name + " (Max 3MB)");
          continue;
        }

        try {
          await uploadInChunks(folder, file);
        } catch (e) {
          alert("Failed to upload " + file.name + ": " + e.message);
        }
      }

      alert("Upload complete");
      fileInput.value = "";
      loadLibrary();
    });
  }
});
