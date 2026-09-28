# IVnet-L
Functionality for hosting a local Nintendo DS Wi-Fi server, with the primary focus on the Generation IV Pokemon games.

## Prerequisites

### IVnet
Ensure you have IVnet built with the ENABLE_LOCALHOST environmental variable set; run:
```
# In the IVnet folder...
ENABLE_LOCALHOST=1 ./ivnetMake
```

Optionally, if you wish to use the Pokemon Classic Network proxy functionality, rather than what IVnet offers locally, run:
```
# In the IVnet folder...
ENABLE_LOCALHOST=1 ENABLE_PROXY_DEBUG=1 ./ivnetMake
```

### HTTPS Certificates
In order for IVnet-L to even function, you must have a valid Nintendo-signed SSL certificate and key from the Wii/Nintendo DS era.

Fortunately, you can get these using the following method:

**Requirements**
- Any Wii
- [nanddumper@ios](https://oscwii.org/library/app/nanddumper_ios)
- [Dolphin Emulator](https://dolphin-emu.org/)
- openssl (should have been installed in the 'Requirements' section of IVnet)
  
**Steps**
1) Ensure your Wii is hacked beforehand, with the Homebrew Channel and Wii Mod Lite installed. Also ensure you have an SD card for your Wii with min. ~8GB storage. For help with this, click [here](https://wii.hacks.guide/).
2) Download nanddumper@ios and place the `nanddumper_ios` folder in the `apps` folder of your SD card. Boot up your Wii with the SD card inside, navigate to the Homebrew Channel/Wii Mod Lite, and run nanddumper@ios from there. (If you are already in possession of a Wii nand dump file, skip to step 4)
3) Follow the instructions to create a binary dump of the NAND memory. This should create a file called `nand.bin` in `/wii/backups/` on your SD card.
4) With your NAND dump on your PC now, run the Dolphin Emulator, and select 
   - `Tools -> Manage NAND -> Import BootMii NAND backup...`
   - `Tools -> Manage NAND -> Extract Certificates from NAND`
5) Enter your terminal and run:
```
# If Dolphin Emulator was installed using Flatpak
cd ~/.var/app/org.DolphinEmu.dolphin-emu/data/dolphin-emu/Wii/
# Else if installed with your package manager
cd ~/.local/share/dolphin-emu/Wii
# Else if built from source
cd dolphin-emu/Wii
```
1) In this folder, you should find two files: `clientca.pem` and `clientcakey.pem`. These are your SSL certificate and key files. If they are in a binary format (checked by running the `file` command on them, or by opening them in a text editor), you can convert them into a readable format by running:
```
openssl x509 -inform DER -in clientca.pem    -outform PEM -out nwc.crt
openssl rsa  -inform DER -in clientcakey.pem -outform PEM -out nwc.key
```
1) If the files were in a readable format to begin with, rename `clientca.pem` to `nwc.crt` and/or `clientcakey.pem` to `nwc.key`.
2) You should now have two files, `nwc.crt` and `nwc.key`. Ensure they are in the same folder of your choosing when referencing them to IVnet.

### Legal notice
It is illegal to distribute these files (`clientca.pem`, `clientcakey.pem`, `nwc.crt`, `nwc.key`, `nand.bin`) as they contain sensitive information about Nintendo and are subject to copyright law and distribution. IVnet and its maintainer(s) are therefore not held accountable for any distributions of these files, and we will, of course, not distribute any of these files ourselves.  

For further reading, click [here](https://github.com/KaeruTeam/nds-constraint).

## IVnet-L set up

### Frontend
To use IVnet-L with the frontend, perform the following:

1) Run IVnet with the `ivnetRun` script, or by running `bin/ivnet`.
2) Go to the `Config` menu and press the `Cert. Path` button.
3) This should pull up a file explorer/terminal. Navigate to the folder where you have your `nwc.crt` and `nwc.key` files saved, and select it.
    - Furthermore, if you are running with ENABLE_PROXY_DEBUG, press the `Proxy File Path` button to select the output log file of the proxy. Here, the HTTP/HTTPS/TCP communication between the DS and the Pokemon Classic Network will occur.
4) Back on the main menu, select `Start -> Your NID name -> '0.0.0.0 - Localhost'`. If all goes well, you should now have IVnet-L running!

### Backend
To use IVnet-L with the backend, run the following command in the IVnet folder:
```
sudo bin/ivnetback --nid {YOUR NID NAME} --dns 0.0.0.0 --ccode {YOUR COUNTRY CODE} --ssid {YOUR CHOSEN SSID NAME} --cert {PATH TO YOUR SSL CERTIFICATE AND KEY FOLDER} --proxy {PATH TO YOUR PROXY OUTPUT FILE (if running with ENABLE_PROXY_DEBUG, optional)}
```

## Features of IVnet-L

### Mystery Gift distribution

You can send Mystery Gift files (`.myg` files) to your games.

To do this properly, ensure you have configured your Mystery Gift folder path:
- **Frontend** - `Config -> Myst. Gift Path -> select your Mystery Gift folder path`.
- **Backend** - run `sudo bin/ivnetback` as above with the `--myg {MYSTERY GIFT FOLDER PATH}` option set.


## More information
### Background

IVnet-L began from my curiosity to somehow distribute Mystery Gifts from my computer onto my games. It is still quite a shock to me that it actually works, and that AMEMONE the Shroomish now resides on my physical cartridges.

Sure, there are things like ActionReplay and PKHeX to put hacked Pokemon onto these old games, but much like the original vision of IVnet, I did not have these things, so I made my own way.

IVnet-L now acts as an open-source conservation project for the Wi-Fi services of the Generation IV Pokemon games. My wish is that the list of features it preserves grows on without end!