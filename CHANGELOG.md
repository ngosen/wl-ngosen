# Nhật ký thay đổi

Mọi thay đổi đáng kể của Ngó Sen cho Wayland (`wl-ngosen`) được ghi ở đây. Cách ghi theo
[Keep a Changelog](https://keepachangelog.com/vi/1.1.0/). Mỗi dòng nói người dùng thấy gì thay đổi.

Số version của `wl-ngosen` riêng, không theo số của Ngó Sen đầy đủ (`fcitx5-ngosen`). Mỗi release có
tag `wl-ngosen-<version>` và ghi tag lõi Ngó Sen đang dùng.

## [Chưa phát hành]

### Thêm

- Bắt đầu kho `wl-ngosen`: bộ gõ Ngó Sen chạy thẳng trên Wayland cho niri, Sway, Hyprland, chưa gõ được (#1).
- Đọc chung cài đặt, gõ tắt, chế độ theo app với bản IBus trong `~/.config/ngosen`; lần đầu chạy nhập cài đặt của bản
  fcitx5 như bản IBus (#2).
- Chạy được trên compositor có input-method-v2: nhận phím rồi gửi lại nguyên vẹn, chưa gõ tiếng Việt; compositor không
  hỗ trợ (GNOME, KDE) thì thoát êm (#3).
- Phím đổi Anh/Việt (mặc định Ctrl+Shift, đổi được trong cài đặt) chạy như bản IBus và fcitx5; lựa chọn Anh/Việt dùng chung
  với bản IBus (#4).
- Gõ tiếng Việt kiểu Preedit (chữ gạch chân khi đang gõ), dùng chung lõi và cài đặt với bản IBus; ô mật khẩu nhận phím
  nguyên vẹn (#5).
- Giữ phím thì chữ lặp lại theo tốc độ lặp phím của compositor, cả khi đang gõ chữ tiếng Việt (#6).
- Gõ Sen (gõ thẳng vào ô, không gạch chân) là chế độ mặc định như bản IBus; chọn Preedit ở bản IBus (cài đặt dùng chung)
  thì wl-ngosen cũng gõ Preedit.
  Bấm chuột vào giữa một chữ rồi gõ tiếp vẫn có thể sửa nhầm chữ cũ, giống bản fcitx5 trên Sway và Hyprland (#7).
- Nhớ chế độ gõ và lựa chọn Anh/Việt theo từng app như bản IBus và fcitx5, dùng chung tệp cài đặt; app được nhận ra
  qua cửa sổ đang chọn của compositor; app Flatpak mang tên như bản IBus đặt (#8).
- Biểu tượng ngó sen ở khay hệ thống: bấm để đổi Anh/Việt; menu (Waybar có, swaybar không) chọn chế độ gõ cho app
  đang dùng, kiểu gõ, bảng mã, các công tắc như bản IBus và lưu nhật ký gõ. Phím đổi Anh/Việt hiện thông báo ngắn (#10).
- Cửa sổ cài đặt như bản IBus, mở từ mục Cài đặt ở menu khay hoặc lệnh `wl-ngosen-settings`; sao lưu và khôi phục
  dùng chung tệp với bản IBus và bản fcitx5. Không có trang Kiểm tra máy (#11).
- Gói cài cho Debian 13, Ubuntu 24.04 trở lên, Fedora 43 trở lên, openSUSE Tumbleweed và Arch Linux; cài được cùng
  máy với bản fcitx5 và bản IBus (#12).
- Version mới có trên repo APT, pacman, zypper chung và COPR Fedora `nguyenphi/ngosen`, cập nhật qua trình quản lý
  gói; README có lệnh thêm repo và cách tự chạy wl-ngosen khi đăng nhập trên niri, Sway, Hyprland (#13).

### Sửa

- Gõ Sen trong Chromium và Firefox sửa dấu bằng cách xoá thẳng trong ô như bản fcitx5, không còn gửi phím xoá rồi
  chờ tới nửa giây mỗi lần đặt dấu (#9).
