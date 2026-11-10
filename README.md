# Ngó Sen cho Wayland (`wl-ngosen`)

**Dành cho niri, Sway, Hyprland.** Dùng GNOME thì cài [Ngó Sen cho GNOME](https://github.com/ngosen/ibus-ngosen),
dùng KDE hay các desktop khác thì cài [Ngó Sen](https://github.com/ngosen/ngosen).

Bộ gõ tiếng Việt Ngó Sen chạy thẳng trên giao thức Wayland input-method-v2, không cần fcitx5 hay IBus.
Đây là **bản thử nghiệm** cho người quen tự cấu hình compositor. Đang làm, chưa có bản phát hành.

## Giới hạn

- Chỉ gõ được trong app Wayland có text-input-v3 (GTK, Qt 6, Firefox, Chrome và Electron mới chạy
  Wayland). App X11 hay Xwayland, như Zalo (Electron 22), không gõ được.
- Không chạy trên GNOME hay KDE: hai desktop này không có input-method-v2.
- Không có emoji, Cứu chữ, Cứu dấu và menu phím ` của bản fcitx5. Chọn chế độ gõ ở menu khay.

## Dựng từ mã nguồn

```bash
git clone --recurse-submodules https://github.com/ngosen/wl-ngosen.git
cd wl-ngosen
cmake -S . -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build
```

## Ghi công

Lõi gõ chữ là của [Ngó Sen](https://github.com/ngosen/ngosen), vốn bắt đầu từ
[fcitx5-lotus](https://github.com/LotusInputMethod/fcitx5-lotus). Giấy phép GPL-3.0-or-later.
