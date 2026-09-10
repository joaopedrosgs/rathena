// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder
//
// ragnadot dev overlay. See the ragnadot client repo: docs/server-setup.md.
// This file and conf/import/*_conf.txt are the only additions; the sole edit
// to upstream source is the packet-obfuscation line in src/config/packets.hpp.

#ifndef CONFIG_CUSTOM_DEFINES_PRE_HPP
#define CONFIG_CUSTOM_DEFINES_PRE_HPP

/**
 * Pin the packet version to match the Godot client (ragnadot).
 *
 * 20211103 is the default the dev server and the client agree on, and the
 * client mirror in net/protocol/packet_ver.gd MUST equal whatever is built
 * here. It is a default rather than a hard #define so a second build can
 * target a modern client without editing tracked source:
 *
 *     msbuild rAthena.sln /p:Configuration=Release /p:Platform=x64
 *             /p:DefineConstants=PACKETVER=20250716
 *
 * Every vcxproj already threads $(DefineConstants) into its preprocessor
 * definitions, so that override needs no project edit.
 * The client selects its matching codec at runtime with PacketVer.select.
 * Only ONE version is live per map-server build — rAthena resolves PACKETVER
 * at compile time; "support both" means both builds work, not both at once.
 */
#ifndef PACKETVER
#define PACKETVER 20211103
#endif

/**
 * Renewal mode is rAthena's default — nothing to set here.
 *
 * Packet obfuscation is disabled by editing src/config/packets.hpp (there is no
 * macro to switch it off: it is guarded by `#ifndef PACKET_OBFUSCATION`). We
 * deliberately do NOT set PACKET_OBFUSCATION_KEY1/2/3.
 */

#endif /* CONFIG_CUSTOM_DEFINES_PRE_HPP */
