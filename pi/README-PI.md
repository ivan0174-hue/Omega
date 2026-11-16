# Omega bare metal per Raspberry Pi 4 / Pi 400

Questa cartella contiene il backend che fa girare Omega direttamente sul
Raspberry Pi, senza Linux, usando la libreria [Circle](https://github.com/rsta2/circle).

| File | Cosa fa |
|---|---|
| `pimain.cpp`, `kernel.cpp/.h` | avvio: schermo, timer, interrupt, USB, scheda SD, FAT |
| `HostCircle.cpp` | le funzioni di `Host.h`: video, tastiera/mouse USB, file, velocità |
| `libc.cpp`, `libc/` | le poche funzioni C che il core usa (printf, qsort...) |
| `omega_main.c` | compila `../main.c` rinominando `main` |
| `config.txt`, `LEGGIMI-SD.txt` | file per la scheda SD |
| `circle-Config.mk` | configurazione con cui compilare Circle |

Il core dell'emulatore (`../*.c`) è lo stesso della versione desktop: nessuna
modifica specifica per il Pi.

## Come funziona

- Video: l'immagine Amiga 640×200 viene raddoppiata in verticale e copiata in
  un framebuffer 640×480 a 32 bit; la GPU la scala alla risoluzione HDMI.
- Velocità: un fotogramma ogni 16,683 ms (59,94 Hz, timing NTSC come la
  versione desktop), misurato con il timer di sistema a 1 MHz.
- Input: tastiera USB (codici HID → codici tasto Amiga) e mouse USB.
- File: FatFs (nomi lunghi, cartelle). `KICK.ROM` (o il primo `.rom` da
  256K/512K) nella radice.
- Dischi: all'accensione i drive sono vuoti, come su un Amiga vero. F1–F4
  aprono un elenco dei file `.adf` sulla scheda (emulazione in pausa); il
  disco scelto viene caricato con `floppyLoadImage()` e inserito. Cambiando
  disco, il vecchio viene tolto e il nuovo inserito dopo 3 secondi.
- Il LED verde (ACT) segnala il motore del floppy.

## Compilare

Serve un compilatore per AArch64: la toolchain GNU consigliata da Circle
(`aarch64-none-elf-`) oppure clang/LLVM ≥ 15 (usato per questa build).

```sh
git clone https://github.com/rsta2/circle.git
cp circle-Config.mk circle/Config.mk          # AARCH=64, RASPPI=4, DEPTH=32, CLANG=1
#   (con la toolchain GNU togli la riga CLANG = 1)
cd circle
for d in lib lib/usb lib/input lib/fs addon/SDCard; do make -C $d; done
# FatFs (long file names) needs our libc headers when Circle is built without stdlib:
make -C addon/fatfs EXTRAINCLUDE="-I /percorso/di/Omega/pi/libc -isystem $(clang -print-resource-dir)/include"
cd boot && make armstub64 && cd ..           # armstub8-rpi4.bin
cd /percorso/di/Omega/pi
make CIRCLEHOME=/percorso/di/circle           # -> kernel8-rpi4.img
```

`DEPTH=32` (framebuffer a 32 bit) e `KERNEL_MAX_SIZE=0x3000000` (il core usa
circa 27 MB di dati statici: 16 MB di spazio indirizzi 68000 più i floppy)
devono essere impostati **prima** di compilare Circle.

Per la scheda SD servono anche i file firmware `start4.elf`, `fixup4.dat`,
`bcm2711-rpi-4-b.dtb` (repository `raspberrypi/firmware`, cartella `boot`).

Per il debug: decommenta `DEFINE += -DOMEGA_SERIAL_LOG` nel Makefile e collega
un adattatore seriale 3,3 V ai GPIO 14/15 (115200 8N1). Rallenta l'emulazione.

## Pi 3

Il codice non dipende dal Pi 4: con `RASPPI = 3` in `Config.mk` (Circle
ricompilato) si ottiene `kernel8.img` per Pi 3; in quel caso servono
`bootcode.bin`, `start.elf`, `fixup.dat` e il `.dtb` del Pi 3.

## Audio

Paula è emulato (4 canali, DMA e scrittura diretta, interrupt, attach,
filtri dell'A500). Uscita a 48 kHz su HDMI0 (`CHDMISoundBaseDevice`) o sul
jack cuffie (`CPWMSoundBaseDevice`), scelta in F5 e salvata in OMEGA.CFG
(`audio=hdmi|jack|off`, `stereo=100|70|50|0`). La frequenza di uscita
viene corretta di ±0,5 % al massimo per seguire la coda del dispositivo
(~60 ms), così audio e video non si separano mai. `config.txt` contiene
`hdmi_drive=2` (modalità HDMI con audio invece di DVI). Serve
`lib/sound/libsound.a` di Circle.

## Joystick

Gamepad USB tramite i driver di Circle (`upad1` → porta 2, `upad2` → porta 1
se scelto); per i gamepad riconosciuti si usa la mappatura nota, per quelli
generici pulsante 1 = fuoco, 2 = secondo pulsante, assi 0/1 e hat. Joystick da
tastiera con tasti configurabili (pagina dal menu F5). Chiavi in OMEGA.CFG:
`joykb`, `joykeys` (codici HID, modificatori 224-231), `padb`, `padstart`,
`port1=mouse|pad`.

## Floppy vero (DrawBridge)

`DrawBridge.c` parla con il firmware 1.x dell'[Arduino Floppy Disk
Reader](https://github.com/RobSmithDev/ArduinoFloppyDiskReader) di Rob Smith
(comandi `xR?`, `+`, `-`, `#NN`, `[`/`]`, `<`, `^`; 2 Mbaud). Sul Pi la porta
è un `CUSBSerialDevice` di Circle (`utty1`..`utty4`, driver FTDI). Quando
l'Amiga legge una traccia non ancora letta, `Floppy.c` chiama il lettore
(bloccante, ~0,25 s): flusso MFM grezzo di ~1,1 giri → lunghezza di un giro
(dall'intestazione di un settore ripetuta) → giunzione spostata nello spazio
tra i settori → sync allineati a 16 bit come fa Paula (preambolo `$AAAA`
conservato, Kickstart 1.3 lo controlla) → se è una traccia AmigaDOS con
settori rovinati, fino a 8 letture e unione dei settori buoni. Sola lettura
(disco protetto da scrittura). Opzione in F5 (`drawbridge=off|df0|df1`).

Prova senza hardware (versione headless):

```sh
OMEGA_DRAWBRIDGE_SIM=disco.adf OMEGA_LIVE_AT=700 OMEGA_SHOT_AT=3000 \
OMEGA_SHOT_FILE=wb.ppm ./omega-headless kick13.rom
```

simula l'Arduino con quel disco nel drive (protocollo vero, indice in un
punto a caso, `OMEGA_DRAWBRIDGE_JITTER=N` perde un bit ogni N);
`OMEGA_FLOPPY_DEBUG=1` mostra ogni traccia letta.

## Limiti attuali

- Le immagini compresse (.adz, .dms, .zip) non sono supportate.
- Copia del fotogramma senza doppio buffer: può comparire un leggero tearing.
- Non testato su hardware reale al momento della stesura (compilato e linkato,
  ma senza un Pi a disposizione).
