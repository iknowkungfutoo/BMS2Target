# What Is BMS2TARGET?

With the recent release of the new Thrustmaster Viper Mission Pack and Viper Panel, many users have shared their dismay that the landing gear and other indicators are not working/integrated with BMS. This isn’t a fault of the product or Thrustmaster as there are far too many applications of which each has its unique style of exposing simulation data. It isn’t fair to expect Thrustmaster to support all of those applications and this is where the community fills the gap.

Along with BMS2TARGET, you will also need [TMHotasLEDSync](https://github.com/iknowkungfutoo/TMHotasLEDSync). Together, they enable the LEDs on the Viper Mission Pack and Viper Panel to relay the indicators in the cockpits of BMS. There is a caveat though as I will explain below.

For the Viper Mission Pack and Viper Panel, there are LEDs that can be used to relay the indicators of the F-16 landing gear, the landing gear handle and the threat warning auxiliary panel. It also has two columns of five user-programmable LEDs. However, the LEDs in the threat warning auxiliary switches do not fully mimic those of the real aircraft. Specifically, the “altitude” switch can either be illuminated red or green on the Viper Mission Pack / Panel as opposed to “LOW” in amber and “ALT” in green. Also, the ACT/PWR switch can only be illuminated fully as opposed to individually for “S” and “POWER”. Therefore we have to accept some compromises in the way in which the indicators of the F-16 can be shown on the Viper Mission Pack / Panel.

For now, BMS2Target only supports the F-16. In the future, I may expand it to the Warthog for the F-18 and F-15.

# How It Works:

BMS2Target.exe is a console application for 64-bit Windows. I have not built a 32-bit version and I would expect 99.99% of users will not be on Windows XP!

BMS2Target.exe reads data from the Falcon BMS shared memory and sends the relevant lamp data to the Thrustmaster TARGET software that is running the TMHotasLEDSync.tmc script. The data is sent via TCP and only if the data changes. The TARGET script handles each packet through an event. Thus it is fairly efficient and should introduce any significant load on your CPU.

BMS2Target.exe reads the Falcon shared memory every 100ms (that’s ten times a second). It’s not too taxing on the system yet fast enough so that we humans shouldn’t notice any lag.

The TARGET script does not configure your ViperTQS for use with BMS. It merely controls the LEDs of the ViperTQS. If you are using a TARGET script to map your device to BMS, I advise you to use the Alternative Launcher instead. However, if you wish to use a target script to map the ViperTQS to BMS, well, you’ll have to try to figure out how to combine this script with yours. Please don’t ask me to help with combining scripts, you’re going to have to figure that out on your own.

# Installation:

Download the bms2target.exe file from the releases section put it anywhere on your PC.

Download and unzip the [TMHotasLEDSync.zip](https://github.com/iknowkungfutoo/TMHotasLEDSync) file to a folder of your choosing.

# How To Use:

1. Run the bms2target.exe by double clicking on it.
2. Start the Thrustmaster T.A.R.G.E.T. script editor.
3. Open the TMHotasLEDSync.tmc script from the folder where you extracted TMHotasLEDSync.zip.
4. Start the script.
4. Start BMS.

# Tested Aircraft:

F-16 CM Blk 40
F-16 CM Blk 50
F-16 CM Blk 52
F-16 DM Blk 52

Other variants of the F-16 may or may not function as expected. Let me know of any problems and I will endeavour to correct them.

# LED Columns

The left user LED column is used to indicate the speed brake position.
The right user LED column is configured as follows from top to bottom:

1. JFS Run
2. Main Gen
3. Stby Gen
4. FLCS Rly
5. EPU Run

# Suggestions And Feature Requests:

Feel free to make any suggestions for improvements here [discussions](https://github.com/iknowkungfutoo/BMS2Target/discussions)

# Need Help?

In the first instance, ensure you have installed the v3.0.23.1003 or later of the Thrustmaster TARGET software. Most problems are due to people running old versions of the Thrustmaster TARGET software.
If that does not solve your problem, raise an issue on the [bms2target BMS thread](https://forum.falcon-bms.com/topic/26193/bms2target-bms-to-thrustmaster-hotas-led-controller-viper-mission-pack-and-viper-panel) or [here](https://github.com/iknowkungfutoo/BMS2Target/issues).

Be sure to include the output of the bms2target.exe console window and TARGET script editor console output in your message (you can select, copy and paste directly from the TARGET console output using your mouse).

If the bms2target.exe closes before you can copy the output, you can redirect the output to a log file using the following command. You will need to run it from a Windows PowerShell command prompt window where your bms2target.exe resides. You can do this by navigating to where your bms2target.exe is in Windows File Explorer, then right click in an empty area of the right hand pane and select "Open in Terminal".

    powershell ".\BMS2Target.exe | tee bms2target.log"


