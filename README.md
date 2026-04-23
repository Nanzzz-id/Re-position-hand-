# LeviViewModel Plugin for Minecraft Bedrock

Plugin ini dirancang untuk **LeviLauncher** (Android) guna mengubah posisi tangan (ViewModel) secara real-time menggunakan antarmuka **Dear ImGui**.

## Fitur
- **Real-time Adjustment**: Ubah posisi X, Y, dan Z tangan melalui slider.
- **Friendly GUI**: Antarmuka menu yang mudah digunakan di perangkat mobile.
- **Auto-Compile**: Siap dikompilasi menggunakan GitHub Actions.

## Cara Menggunakan
1. **Upload ke GitHub**: Buat repositori baru di GitHub dan upload semua file dalam folder ini.
2. **Kompilasi**: GitHub Actions akan otomatis mendeteksi file `.yml` dan melakukan build. Tunggu hingga proses selesai di tab "Actions".
3. **Download**: Ambil file `libLeviViewModel.so` dari hasil build (Artifacts).
4. **Pasang**: Masukkan file `.so` tersebut ke folder plugin LeviLauncher di perangkat Android Anda.

## Catatan Teknis
- Kode ini menggunakan **Dobby** untuk melakukan hooking pada fungsi internal Minecraft.
- Alamat memori (offsets) mungkin perlu disesuaikan tergantung pada versi Minecraft yang Anda gunakan.
- Pastikan Anda memiliki library ImGui dan Dobby di direktori yang sesuai jika melakukan kompilasi manual.

## Struktur Proyek
- `jni/main.cpp`: Logika utama plugin dan GUI.
- `jni/CMakeLists.txt`: Konfigurasi build Android NDK.
- `.github/workflows/main.yml`: Automasi build untuk GitHub.
