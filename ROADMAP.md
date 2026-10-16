# BÁO CÁO LỘ TRÌNH CHUYỂN ĐỔI DỰ ÁN SMART IR REMOTE SANG C (ESP32-S3)

> **Dự án gốc**: `03_Smart_IR_Remote_V2_littleFS` (Viết bằng C++, ESP-IDF)  
> **Dự án đích**: `S3` (Viết bằng C thuần, ESP-IDF v5.5.5, FreeRTOS, PSRAM)  
> **Phiên bản tài liệu**: `v1.0`  
> **Ngày lập**: 16/10/2026  

---

## 1. SƠ ĐỒ KIẾN TRÚC HỆ THỐNG DỰ ÁN S3 (C ARCHITECTURE)

```mermaid
flowchart TB
    subgraph APP_LAYER ["TẦNG ỨNG DỤNG & ĐIỀU KHIỂN"]
        MAIN["app_main.c\n(Task giám sát & Vòng lặp)"]
        DEVICE_MGR["Device Manager\n(app_device.c / cJSON)"]
        SCHEDULE["Schedule & Timer\n(app_schedule.c)"]
    end

    subgraph NETWORK_LAYER ["TẦNG MẠNG & KẾT NỐI (NETWORK)"]
        BLUFI["app_blufi.c\n(Cấu hình mạng BLE)"]
        WIFI["app_wifi.c\n(Station mode & Reconnect)"]
        BLE_MESH["app_blemesh.c\n(Relay / OnOff / Vendor Model)"]
    end

    subgraph IR_VOICE_LAYER ["TẦNG HỒNG NGOẠI & GIỌNG NÓI (IR & VOICE)"]
        RMT_TX["app_rmt_tx.c\n(Phát xung IR 38kHz)"]
        RMT_RX["app_rmt_rx.c\n(Học lệnh IR / Đo xung)"]
        IR_ENCRYPT["app_rawcode_encrypt.c\n(Nén mã thô IR)"]
        UART_VOICE["app_uart_com.c\n(Giao tiếp chip RA Voice)"]
    end

    subgraph PERIPHERAL_LAYER ["TẦNG NGOẠI VI (PERIPHERAL)"]
        BUTTON["app_button.c\n(Quét phím GPIO 8)"]
        GPIO_LED["app_gpio.c\n(LED trạng thái / Nguồn)"]
        SPEAKER["app_speaker.c\n(MAX98357A I2S Player)"]
        SHT3X["app_sht3x.c\n(Cảm biến nhiệt/ẩm I2C)"]
    end

    subgraph CORE_LAYER ["TẦNG CỐT LÕI (CORE & HAL)"]
        LITTLEFS["app_littlefs.c\n(Mount /storage)"]
        COMMON["app_common.c\n(PSRAM, Mutex, Log)"]
        CONFIG["app_config.h\n(Định nghĩa chân & Tham số)"]
    end

    %% Quan hệ
    BUTTON -->|Kích hoạt 3s| BLUFI
    BUTTON -->|Phản hồi beep| SPEAKER
    BLUFI -->|Cấp SSID/PASS| WIFI
    WIFI -->|Trạng thái IP| BLUFI
    DEVICE_MGR -->|Lưu JSON| LITTLEFS
    DEVICE_MGR -->|Gửi lệnh IR| RMT_TX
    BLE_MESH -->|Điều khiển| DEVICE_MGR
    SHT3X -->|Đọc I2C| BLE_MESH
    MAIN --> CORE_LAYER
```

---

## 2. LỘ TRÌNH TỔNG THỂ 5 GIAI ĐOẠN

