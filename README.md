📚 EduSpark – Smart Education Website (Arduino Powered)

EduSpark is a web-based educational platform powered by Arduino (ESP32) that hosts a dynamic website directly from a microcontroller. It combines embedded systems with web technologies to deliver an interactive learning interface.

🚀 Overview

This project demonstrates how an ESP32 microcontroller can act as a web server, hosting a fully functional educational website built with HTML, CSS, and JavaScript.

It is ideal for:

Embedded systems projects
IoT-based web applications
Smart education solutions
⚡ Features
🌐 Website hosted directly on ESP32
📶 Works over local WiFi network
🎓 Educational content interface
📱 Responsive design (HTML/CSS/JS)
⚙️ Real-time interaction with hardware (if implemented)
🔌 Lightweight and efficient
🛠️ Tech Stack
🔹 Hardware
Arduino / ESP32
🔹 Software
Arduino IDE
Embedded C / Arduino Code
HTML5
CSS3
JavaScript

⚙️ How It Works
ESP32 connects to a WiFi network
It starts a web server
HTML, CSS, and JS files are served from:
SPIFFS / LittleFS (internal storage)
Users access the site via ESP32 IP address
🔹 Libraries (if used)
WiFi.h
WebServer.h
