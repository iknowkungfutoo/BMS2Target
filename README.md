# What Is BMS2TARGET?

Out of the box, the Thrustmaster Viper Mission Pack and Viper Panel landing gear and other indicators are not functional and are not integrated with DCS or Falcon BMS. This isn't a fault of the product or Thrustmaster as there are far too many applications, each with their unique style of exposing simulation data. It isn't fair to expect Thrustmaster to support all of those applications and this is where the community fills the gap.


Along with BMS2TARGET, you will also need [TMHotasLEDSync](https://github.com/iknowkungfutoo/TMHotasLEDSync). Together, they enable the LEDs on the Viper Mission Pack and Viper Panel to relay the indicators in the BMS cockpits. There is a caveat, though, as I will explain below.

For the Viper Mission Pack and Viper Panel, some LEDs can be used to relay the indicators of the F-16 landing gear, the landing gear handle and the threat warning auxiliary panel. It also has two columns of five user-programmable LEDs. However, the LEDs in the threat warning auxiliary switches do not fully mimic those of the real aircraft. Specifically, the “altitude” switch can either be illuminated red or green on the Viper Mission Pack / Panel as opposed to “LOW” in amber and “ALT” in green. Also, the ACT/PWR switch has only one physical LED for what are two separate real-aircraft states, so it is lit solid for POWER and flashes to indicate activity, rather than showing both independently. Therefore, we have to accept some compromises regarding how the indicators of the F-16 can be shown on the Viper Mission Pack / Panel.

For now, BMS2Target only supports the F-16. In the future, I may expand it to the Warthog for the F-18 and F-15.

# How It Works:

BMS2Target.exe is a 64-bit Windows application that runs quietly in the system tray - no console window, no taskbar entry. I have not built a 32-bit version and expect 99.99% of users will not be on Windows XP!

BMS2Target.exe reads data from the Falcon BMS shared memory and sends the relevant lamp data to the Thrustmaster TARGET software running the TMHotasLEDSync.tmc script. The data is sent via TCP and only if the data changes. The TARGET script handles each packet through an event. Thus, it is reasonably efficient and should introduce any significant load on your CPU.

BMS2Target.exe reads the Falcon shared memory every 100ms (that’s ten times a second). It’s not too taxing on the system yet fast enough so that we humans shouldn’t notice any lag.

If the TARGET script is restarted or its connection drops, BMS2Target reconnects on its own once it's back - no need to restart BMS2Target. If Falcon BMS itself closes or crashes, BMS2Target notices and resets the LEDs so they don't get stuck showing stale state.

BMS2Target only holds a connection to the TARGET script while Falcon BMS is actually running - it never connects to TARGET without BMS present, and disconnects as soon as BMS closes. This means you can quit BMS and start DCS (with dcs2target) instead without needing to restart TMHotasLEDSync or BMS2Target: TARGET's connection is left free for dcs2target to take over. It also sends a small heartbeat signal roughly once a second so TMHotasLEDSync can tell the difference between "nothing has changed" and "BMS2Target has stopped responding" - if BMS2Target vanishes without a clean disconnect (a crash, for example), TMHotasLEDSync notices within a few seconds and turns the LEDs off on its own.

The TARGET script does not configure your ViperTQS for use with BMS. It merely controls the LEDs of the ViperTQS. If you use a TARGET script to map your device to BMS, I suggest using the Alternative Launcher instead. However, if you wish to use a target script to map the ViperTQS to BMS, you’ll have to try to figure out how to combine this script with yours. Please don’t ask me to help combine scripts; you’ll have to figure that out yourself.

# Installation:

1. Download `BMS2Target-Setup.msi` from the releases section and run it. It installs to Program Files with a Start Menu shortcut, and uninstalls cleanly from "Apps & features".
2. Install [TMHotasLEDSync](https://github.com/iknowkungfutoo/TMHotasLEDSync) using its MSI installer. This creates a "Thrustmaster HOTAS LED Sync" shortcut in a "Slughead Products" Start Menu folder that automatically starts the Thrustmaster T.A.R.G.E.T. software with the TMHotasLEDSync.tmc script loaded and running.

# How To Use:

1. Run BMS2Target via its Start Menu shortcut. It has no window - look for its icon in the system tray (you may need to click the "show hidden icons" arrow next to the clock).
2. Run the "Thrustmaster HOTAS LED Sync" shortcut created by its installer (Start Menu > Slughead Products, or the Desktop if you chose that option).
3. Start BMS.

Hovering over the tray icon shows the current connection state, and it pops up a notification when it connects to TARGET/BMS and when a flight starts or ends. Right-click the icon for an About option, to have BMS2Target launch automatically the next time you log into Windows ("Start with Windows"), and to exit.

# Tested Aircraft:

F-16 CM Blk 40
F-16 CM Blk 50
F-16 CM Blk 52
F-16 DM Blk 52

Other variants of the F-16 may or may not function as expected. If you encounter any problems, please let me know, and I will endeavour to correct them.

# LED Columns

Both user LED columns are numbered from the bottom (LED 1) to the top (LED 5) on the hardware.

The left column indicates speed brake position (LED 5 = brake ≥ 100% down to LED 1 = brake ≥ 20%).

The right column is configured as follows:

| LED | Function |
|---|---|
| LED 5 (top) | JFS Run |
| LED 4 | Main Gen |
| LED 3 | Stby Gen |
| LED 2 | FLCS Rly |
| LED 1 (bottom) | EPU Run |

# Suggestions And Feature Requests:

Feel free to make any suggestions for improvements here [discussions](https://github.com/iknowkungfutoo/BMS2Target/discussions)

# Need Help?

Raise an issue on the [bms2target BMS thread](https://forum.falcon-bms.com/topic/26193/bms2target-bms-to-thrustmaster-hotas-led-controller-viper-mission-pack-and-viper-panel) or [here](https://github.com/iknowkungfutoo/BMS2Target/issues).

Be sure to include a log file and the TARGET script editor console output in your message (you can select all using CTRL-A, copy using CTRL-C and paste with CTRL-V directly from the TARGET console output using your mouse).

BMS2Target doesn't log anything by default. To get a log file, right-click the tray icon and tick "Enable Log File" *before* reproducing the problem. This creates `bms2target.log` in your Downloads folder and records lamp/connection events. Unticking it (or exiting the app) stops logging; ticking it again starts a fresh log for that session.

