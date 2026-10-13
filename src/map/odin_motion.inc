// Copyright (c) Odin contributors - GPL-3.0-or-later
// Included once by clif.cpp. Wire contract: ragnarok/docs/motion-protocol.md.
// This is an optional presentation protocol; all gameplay still uses unit.cpp.
namespace {
constexpr uint32 MOTION_PROBE_A = 0x52474d31, MOTION_PROBE_B = 0x314d4752;
// Feature bits: 1 the v1 layout, 2 relocation fence (0x0cf4), 4 change-driven recovery.
constexpr uint16 MOTION_FEATURES = 1 | 2 | 4;
constexpr uint32 MOTION_HEARTBEAT_TICKS = 5; // full rotating batch every 5 x 200 ms
static uint32 motion_epoch = 0;
static uint16 motion_late = 0;
static t_tick motion_late_at = 0;
static void motion_u16(std::vector<uint8>& b, uint16 v) { b.push_back(static_cast<uint8>(v)); b.push_back(static_cast<uint8>(v >> 8)); }
static void motion_u32(std::vector<uint8>& b, uint32 v) { motion_u16(b,v); motion_u16(b,v >> 16); }
static void motion_send(map_session_data* sd, const std::vector<uint8>& b) {
	if (!session_isActive(sd->fd)) return;
	WFIFOHEAD(sd->fd,b.size()); memcpy(WFIFOP(sd->fd,0),b.data(),b.size()); WFIFOSET(sd->fd,b.size());
}
static bool motion_probe(map_session_data* sd, uint32 value) {
	const t_tick now = gettick();
	if (value == MOTION_PROBE_A) { sd->motion.probe_at = now; return true; }
	if (value != MOTION_PROBE_B) return false;
	if (sd->motion.probe_at && DIFF_TICK(now,sd->motion.probe_at) >= 0 && DIFF_TICK(now,sd->motion.probe_at) < 2000) {
		sd->motion.enabled = false; sd->motion.epoch = ++motion_epoch;
		sd->motion.input = sd->motion.snapshot = sd->motion.cursor = 0;
		sd->motion.fence = sd->motion.beat = 0; sd->motion.sent.clear();
		std::vector<uint8> b; motion_u16(b,0xcf0); motion_u16(b,1);
		motion_u32(b,sd->motion.epoch); motion_u32(b,static_cast<uint32>(now)); motion_u16(b,MOTION_FEATURES); motion_u16(b,200);
		motion_send(sd,b);
	}
	sd->motion.probe_at = 0; return true;
}
static bool motion_visible(map_session_data* sd, block_list* bl) {
	// Official hide/disguise/visibility rules are a security boundary too.
	if (bl->id == sd->id) return true;
	if (bl->type == BL_PC && pc_isinvisible((map_session_data*)bl)) return false;
	status_change* sc = status_get_sc(bl);
	bool hidden = sc && (sc->option & (OPTION_HIDE | OPTION_CLOAK | OPTION_INVISIBLE | OPTION_CHASEWALK));
	if (!hidden) return true;
	if (battle_config.hide_cloaked_units & bl->type) return false;
	// Match clif_move -> clif_send_sub's ally/Intravision restriction.
	if (bl->type == BL_PC && population_engine_is_population_pc(bl->id)) return true;
	return battle_config.update_enemy_position || sd->special_state.intravision ||
		sd->sc.getSCE(SC_INTRAVISION) || battle_check_target(bl,sd,BCT_ENEMY) <= 0;
}
// What a row would tell this client, minus progress along an already-sent path.
static uint64 motion_signature(map_session_data* sd, block_list* bl) {
	unit_data* ud = unit_bl2ud(bl);
	bool walking = ud->walktimer != INVALID_TIMER && get_timer(ud->walktimer) && ud->walkpath.path_pos < ud->walkpath.path_len;
	uint64 s = ud->motion_generation;
	s = s * 1000003u ^ static_cast<uint16>(status_get_speed(bl));
	s = s * 1000003u ^ (walking ? 1u : 0u);
	s = s * 1000003u ^ (static_cast<uint32>(static_cast<uint16>(ud->to_x)) << 16 | static_cast<uint16>(ud->to_y));
	if (!walking) s = s * 1000003u ^ (static_cast<uint32>(static_cast<uint16>(bl->x)) << 16 | static_cast<uint16>(bl->y));
	if (bl->id == sd->id) s = s * 1000003u ^ sd->motion.input;
	return s;
}
static void motion_row(std::vector<uint8>& b, map_session_data* sd, block_list* bl, t_tick now, uint8 reason) {
	unit_data* ud = unit_bl2ud(bl);
	const TimerData* td = ud->walktimer != INVALID_TIMER ? get_timer(ud->walktimer) : nullptr;
	bool walking = td && ud->walkpath.path_pos < ud->walkpath.path_len;
	int16 x = bl->x, y = bl->y; uint8 sx = 8, sy = 8;
	if (walking) ud->getpos(x,y,sx,sy,now);
	motion_u32(b,bl->id); motion_u32(b,ud->motion_generation);
	motion_u32(b,bl->id == sd->id ? sd->motion.input : 0);
	motion_u16(b,bl->x); motion_u16(b,bl->y);
	motion_u16(b,x); motion_u16(b,y); b.push_back(sx); b.push_back(sy);
	motion_u16(b,ud->to_x); motion_u16(b,ud->to_y);
	uint16 speed = status_get_speed(bl); motion_u16(b,speed);
	b.push_back(walking ? 1 : 0); b.push_back(reason);
	uint8 count = walking ? ud->walkpath.path_len - ud->walkpath.path_pos : 0;
	b.push_back(count);
	x = bl->x; y = bl->y; t_tick deadline = td ? td->tick : now;
	for (int i = ud->walkpath.path_pos; walking && i < ud->walkpath.path_len; ++i) {
		int dir = ud->walkpath.path[i]; x += dirx[dir]; y += diry[dir];
		if (i != ud->walkpath.path_pos) deadline += (dirx[dir] && diry[dir]) ? speed * MOVE_DIAGONAL_COST / MOVE_COST : speed;
		motion_u16(b,x); motion_u16(b,y); motion_u32(b,static_cast<uint32>(deadline));
	}
}
static void motion_snapshot(map_session_data* sd, const std::vector<block_list*>& units, uint8 reason = 0) {
	if (!sd->motion.enabled || !sd->prev || !session_isActive(sd->fd)) return;
	t_tick now = gettick(); std::vector<uint8> b;
	motion_u16(b,0xcf3); motion_u16(b,0); motion_u32(b,sd->motion.epoch);
	motion_u32(b,++sd->motion.snapshot); motion_u32(b,static_cast<uint32>(now)); motion_u16(b,static_cast<uint16>(units.size()));
	motion_u16(b,DIFF_TICK(now,motion_late_at) < 1000 ? motion_late : 0);
	for (block_list* bl : units) { motion_row(b,sd,bl,now,reason); sd->motion.sent[bl->id] = motion_signature(sd,bl); }
	b[2] = static_cast<uint8>(b.size()); b[3] = static_cast<uint8>(b.size() >> 8); motion_send(sd,b);
}
static int32 motion_collect(block_list* bl, va_list ap) {
	auto sd = va_arg(ap,map_session_data*); auto units = va_arg(ap,std::vector<block_list*>*);
	if (bl->id != sd->id && unit_bl2ud(bl) && motion_visible(sd,bl)) units->push_back(bl);
	return 0;
}
static int32 motion_recover(map_session_data* sd, va_list) {
	if (!sd->motion.enabled || !sd->prev || !session_isActive(sd->fd)) return 0;
	std::vector<block_list*> nearby, rows;
	map_foreachinallarea(motion_collect,sd->m,sd->x-AREA_SIZE,sd->y-AREA_SIZE,sd->x+AREA_SIZE,sd->y+AREA_SIZE,BL_CHAR,sd,&nearby);
	// Forget units that left: a returning unit is new state again.
	std::unordered_map<int32, uint64> kept;
	auto keep = [&](block_list* bl) { auto it = sd->motion.sent.find(bl->id); if (it != sd->motion.sent.end()) kept.emplace(*it); };
	keep(sd); for (block_list* bl : nearby) keep(bl);
	sd->motion.sent.swap(kept);
	// TCP delivers every event row, so recovery sends only state the client has
	// not been told (a missed stop, a speed change, a unit new in view)...
	auto changed = [&](block_list* bl) { auto it = sd->motion.sent.find(bl->id); return it == sd->motion.sent.end() || it->second != motion_signature(sd,bl); };
	if (changed(sd)) rows.push_back(sd);
	for (block_list* bl : nearby) if (rows.size() < 16 && changed(bl)) rows.push_back(bl);
	// ...plus a slow rotating heartbeat that bounds any divergence it cannot see.
	if (++sd->motion.beat % MOTION_HEARTBEAT_TICKS == 0) {
		if (std::find(rows.begin(),rows.end(),sd) == rows.end() && rows.size() < 16) rows.insert(rows.begin(),sd);
		for (size_t i=0; i<nearby.size() && i<15 && rows.size() < 16; ++i) {
			block_list* bl = nearby[(sd->motion.cursor+i)%nearby.size()];
			if (std::find(rows.begin(),rows.end(),bl) == rows.end()) rows.push_back(bl);
		}
		if (!nearby.empty()) sd->motion.cursor = (sd->motion.cursor+15)%nearby.size();
	}
	if (!rows.empty()) motion_snapshot(sd,rows);
	return 0;
}
static TIMER_FUNC(motion_recovery_timer) { map_foreachpc(motion_recover); return 0; }
static int32 motion_notify(block_list* recipient, va_list ap) {
	auto bl = va_arg(ap,block_list*); int reason = va_arg(ap,int);
	auto sd = (map_session_data*)recipient;
	if (sd->motion.enabled && motion_visible(sd,bl)) motion_snapshot(sd,{bl},reason);
	return 0;
}
} // namespace
void clif_motion_timer_late(t_tick scheduled) {
	t_tick now = gettick_nocache(); uint16 late = (uint16)std::min<t_tick>(65535,std::max<t_tick>(0,DIFF_TICK(now,scheduled)));
	if (DIFF_TICK(now,motion_late_at) >= 1000) { motion_late = 0; motion_late_at = now; }
	motion_late = std::max(motion_late,late);
}
void clif_motion_changed(block_list* bl, uint8 reason) {
	unit_data* ud = unit_bl2ud(bl); if (!ud || !bl->prev) return;
	++ud->motion_generation;
	// Feature bit 1: a click sent before the client saw this relocation is stale.
	if (reason == 8 && bl->type == BL_PC) ((map_session_data*)bl)->motion.fence = ud->motion_generation;
	map_foreachinallarea(motion_notify,bl->m,bl->x-AREA_SIZE,bl->y-AREA_SIZE,bl->x+AREA_SIZE,bl->y+AREA_SIZE,BL_PC,bl,(int)reason);
}
static void clif_parse_MotionAccept(int32 fd, map_session_data* sd) {
	if (RFIFOW(fd,2) != 1 || !sd->motion.epoch || RFIFOL(fd,4) != sd->motion.epoch) return;
	sd->motion.enabled = true; motion_snapshot(sd,{sd});
}
static void motion_move(map_session_data* sd, uint32 input, int16 x, int16 y, bool fenced) {
	if ((int32)(input-sd->motion.input) > 0) {
		sd->motion.input = input;
		// Acknowledged but not executed: it was aimed from before a relocation.
		bool accepted = !fenced && clif_walk_request(sd,x,y);
		motion_snapshot(sd,{sd},accepted ? 1 : 6);
	} else motion_snapshot(sd,{sd});
}
static void clif_parse_MotionMove(int32 fd, map_session_data* sd) {
	if (!sd->motion.enabled || RFIFOL(fd,2) != sd->motion.epoch) return;
	motion_move(sd,RFIFOL(fd,6),RFIFOW(fd,10),RFIFOW(fd,12),false);
}
static void clif_parse_MotionMove2(int32 fd, map_session_data* sd) {
	if (!sd->motion.enabled || RFIFOL(fd,2) != sd->motion.epoch) return;
	uint32 seen = RFIFOL(fd,10);
	motion_move(sd,RFIFOL(fd,6),RFIFOW(fd,14),RFIFOW(fd,16),(int32)(seen-sd->motion.fence) < 0);
}
