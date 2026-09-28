# IVnet
Connect your generation IV Pokemon games to the Internet!

## Installation
### Requirements

Before you can compile IVnet, run:

- **Debian/Ubuntu/Mint**
  ```
  sudo apt install build-essential git make wget tar openssl libgl1-mesa-dev libx11-dev zenity iproute2 iw rfkill iptables hostapd dnsmasq
  ```
- **Arch**
  ```
  sudo pacman -S base-devel git make wget tar openssl zenity iproute2 iw rfkill iptables hostapd dnsmasq
  ```
- **Fedora**
  ```
  sudo dnf install @development-tools git wget tar openssl mesa-libGL-devel libX11-devel iproute iw rfkill iptables hostapd dnsmasq
  ```

### Build

**Installing [raylib](https://github.com/raysan5/raylib)**

IVnet uses raylib as its frontend, so ensure that it is installed on your system.

For building statically with make:
```
git clone https://github.com/raysan5/raylib.git
cd raylib/src
make PLATFORM=PLATFORM_DESKTOP
sudo make install
```
**Installing [cottage](https://github.com/vixthevix/cottage)**

IVnet uses cottage for IVnet-L.

To include it, run:
```
#In the IVnet folder...

cd src
git clone https://github.com/vixthevix/cottage.git

#cottage is composed of header files, so no further building here.
```

**Building IVnet**

Run `./ivnetMake` in the IVnet folder. Alternatively, run the script with your command line interpreter (e.g. `bash`, `zsh` etc.) or copy and run the `gcc` commands directly in your terminal. 

Run with the `-h` flag for build tips:
```
IVnet - Connect your Generation IV Pokemon games to the Internet!

=================================================================

To build normally, run './ivnetMake' or 'bash ivnetMake'

=================================================================

OPTIONAL ENV PARAMETERS
     ENABLE_LOCALHOST   - Builds with additional functionality for hosting a local server
     ENABLE_PROXY_DEBUG - Builds with additional functionality for acting as a proxy between
                          the DS and the Pokemon Classic Network (requires ENABLE_LOCALHOST)

For more info, visit https://github.com/vixthevix/IVnet
```

## Software guide

### Run

Run `./ivnetRun` or `bin/ivnet` in the IVnet folder.

### Setting up a session
Before you begin, ensure you have:
- An available Network Interface Device (NID) that is not your main WiFi device, such as a WiFi dongle.
- Configured your country code in the config menu (look for your code in 'More Information' below).

Once this is done:

1) On the main menu, click **START**.
2) Select your NID 
   (if using a dongle, refresh while unplugged 
   and while plugged to recognize it).
3) Select a DNS server to connect to.
4) Wait for the device to be set up, 
   and if no errors occur, you are good to go!
5) On your Pokemon D/P/Pt/HG/SS game, 
   go to your Nintendo WFC settings.
6) Go to Nintendo Wi-Fi Connection Settings -> 
   Connection 1/2/3 (whichever is available) -> 
   Search for an Access Point -> 
   IVnet -> wait for the connection to set up.
7) Once connected, click on Ready on 
   the chosen Connection, scroll down, 
   turn off Auto-obtain DNS, 
   and ensure the Primary DNS is set 
   to your chosen DNS server, 
   and Secondary DNS is all 0 or matching the Primary DNS.
8) Save Settings, then test the connection. 
   If all good, you now have access to 
   Gen IV Internet features, such as Mystery Gift and the GTS!

Please note that the DS's connection strength to the servers depends on your PC's own Internet connection strength.<br>
Additionally, some extra steps may be required to fully connect to the servers, depending on where you live. Look in 'More information' for this.

### Using the backend only

If you wish to use IVnet without the frontend, you can run the backend direcly by running `bin/ivnetback` in your terminal, while in the IVnet folder.

The backend takes 4 arguments:
- The name of the Network Interface Device to use as an Access Point.
  - You can check what your available NIDs are by running `sudo ls /sys/class/net/`
- The DNS server to connect to.
- Your country code (look for your code in 'More Information' below).
- A chosen SSID for the Access Point.
  - **Only by running the backend can you make a custom SSID**. This will be the name that shows up on the Nintendo WFC Access Point search results.

### IVnet-L

IVnet-L is the name for the localhost functionality of IVnet. Instead of connecting to an external server, it uses cottage to set up a HTTP and HTTPS server for communicating with the DS directly. <br>
For more information on setting up IVnet-L, click [here](./assets/pages/IVnet-L.md).

