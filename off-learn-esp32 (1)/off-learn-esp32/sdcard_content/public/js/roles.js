// js/roles.js

function getCurrentUserRole() {
    const rawUser = localStorage.getItem("loggedInUser") || localStorage.getItem("currentUser");

    if (!rawUser) return null;

    try {
        const user = JSON.parse(rawUser);
        return user && typeof user.role === "string" ? user.role.trim().toLowerCase() : null;
    } catch (error) {
        return null;
    }
}

/* -----------------------------
   Restrict Upload Page Access
------------------------------*/
function restrictUploadPage() {
    const role = getCurrentUserRole();

    if (role !== "faculty") {
        alert("Access denied. Faculty only.");
        window.location.href = "dashboard.html";
    }
}

/* -----------------------------
   Hide Upload Button For Students
------------------------------*/
function hideUploadForStudents() {
    const role = getCurrentUserRole();

    if (role === "student") {

        // Hide all links pointing to upload.html
        const uploadLinks = document.querySelectorAll('a[href="upload.html"]');
        uploadLinks.forEach(link => {
            link.style.display = "none";
        });

        // Hide Upload buttons
        const uploadBtn = document.getElementById("uploadBtn");
        if (uploadBtn) uploadBtn.style.display = "none";

        // Hide Quick Links Upload card
        const uploadCards = document.querySelectorAll('a[href="upload.html"]');
        uploadCards.forEach(card => {
            card.style.display = "none";
        });
    }
}

/* -----------------------------
   Hide Delete Buttons For Students
------------------------------*/
function hideDeleteForStudents() {
    const role = getCurrentUserRole();

    if (role === "student") {
        const deleteButtons = document.querySelectorAll(".delete-btn");
        deleteButtons.forEach(btn => {
            btn.style.display = "none";
        });
    }
}

// Expose helpers for pages that call them from inline scripts.
window.getCurrentUserRole = getCurrentUserRole;
window.restrictUploadPage = restrictUploadPage;
window.hideUploadForStudents = hideUploadForStudents;
window.hideDeleteForStudents = hideDeleteForStudents;
