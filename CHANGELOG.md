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
  qua cửa sổ đang chọn của compositor.
