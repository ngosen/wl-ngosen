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
- swaybar không hiện menu khay của app nào: bấm biểu tượng chỉ đổi Anh/Việt. Muốn có menu thì dùng Waybar.

## Cài đặt

wl-ngosen chưa có version phát hành. Khi có, gói sẽ nằm trong cùng repo có chữ ký với bộ gõ Ngó Sen cho fcitx5 và IBus, nên version mới sẽ đến qua update hệ thống (`dnf upgrade`, `apt upgrade`, `pacman -Syu`, `zypper up`).

Repo APT, pacman và zypper dùng chung key fingerprint `6D4A 2075 F985 3BE8 F6B6 FE7E 7550 1858 158B 36CD`; COPR dùng key của COPR. Nếu máy đã có repo Ngó Sen do từng cài bộ gõ cho fcitx5 hoặc IBus qua repo, chỉ cần chạy lệnh cài gói, không thêm repo lần nữa.

Các distro có gói:

- Debian 13, Debian testing (cả Kali Rolling), Ubuntu 24.04, 26.04, 26.10: x86_64 và arm64.
- Fedora 43, 44, 45: x86_64 và aarch64.
- openSUSE Tumbleweed: x86_64.
- Arch, CachyOS, EndeavourOS: x86_64.

Không có gói cho Debian 12 (`bookworm`) và các distro dựa trên nó như deepin 23/25, LMDE 6 vì libadwaita quá cũ cho cửa sổ cài đặt.

**Fedora (COPR, x86_64 và aarch64):**
```bash
sudo dnf copr enable nguyenphi/ngosen
sudo dnf install wl-ngosen
```

**Debian, Ubuntu (APT, x86_64 và arm64):**
```bash
sudo curl -fsSL -o /usr/share/keyrings/ngosen.gpg https://ngosen.github.io/ngosen/apt/ngosen.gpg
echo "deb [signed-by=/usr/share/keyrings/ngosen.gpg] https://ngosen.github.io/ngosen/apt $(. /etc/os-release && echo ${UBUNTU_CODENAME:-$VERSION_CODENAME}) main" | sudo tee /etc/apt/sources.list.d/ngosen.list
sudo apt update
sudo apt install wl-ngosen
```

> [!NOTE]
> Trên Kali, thay phần `$(. /etc/os-release && echo ${UBUNTU_CODENAME:-$VERSION_CODENAME})` bằng `forky`.

**Arch, CachyOS, EndeavourOS (pacman, x86_64):**
```bash
curl -fsSL https://ngosen.github.io/ngosen/arch/ngosen.asc | sudo pacman-key --add -
sudo pacman-key --lsign-key 6D4A2075F9853BE8F6B6FE7E75501858158B36CD
printf '\n[ngosen]\nServer = https://ngosen.github.io/ngosen/arch/$arch\n' | sudo tee -a /etc/pacman.conf
sudo pacman -Syu wl-ngosen
```

**openSUSE Tumbleweed (zypper, x86_64):**
```bash
sudo zypper addrepo https://ngosen.github.io/ngosen/opensuse/ngosen.repo
sudo zypper refresh
sudo zypper install wl-ngosen
```

zypper sẽ hỏi một lần có tin cậy key không; đối chiếu fingerprint ở trên rồi chọn tin cậy.

Có thể cài wl-ngosen cùng máy với bộ gõ cho fcitx5 và IBus, nhưng chỉ chạy một bộ gõ mỗi lúc (tắt fcitx5 trước khi chạy wl-ngosen).

## Tự chạy khi đăng nhập

wl-ngosen không tự chạy; thêm lệnh khởi động vào cấu hình compositor rồi đăng nhập lại để biểu tượng hiện ở tray của Waybar hoặc swaybar.

niri, `~/.config/niri/config.kdl`:
```kdl
spawn-at-startup "wl-ngosen"
```

Sway, `~/.config/sway/config`:
```
exec wl-ngosen
```

Hyprland 0.57 trở lên, `~/.config/hypr/hyprland.lua` (nếu đã có khối `hl.on("hyprland.start", ...)` thì thêm dòng `hl.exec_cmd` vào khối đó):
```lua
hl.on("hyprland.start", function()
  hl.exec_cmd("wl-ngosen")
end)
```

Hyprland cũ hơn còn dùng `hyprland.conf` thì thêm `exec-once = wl-ngosen` (chưa thử trên version cũ).

## Cửa sổ cài đặt

Mở từ mục **Cài đặt** trong menu tray, hoặc chạy `wl-ngosen-settings`.

Cài đặt, gõ tắt và từ điển dùng chung với bộ gõ cho IBus. Dữ liệu sao lưu từ wl-ngosen khôi phục được ở bộ gõ cho IBus và fcitx5, và ngược lại.

## Không gõ được trong app GTK

Nếu từng đặt `gtk-im-module` thành `fcitx` theo wiki fcitx5 hoặc hướng dẫn cũ, app GTK 4 sẽ nạp module của fcitx5 và không nói chuyện với wl-ngosen. Trả key về mặc định:
```bash
gsettings reset org.gnome.desktop.interface gtk-im-module
```

Sau đó mở lại app.

## Dựng từ mã nguồn

Cửa sổ cài đặt cần Python 3 với PyGObject, GTK 4 và libadwaita.

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
