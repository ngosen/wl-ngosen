[English](CONTRIBUTING.en.md) | [Tiếng Việt](CONTRIBUTING.md)

# Đóng góp cho Ngó Sen cho Wayland

Cảm ơn bạn quan tâm đến `wl-ngosen`! Kho này là phần vỏ Wayland của Ngó Sen: nó nối compositor Wayland với lõi gõ chữ trong
submodule `ngosen` ([ngosen/ngosen](https://github.com/ngosen/ngosen)). Quy tắc chi tiết cho người sửa code nằm
trong [AGENTS.md](AGENTS.md).

## Báo lỗi, hỏi đáp và đề xuất

- **Gõ ra sai chữ, treo, mất chữ:** mở issue bằng form [Báo lỗi](https://github.com/ngosen/wl-ngosen/issues/new/choose),
  kèm nhật ký gõ ("Lưu nhật ký gõ" trong menu khay ngay sau khi lỗi xảy ra).
- **Cài, cập nhật hay gỡ không được:** form "Lỗi cài đặt hoặc cập nhật", dán toàn bộ output trong terminal.
- **Muốn có tính năng mới:** form "Yêu cầu tính năng". Tìm trong các yêu cầu đã có trước.
- **Hỏi cách dùng, cách cài:** mục [Hỏi đáp](https://github.com/ngosen/wl-ngosen/discussions) trong Discussions.
- **Lỗi của bản fcitx5** (`fcitx5-ngosen`) báo ở [kho ngosen](https://github.com/ngosen/ngosen/issues/new/choose).

## Dựng và chạy test

Các gói cần cài có trong mục [Dựng từ mã nguồn](README.md#dựng-từ-mã-nguồn) của README.

```bash
git clone --recurse-submodules https://github.com/ngosen/wl-ngosen.git
cd wl-ngosen
cmake -S . -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build
```

Dùng lại thư mục `build/` cho mỗi lần dựng; không tạo thư mục mới cho mỗi lần thử.

## Gửi thay đổi

1. Fork kho, tạo nhánh từ `next` (tính năng) hoặc `patch` (sửa lỗi của version đang phát hành).
2. Một thay đổi một commit, theo [Conventional Commits](https://www.conventionalcommits.org/) bằng tiếng Anh:
   `fix(sen): keep the word after a click`.
3. Trước khi push: `clang-format -i` các file C++ đã sửa, `ruff check` và `ruff format` nếu sửa `settings/`,
   rồi `ctest --test-dir build`.
4. Sửa lỗi thì kèm test hỏng khi bỏ bản sửa.
5. Thêm một dòng vào `CHANGELOG.md` mục `[Chưa phát hành]`: người dùng thấy gì thay đổi, kèm số PR, bằng tiếng Việt.
6. Mở PR vào `next` hoặc `patch` và điền mẫu PR.

Đổi cách gõ chữ (bỏ dấu, tự sửa, Gõ Sen) là việc của lõi: gửi PR vào [ngosen/ngosen](https://github.com/ngosen/ngosen).

## Nhánh

- `main` chỉ chứa bản phát hành, đổi qua PR phát hành của maintainer ([RELEASING.md](RELEASING.md)). Không gửi PR vào `main`.
- `next`: tính năng và mọi thay đổi khác.
- `patch`: sửa lỗi của version đang phát hành, ra thành version X.Y.Z+1 rồi được đưa sang `next`.
- Không chắc thì gửi vào `next`; maintainer sẽ chuyển nếu cần.

## Quy tắc ứng xử

Mọi người tham gia tuân thủ [Quy tắc ứng xử](CODE_OF_CONDUCT.md).

## Giấy phép

Bằng cách đóng góp, bạn đồng ý code của bạn được cấp phép theo cùng giấy phép với dự án (GPL-3.0-or-later).
