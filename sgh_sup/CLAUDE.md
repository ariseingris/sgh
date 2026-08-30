# Project: Smart Green House (Nhà kính Thông minh)

## 🌿 Project Overview (Tổng quan dự án)
This project aims to automate a greenhouse system to monitor and control environmental factors.
* **Goal (Mục tiêu):** Optimize plant growth (Tối ưu hóa sự phát triển cây trồng).
* **Core functions (Chức năng chính):** Auto-watering (Tưới nước tự động), Light control (Điều khiển ánh sáng), and Temperature monitoring (Giám sát nhiệt độ).

## 🏗 Tech Stack (Công nghệ sử dụng)
* **Hardware (Phần cứng):** ESP32, DHT22 (Sensor), Soil Moisture Sensor (Cảm biến độ ẩm đất).
* **Communication (Giao thức):** MQTT, WiFi.
* **Platform (Nền tảng):** Home Assistant or Private Dashboard (Giao diện điều khiển).
* **Database (Cơ sở dữ liệu):** InfluxDB.

## 📁 Directory Structure (Cấu trúc thư mục)
* `/firmware`: Source code for microcontrollers (Mã nguồn vi điều khiển).
* `/backend`: Server-side logic (Logic máy chủ).
* `/docs`: Technical manual and wiring diagrams (Tài liệu kỹ thuật và sơ đồ đấu nối).

## 🎯 Current Task (Nhiệm vụ hiện tại)
* Designing the logic for the **Smart Irrigation** (Hệ thống tưới thông minh) based on soil sensor thresholds.
* Integrating **Real-time alerts** (Thông báo thời gian thực) via Telegram.

## ⚠️ Requirements (Yêu cầu kỹ thuật)
* Low power consumption (Tiêu thụ điện năng thấp).
* Stability in high humidity (Hoạt động ổn định trong môi trường độ ẩm cao).