```mermaid
gantt
    title LỘ TRÌNH CHUYỂN ĐỔI TOÀN DIỆN DỰ ÁN S3 (C)
    dateFormat  YYYY-MM-DD
    section Giai đoạn 0 (Nền tảng)
    Cấu trúc Common, PSRAM, LittleFS       :done,    des1, 2026-10-01, 2026-10-05
    Driver Loa I2S MAX98357A               :done,    des2, 2026-10-05, 2026-10-10
    Tích hợp BLE Mesh cơ bản               :done,    des3, 2026-10-10, 2026-10-15
    section Giai đoạn 1 (Cấp mạng BluFi)
    Driver Nút bấm GPIO 8 (app_button)     :active,  p1_1, 2026-10-16, 2026-10-17
    Wi-Fi Station Manager (app_wifi)       :         p1_2, 2026-10-17, 2026-10-19
    BluFi GATT Server & Adv (app_blufi)    :         p1_3, 2026-10-18, 2026-10-21
    Test kết nối App Vconnex qua BluFi     :         p1_4, 2026-10-21, 2026-10-22
    section Giai đoạn 2 (Hồng ngoại IR)
    RMT TX - Phát sóng mang IR 38kHz       :         p2_1, 2026-10-23, 2026-10-25
    RMT RX - Thu học lệnh & Lọc nhiễu      :         p2_2, 2026-10-25, 2026-10-27
    Giải thuật nén/mã hóa Rawcode          :         p2_3, 2026-10-27, 2026-10-29
    section Giai đoạn 3 (Quản lý Thiết bị)
    Parser & Generator cJSON Device        :         p3_1, 2026-10-30, 2026-11-02
    Port thư viện AC Protocol thông dụng   :         p3_2, 2026-11-02, 2026-11-06
    Lưu trữ cấu hình trên LittleFS         :         p3_3, 2026-11-06, 2026-11-08
    section Giai đoạn 4 (Ngoại vi & Mạng)
    Driver Cảm biến nhiệt ẩm SHT3x         :         p4_1, 2026-11-09, 2026-11-11
    LED trạng thái & Nguồn (app_gpio)      :         p4_2, 2026-11-11, 2026-11-12
    Ghép nối BLE Mesh với IR & SHT3x       :         p4_3, 2026-11-13, 2026-11-16
    section Giai đoạn 5 (Voice chip RA)
    UART giao tiếp MCU Voice Renesas RA    :         p5_1, 2026-11-17, 2026-11-20
    Xử lý Voice tag & Tinh chỉnh hoàn thiện:         p5_2, 2026-11-20, 2026-11-24
```

---

## 3. CHI TIẾT GIAI ĐOẠN 1: THÊM THIẾT BỊ VÀO APP BẰNG BLUFI KHI BẤM NÚT

### 3.1. Sơ đồ tuần tự xử lý (Sequence Flow)

```mermaid
sequenceDiagram
    autonumber
    actor User as Người dùng
    participant Btn as app_button (GPIO 8)
    participant Spk as app_speaker (I2S)
    participant Blufi as app_blufi (BLE)
    participant Wifi as app_wifi (STA)
    actor App as App Vconnex / EspBlufi

    Note over User,Btn: Người dùng nhấn giữ nút GPIO 8 trong 3 giây
    User->>Btn: Giữ nút >= 3000ms
    Btn->>Spk: app_speaker_PlayBeep(2000Hz, 300ms) / Voice
    Btn->>Blufi: app_blufi_Start()
    
    Note over Blufi: Khởi tạo BLE Host + Bluedroid + Profile BluFi
    Blufi->>Blufi: Tạo tên ADV: "VCONNEX-" + 2 byte MAC cuối
    Blufi-->>App: Phát BLE ADV (Advertising)

    User->>App: Mở App, tìm và bấm Kết nối thiết bị
    App->>Blufi: Kết nối BLE GATT
    Blufi->>Blufi: Dừng ADV, chuyển trạng thái Connected
    
    App->>Blufi: Gửi thông tin Wi-Fi (SSID, Password)
    Blufi->>Wifi: app_wifi_ConnectWithConfig(ssid, pass)
    
    Note over Wifi: Kết nối tới Wi-Fi Access Point
    alt Kết nối thành công
        Wifi-->>Blufi: Trả về trạng thái: IP_EVENT_STA_GOT_IP
        Blufi-->>App: Gửi báo cáo ESP_BLUFI_STA_CONN_SUCCESS
        App->>Blufi: Gửi Custom Data (Device ID / Token qua JSON)
        Blufi-->>App: Phản hồi Custom Data OK
        Blufi->>Blufi: Lưu cấu hình, kết thúc BluFi (app_blufi_Stop)
        Blufi->>Spk: Phát âm thanh thành công
    else Kết nối thất bại (Sai pass / Không thấy AP)
        Wifi-->>Blufi: WIFI_EVENT_STA_DISCONNECTED
        Blufi-->>App: Gửi báo cáo ESP_BLUFI_STA_CONN_FAIL
        Note over Blufi,App: App yêu cầu nhập lại mật khẩu
    end

    Note over Blufi: Timeout 90 giây nếu không có kết nối sẽ tự hủy chế độ BluFi
```

