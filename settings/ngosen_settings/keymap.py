# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
# SPDX-FileCopyrightText: 2026 Ngó Sen contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""The custom keymap: which key does which Vietnamese edit, with the presets of the fcitx5 edition."""

from .i18n import N_

# The codes the engine reads, and their labels; the arrows and letters need no translation.
ACTIONS = [
    ("XoaDauThanh", N_("Remove the tone mark")),
    ("DauSac", N_("Sắc tone")),
    ("DauHuyen", N_("Huyền tone")),
    ("DauHoi", N_("Hỏi tone")),
    ("DauNga", N_("Ngã tone")),
    ("DauNang", N_("Nặng tone")),
    ("A_Â", "a -> â"),
    ("E_Ê", "e -> ê"),
    ("O_Ô", "o -> ô"),
    ("AEO_ÂÊÔ", "a/e/o -> â/ê/ô"),
    ("UOA_ƯƠĂ", "u/o/a -> ư/ơ/ă"),
    ("D_Đ", "d -> đ"),
    ("UO_ƯƠ", "u/o -> ư/ơ"),
    ("A_Ă", "a -> ă"),
    ("__ă", "ă"),
    ("_Ă", "Ă"),
    ("__â", "â"),
    ("_Â", "Â"),
    ("__ê", "ê"),
    ("_Ê", "Ê"),
    ("__ô", "ô"),
    ("_Ô", "Ô"),
    ("__ư", "ư"),
    ("_Ư", "Ư"),
    ("__ơ", "ơ"),
    ("_Ơ", "Ơ"),
    ("__đ", "đ"),
    ("_Đ", "Đ"),
    ("UOA_ƯƠĂ__Ư", "u/o/a -> ư/ơ/ă, ư"),
]
ACTION_CODES = [code for code, _label in ACTIONS]

TELEX = [
    ("z", "XoaDauThanh"),
    ("s", "DauSac"),
    ("f", "DauHuyen"),
    ("r", "DauHoi"),
    ("x", "DauNga"),
    ("j", "DauNang"),
    ("a", "A_Â"),
    ("e", "E_Ê"),
    ("o", "O_Ô"),
    ("w", "UOA_ƯƠĂ"),
    ("d", "D_Đ"),
]
VNI = [
    ("0", "XoaDauThanh"),
    ("1", "DauSac"),
    ("2", "DauHuyen"),
    ("3", "DauHoi"),
    ("4", "DauNga"),
    ("5", "DauNang"),
    ("6", "AEO_ÂÊÔ"),
    ("7", "UO_ƯƠ"),
    ("8", "A_Ă"),
    ("9", "D_Đ"),
]
VIQR_MARKS = [
    ("'", "DauSac"),
    ("`", "DauHuyen"),
    ("?", "DauHoi"),
    ("~", "DauNga"),
    (".", "DauNang"),
    ("^", "AEO_ÂÊÔ"),
    ("+", "UO_ƯƠ"),
    ("*", "UO_ƯƠ"),
    ("(", "A_Ă"),
]

# As listed in the fcitx5 edition, so a preset gives the same keys in both.
PRESETS = [
    ("Telex", TELEX),
    ("VNI", VNI),
    ("VIQR", [("0", "XoaDauThanh")] + VIQR_MARKS + [("d", "D_Đ")]),
    (
        N_("Microsoft layout"),
        [
            ("8", "DauSac"),
            ("5", "DauHuyen"),
            ("6", "DauHoi"),
            ("7", "DauNga"),
            ("9", "DauNang"),
            ("1", "__ă"),
            ("!", "_Ă"),
            ("2", "__â"),
            ("@", "_Â"),
            ("3", "__ê"),
            ("#", "_Ê"),
            ("4", "__ô"),
            ("$", "_Ô"),
            ("0", "__đ"),
            (")", "_Đ"),
        ],
    ),
    ("Telex + VNI", TELEX + VNI),
    # fcitx5 lists a doubled backslash; the engine reads only its first character.
    ("Telex + VNI + VIQR", TELEX + VNI + VIQR_MARKS + [("\\", "D_Đ")]),
    (
        N_("VNI on a French keyboard"),
        [
            ("&", "XoaDauThanh"),
            ("é", "DauSac"),
            ('"', "DauHuyen"),
            ("'", "DauHoi"),
            ("(", "DauNga"),
            ("-", "DauNang"),
            ("è", "AEO_ÂÊÔ"),
            ("_", "UO_ƯƠ"),
            ("ç", "A_Ă"),
            ("à", "D_Đ"),
        ],
    ),
    ("Telex W", TELEX[:-2] + [("w", "UOA_ƯƠĂ__Ư"), ("d", "D_Đ")]),
]


def valid_key(key):
    """One character that is not a space: the engine reads only the first character of a key."""
    return len(key) == 1 and not key.isspace()


def action_label(code):
    """The label of an action, or the code itself when it is not one the window knows."""
    return dict(ACTIONS).get(code, code)
