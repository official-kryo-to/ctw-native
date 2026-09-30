// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// A block of test bytes at the fixed addresses the tables are read from (see parity.sh).
.section .blob,"a"
.globl ctw_blob
ctw_blob: .incbin "blob.bin"