---

### 3.2. Sơ đồ máy trạng thái nút nhấn (Button State Machine)

```mermaid
stateDiagram-v2
    [*] --> STATE_IDLE: Nút nhả (Level = 1)
    
    STATE_IDLE --> STATE_PRESSED: Nhấn nút (Level = 0)
    
    state STATE_PRESSED {
        [*] --> COUNTING: Bắt đầu đếm tick (10ms)
        COUNTING --> SHORT_PRESS: Nhả nút trước 3s (< 3000ms)
        COUNTING --> HOLD_3S: Giữ đủ 3s (3000ms)
        HOLD_3S --> HOLD_10S: Giữ tiếp đủ 10s (10000ms)
    }

    SHORT_PRESS --> ACTION_CLICK: Phát âm thanh click ngắn / Toggle LED
    ACTION_CLICK --> STATE_IDLE

    HOLD_3S --> ACTION_BLUFI: 1. Kêu Beep xác nhận\n2. Bật chế độ BluFi (app_blufi_Start)\n3. Chuyển LED sang chớp nhanh
    ACTION_BLUFI --> STATE_IDLE: Nhả nút

    HOLD_10S --> ACTION_RESET: 1. Kêu Beep dài\n2. Xóa Wi-Fi NVS\n3. Factory Reset hệ thống
    ACTION_RESET --> STATE_IDLE: Khởi động lại
```

---

### 3.3. Các module cần triển khai trong Giai đoạn 1

| STT | File cần tạo / sửa | Mô tả trách nhiệm | Trạng thái |
| :---: | :--- | :--- | :---: |
| 1 | `app_config.h` | Bổ sung định nghĩa chân `BUTTON_PIN (8)`, cấu hình BluFi timeout, tên thiết bị. | Đang chờ |
| 2 | `peripheral/app_button.h` | Khai báo API: `app_button_Init()`, callback sự kiện nút. | Đang chờ |
| 3 | `peripheral/app_button.c` | Task FreeRTOS quét nút 10ms, chống rung, xử lý Short/3s/10s. | Đang chờ |
| 4 | `network/app_wifi.h` | Khai báo API: `app_wifi_Init()`, `app_wifi_Connect()`, `app_wifi_IsConnected()`. | Đang chờ |
| 5 | `network/app_wifi.c` | Khởi tạo Netif Station, quản lý event loop, tự động reconnect. | Đang chờ |
| 6 | `network/app_blufi.h` | Khai báo API: `app_blufi_Init()`, `app_blufi_Start()`, `app_blufi_Stop()`. | Đang chờ |
| 7 | `network/app_blufi.c` | Cài đặt Bluetooth GATT Server, Adv, nhận SSID/Pass & Custom JSON. | Đang chờ |
| 8 | `CMakeLists.txt` | Đăng ký các file `.c` mới và các thư viện `esp_wifi`, `esp_netif`, `bt`. | Đang chờ |
| 9 | `app_main.c` | Khởi động `app_button_Init()` và `app_wifi_Init()` trong hàm `app_main`. | Đang chờ |

---

## 4. BẢNG THEO DÕI TIẾN ĐỘ TỔNG QUAN

```mermaid
pie title TỈ LỆ HOÀN THÀNH CHUYỂN ĐỔI S3
    "Đã hoàn thành (Core, Spk, BLE Mesh)" : 30
    "Giai đoạn 1 (Button + BluFi + WiFi)" : 20
    "Giai đoạn 2 (Hồng ngoại RMT TX/RX)" : 20
    "Giai đoạn 3 (Quản lý thiết bị cJSON)" : 15
    "Giai đoạn 4 & 5 (SHT3x + Voice)" : 15
```