## Hardware guide
### External Network Interface Devices
I recommend using an external NID as the Access Point for IVnet, as it is less likely to pose a risk to your systems built-in network devices. <br>
In my testing and usage, I have been using an [**AR9271 USB WiFi Adapter**](https://www.amazon.co.uk/dp/B0BRG6587D?ref=ppx_yo2ov_dt_b_fed_asin_title). The Linux kernel supports it natively and I have had no issues with it.

It would be, again, greatly appreciated if information on how other external NIDs work with IVnet could be collected, to display here for all to see.<br>
From my own research, the device must be capable of the following:
- 2.4 GHz Wifi **only**.
- Standard 2.4GHz channels (1 through 11).
- IEEE 802.11b / 802.11g support.
- Access Point suport.
- nl80211 Linux interface support.
- Unencrypted network support.

### Built-in Network Interface Cards
If your system is using Ethernet as its main connection to the Internet, it should be safe to use a built-in NIC as the Access Point for IVnet. This is currently untested, and feedback on this would be greatly appreciated.<br>
The list above on required capabilities should also apply for a built-in NIC.

## More information
### Background
I started IVnet because, before the program was made, the only way you can connect the generation IV games to custom servers, feasably, was by using either an old router that only supported WEP WiFi encryption (which the DS supports, unlike the newer WPA protocols), or an unsecure mobile hotspot with something like an Android.

I had neither, and after hearing about and watching MattKC's work on [Vanilla](https://github.com/vanilla-wiiu/vanilla), I decided to learn how to use a USB WiFi adapter to connect to the Internet. I am proud to say that it works!

On the backend, the main programs running are [hostapd](https://wireless.docs.kernel.org/en/latest/en/users/documentation/hostapd.html), which deals with turning the NID into an Access Point, and [dnsmasq](https://wiki.archlinux.org/title/Dnsmasq), which turns the NID into a DHCP server, thus allowing it to assign IP addresses to connecting systems, like the Nintendo DS, and rerouting traffic to a specified DNS server. It also allows for a custom SSID to be used.

Due to this being an unsecure Access Point, **ensure the Access Point is up only for as long as needed**. A timer of 3 hours is set by default to time out the AP.

### ISO 3166-1 alpha-2 codes
IVnet requires that you configure your "country code" AKA your ISO 3166-1 alpha-2 national code. This is to ensure that the programs ran by IVnet (specifically hostapd) are compliant with your country's WiFi laws and regulations.

All of the country codes can be found [here](https://en.wikipedia.org/wiki/ISO_3166-1_alpha-2).

### IVgift
In tandem with the Mystery Gift transmission feature of IVnet-L, I have created a program for creating and editing Generation IV Mystery Gifts on the browser, [IVgift](https://github.com/vixthevix/IVgift).

I have tested IVnet witha  variety of Mystery Gifts made using IVgift, and I can confirm that (for the most part), they are accepted by the games. Go try it out!

### Extra steps
Depending on where you live in the world and what your country code may be, you may need to tweak your local WiFi settings to accommodate for this (if your main Internet connection is via Ethernet, this shouldn't be an issue, maybe).

For instance, as a UK resident, I had to manually change my router's DNS routing (to 8.8.8.8 for Google's services, for instance), due to UK rules on Internet providers changing DNS targeted packet destinations to their own servers; this would result in packets being sent to the official Nintendo servers, which are of course discontinued.<br>
Alternatively, I could connect my PC to my iPhone's hotspot to connect to the servers succesfully.

For more information on what to do for your specific circumstance, I recommend for now researching online on how to set your WiFi settings right for IVnet to work properly; with enough support, this information could also be available on this repository at some point in the future.

### Credits
Many thanks to
- The people behind [the Pokemon Classic Network](https://pkmnclassic.net/), [Wiimmfi](https://wiimmfi.de/), [Kaeru Team](https://kaeru.world/projects/wfc) and [AltWFC](https://github.com/barronwaffles/dwc_network_server_emulator), who pioneered these technologies and services.
- [openssl](), for maintaining their very old (and luckily for us, unsecure) libaries.
- My brother, who had to endure my yapping about this project and many others lmao.

### What's next?

- Work on and test ports to Linux Virtual Machines and WSL.
- Work on potential bugs that may crop up.
- Work on list of compatible Network Interface Devices.
- Improve the frontend visually, such as adding a background image and hyperlinks.
- Expanding the user-set configuration options, if needed. 

Furthermore, discussions are set up to contribute any information on both hardware and software compatibility and bugs, to improve IVnet further.

All in all, I hope you enjoy using IVnet!

-vixthevix, a Human from Earth.