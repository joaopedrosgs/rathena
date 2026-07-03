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
 * One version, exactly — never multi-version support.
 * The client mirror lives in net/protocol/packet_ver.gd and MUST equal this.
 */
#define PACKETVER 20211103

/**
 * Renewal mode is rAthena's default — nothing to set here.
 *
 * Packet obfuscation is disabled by editing src/config/packets.hpp (there is no
 * macro to switch it off: it is guarded by `#ifndef PACKET_OBFUSCATION`). We
 * deliberately do NOT set PACKET_OBFUSCATION_KEY1/2/3.
 */

#endif /* CONFIG_CUSTOM_DEFINES_PRE_HPP */
