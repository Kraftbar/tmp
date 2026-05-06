# Wi-Fi Scan Tools

`iwgrep` filters `iw dev <iface> scan` output by whole `BSS ...` blocks instead of individual lines.
`iwtable` talks to the kernel over `nl80211` directly and prints a compact table of SSIDs, BSSIDs, channel, bandwidth, Wi-Fi standard, signal age, frequency, signal, and security.

## Build

```sh
make
make install
```

## Usage

```sh
iw dev wlan0 scan | ./iwgrep "60:b9"
iw dev wlan0 scan | ./iwgrep "MySSID"
iw dev wlan0 scan | ./iwgrep -i "guest"
sudo ./iwtable -i wlan0
sudo ./iwtable
```

To install it somewhere on your `PATH`:

```sh
sudo make install
```

Then use:

```sh
iw dev wlan0 scan | iwgrep "60:b9"
sudo iwtable -i wlan0
```

If you want to auto-pick the first wireless interface:

```sh
iw dev "$(iw dev | awk '$1 == \"Interface\" { print $2; exit }')" scan | iwgrep "60:b9"
sudo iwtable
```
