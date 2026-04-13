// public/js/upload.js

document.addEventListener("DOMContentLoaded", async () => {

  const fileInput = document.getElementById("fileInput");
  const folderSelect = document.getElementById("subject");
  const uploadBtn = document.getElementById("uploadBtn");
  const MAX_UPLOAD_SIZE_BYTES = 3 * 1024 * 1024;
  let uploadInProgress = false;

  const user = JSON.parse(localStorage.getItem("loggedInUser") || "{}");

  // ================= ROLE CHECK =================
  if (!user || user.role !== "faculty") {
    alert("Only faculty can upload");
    window.location.href = "dashboard.html";
    return;
  }

  // ================= LOAD FOLDERS =================
  await loadFolders(folderSelect);

  // ================= SINGLE-REQUEST UPLOAD =================
  // Sends the entire file in one multipart request.
  // ESP32 WebServer handles internal chunking automatically.
  async function uploadFile(folder, file) {
    const form = new FormData();
    form.append("file", file);

    const url = `/upload?folder=${encodeURIComponent(folder)}&file=${encodeURIComponent(file.name)}`;

    console.log(`Uploading file: ${file.name} (${file.size} bytes) to folder: ${folder}`);

    const response = await fetch(url, {
      method: "POST",
      headers: {
        "X-User-Role": "faculty"
      },
      body: form
    });

    let data = null;
    try {
      data = await response.json();
    } catch (e) {
      // Response wasn't valid JSON
    }

    if (!response.ok || !data || data.status !== "success") {
      const msg = (data && data.message) || "Upload failed";
      console.error("Upload failed:", response.status, data);
      throw new Error(msg);
    }

    console.log("Upload success:", data);
  }

  // ================= UPLOAD BUTTON =================
  uploadBtn.addEventListener("click", async () => {
    if (uploadInProgress) {
      return;
    }

    const files = Array.from(fileInput.files || []);
    const folder = folderSelect.value;

    if (!folder) return alert("Select folder");
    if (files.length === 0) return alert("Select file");

    // ================= FILE LIMIT =================
    const res = await fetch(`list-files?folder=${encodeURIComponent(folder)}`, { cache: "no-store" });
    const existingData = await res.json();
    if (!res.ok) {
      return alert((existingData && existingData.message) || "Failed to read folder files");
    }
    const existing = Array.isArray(existingData) ? existingData : [];

    // ================= LOOP OVER ALL SELECTED FILES =================
    uploadInProgress = true;
    uploadBtn.disabled = true;

    // Update button text to show uploading state
    const originalText = uploadBtn.textContent;
    uploadBtn.textContent = "Uploading...";

    try {
      for (let i = 0; i < files.length; i++) {
        const file = files[i];
        const name = file.name.toLowerCase();
        let type = "";

        if (name.endsWith(".pdf")) type = "pdf";
        else if (name.endsWith(".doc") || name.endsWith(".docx")) type = "word";
        else if (name.endsWith(".mp3") || name.endsWith(".wav")) type = "audio";
        else if (name.endsWith(".html") || name.endsWith(".htm")) type = "web";
        else if (name.endsWith(".pptx") || name.endsWith(".ppt")) type = "presentation";
        else if (name.endsWith(".mp4")) type = "video";
        else {
          alert("Skipping invalid file type: " + file.name);
          continue;
        }

        if (file.size > MAX_UPLOAD_SIZE_BYTES) {
          alert("Skipping file too large (Max 3MB): " + file.name);
          continue;
        }

        if (files.length > 1) {
          uploadBtn.textContent = `Uploading ${i + 1}/${files.length}...`;
        }

        console.log(`Starting upload for ${file.name}`);
        await uploadFile(folder, file);
      }
    } catch (error) {
      alert(error && error.message ? error.message : "Upload failed");
      return;
    } finally {
      uploadInProgress = false;
      uploadBtn.disabled = false;
      uploadBtn.textContent = originalText;
    }

    alert("Upload success! Files are now visible to students.");
    window.location.href = `storage.html?folder=${encodeURIComponent(folder)}`;
  });
});

// ================= LOAD FOLDERS =================
async function loadFolders(select) {
  const res = await fetch("folders");
  const folders = await res.json();

  select.innerHTML = `<option value="">Select Folder</option>`;

  folders.forEach(f => {
    const opt = document.createElement("option");
    opt.value = f;
    opt.textContent = f;
    select.appendChild(opt);
  });
}
