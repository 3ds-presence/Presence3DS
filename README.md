# Presence3DS

> [!IMPORTANT]  
> This project is a fork of [Luma3DS](https://github.com/LumaTeam/Luma3DS) updated to include Presence3DS features. This README will only cover the Presence3DS specific features. For more information about the original Luma3DS project, please refer to the [original README](https://github.com/LumaTeam/Luma3DS#readme).

*Luma 3DS with Discord RPC Integration*

## Description
This fork of Luma3DS adds a way to communicate the current game being played on the 3DS to a 3DS Presence server, which can then be used to display the current game being played on Discord.
The 3DS sends to the 3DS Presence server the following information:
- Game Title ID
- Game Title Name
- Game Publisher

Optionally: 
- Main Mii information (the one shown in Mii Plaza or Friends List)
- Extra information about the game

## Features
This fork adds a new menu to the Luma3DS Rosalina menu (default hotkey to access this menu is <kbd>L+Down+Select</kbd>).

The new menu can be accessed by going into the new "Discord RPC..." entry.
In this menu, you can check the status of the connection to the 3DS Presence server, start and stop the connection, and configure preference settings.

When the connection is started, if you go into sleep mode, the connection will be automatically stopped, and restarted when you wake up the 3DS.

There is also a log menu, useful if there is a problem with the connection to the 3DS Presence server, or if you want to check what information is being sent to the server.

You can connect to the official 3DS Presence server, or to your own server if you want to host your own.

> [!CAUTION]
> Using a non-official 3DS Presence server may be a security risk, as the owner of the server can send anything as Discord Rich Presence information. When using a non-official server, make sure you trust the owner of the server. A warning will be displayed when connecting to a non-official server in the 3DS Presence menu.

### Current settings:
- `Hide Mii in Presence`: If enabled, the Mii information will not be sent to the 3DS Presence server.
- `Hide Home activity`: If enabled, the home menu activity will not be sent to the 3DS Presence server (no presence will be shown on your Discord status when on the home menu).
- `Auto-Start at boot`: If enabled, the connection to the 3DS Presence server will be automatically started when the 3DS is booted.

## Extra information about the game
By default, Presence3DS will only send the game title ID and name to the 3DS Presence server. However, it is possible to add extra information about the current state of the game being played (like level, score, etc).

To provide this extra information, you need a `<TITLEID>.txt` file for each game you want to add extra information for, and place it in the `presence3ds/rpc` folder of your SD card. 

You can find all the available scripts, and a guide to create your own, on the [Add-ons repository](https://github.3ds-presence/RPC-AddOns).

## Installation and upgrade
Presence3DS requires [boot9strap](https://github.com/SciresM/boot9strap) to run.

Please refer to the [3DS-Presence website](http://3ds-presence.top/) to get the latest version of Presence3DS and your personal config file, needed to connect to the 3DS Presence server, then follow the instructions to install it on your 3DS.

## Credits

This project could not have been possible without the work of the Luma3DS team, and the many contributors to the 3DS homebrew scene.
Please refer to the original [Luma3DS README](https://github.com/LumaTeam/Luma3DS#credits) for a full list of credits.

This fork was created by LeonLeBreton.