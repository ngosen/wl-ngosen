# Mô tả

<!-- Vấn đề người dùng gặp là gì, và thay đổi này giải quyết thế nào. -->

## Loại thay đổi

- [ ] Tính năng mới
- [ ] Sửa lỗi
- [ ] Cập nhật tài liệu
- [ ] Đóng gói hoặc CI

## Liên kết issue

<!-- Fixes #123 (nếu có) -->

## Đã test ở đâu

<!-- Compositor, app, version; ghi rõ cái chưa test. -->

# Checklist

- [ ] Nhánh đích là `next` (tính năng) hoặc `patch` (sửa lỗi của version đang phát hành), không phải `main`
- [ ] Đã chạy `clang-format` (theo `.clang-format`) và `ruff check`, `ruff format` nếu sửa `settings/`
- [ ] Build và test pass (`cmake -S . -B build && cmake --build build && ctest --test-dir build`)
- [ ] Sửa lỗi thì có test hỏng khi bỏ bản sửa
- [ ] Đổi cách gõ chữ thì gửi vào lõi [ngosen/ngosen](https://github.com/ngosen/ngosen), không sửa ở đây
- [ ] Sửa phần chép từ ibus-ngosen thì sửa cả bên đó
- [ ] Đã thêm một dòng vào `CHANGELOG.md` mục `[Chưa phát hành]`
