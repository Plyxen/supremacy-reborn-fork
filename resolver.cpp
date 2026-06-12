#include "includes.h"

Resolver g_resolver{};

LagRecord* Resolver::FindIdealRecord( AimPlayer* data ) {
    LagRecord *first_valid, *current;

	if( data->m_records.empty( ) )
		return nullptr;

    first_valid = nullptr;

    // iterate records.
	for( const auto &it : data->m_records ) {
		if( it->dormant( ) || it->immune( ) || !it->valid( ) )
			continue;

        // get current record.
        current = it.get( );

        // first record that was valid, store it for later.
        if( !first_valid )
            first_valid = current;

        // try to find a record with a shot, lby update, walking or no anti-aim.
		if( it->m_shot || it->m_mode == Modes::RESOLVE_BODY || it->m_mode == Modes::RESOLVE_WALK || it->m_mode == Modes::RESOLVE_NONE )
            return current;
	}

	// none found above, return the first valid record if possible.
	return ( first_valid ) ? first_valid : nullptr;
}

LagRecord* Resolver::FindLastRecord( AimPlayer* data ) {
    LagRecord* current;

	if( data->m_records.empty( ) )
		return nullptr;

	// iterate records in reverse.
	for( auto it = data->m_records.crbegin( ); it != data->m_records.crend( ); ++it ) {
		current = it->get( );

		// if this record is valid.
		// we are done since we iterated in reverse.
		if( current->valid( ) && !current->immune( ) && !current->dormant( ) )
			return current;
	}

	return nullptr;
}

LagRecord* Resolver::FindFirstRecord( AimPlayer* data ) {
	if( data->m_records.empty( ) )
		return nullptr;

	// records are stored newest-first (emplace_front), so iterating forward
	// gives us the most recent record first. return the first valid one.
	for( const auto& it : data->m_records ) {
		LagRecord* current = it.get( );

		if( !current->dormant( ) && !current->immune( ) && current->valid( ) )
			return current;
	}

	return nullptr;
}

void Resolver::OnBodyUpdate( Player* player, float value ) {
	AimPlayer* data = &g_aimbot.m_players[ player->index( ) - 1 ];

	// set data.
	data->m_old_body = data->m_body;
	data->m_body     = value;
}

float Resolver::GetAwayAngle( LagRecord* record ) {
	float  delta{ std::numeric_limits< float >::max( ) };
	vec3_t pos;
	ang_t  away;

	// other cheats predict you by their own latency.
	// they do this because, then they can put their away angle to exactly
	// where you are on the server at that moment in time.

	// the idea is that you would need to know where they 'saw' you when they created their user-command.
	// lets say you move on your client right now, this would take half of our latency to arrive at the server.
	// the delay between the server and the target client is compensated by themselves already, that is fortunate for us.

	// we have no historical origins.
	// no choice but to use the most recent one.
	//if( g_cl.m_net_pos.empty( ) ) {
		math::VectorAngles( g_cl.m_local->m_vecOrigin( ) - record->m_pred_origin, away );
		return away.y;
	//}

	// half of our rtt.
	// also known as the one-way delay.
	//float owd = ( g_cl.m_latency / 2.f );

	// since our origins are computed here on the client
	// we have to compensate for the delay between our client and the server
	// therefore the OWD should be subtracted from the target time.
	//float target = record->m_pred_time; //- owd;

	// iterate all.
	//for( const auto &net : g_cl.m_net_pos ) {
		// get the delta between this records time context
		// and the target time.
	//	float dt = std::abs( target - net.m_time );

		// the best origin.
	//	if( dt < delta ) {
	//		delta = dt;
	//		pos   = net.m_pos;
	//	}
	//}

	//math::VectorAngles( pos - record->m_pred_origin, away );
	//return away.y;
}

void Resolver::MatchShot( AimPlayer* data, LagRecord* record ) {
	// do not attempt to do this in nospread mode.
	if( g_menu.main.config.mode.get( ) == 1 )
		return;

	float shoot_time = -1.f;

	Weapon* weapon = data->m_player->GetActiveWeapon( );
	if( weapon ) {
		// with logging this time was always one tick behind.
		// so add one tick to the last shoot time.
		shoot_time = weapon->m_fLastShotTime( ) + g_csgo.m_globals->m_interval;
	}

	// this record has a shot on it.
	if( game::TIME_TO_TICKS( shoot_time ) == game::TIME_TO_TICKS( record->m_sim_time ) ) {
		if( record->m_lag <= 2 )
			record->m_shot = true;
		
		// more then 1 choke, cant hit pitch, apply prev pitch.
		else if( data->m_records.size( ) >= 2 ) {
			LagRecord* previous = data->m_records[ 1 ].get( );

			if( previous && !previous->dormant( ) )
				record->m_eye_angles.x = previous->m_eye_angles.x;
		}
	}
}

void Resolver::SetMode( LagRecord* record ) {
	float speed = record->m_anim_velocity.length( );

	// air takes highest priority.
	if( !( record->m_flags & FL_ONGROUND ) )
		record->m_mode = Modes::RESOLVE_AIR;

	// moving on ground, not fakewalking.
	else if( speed > 0.1f && !record->m_fake_walk )
		record->m_mode = Modes::RESOLVE_WALK;

	// standing still or fakewalking.
	else
		record->m_mode = Modes::RESOLVE_STAND;
}

void Resolver::ResolveAngles( Player* player, LagRecord* record ) {
	AimPlayer* data = &g_aimbot.m_players[ player->index( ) - 1 ];

	// mark this record if it contains a shot.
	MatchShot( data, record );

	// next up mark this record with a resolver mode that will be used.
	SetMode( record );

	// if we are in nospread mode, force all players pitches to down.
	// TODO; we should check thei actual pitch and up too, since those are the other 2 possible angles.
	// this should be somehow combined into some iteration that matches with the air angle iteration.
	if( g_menu.main.config.mode.get( ) == 1 )
		record->m_eye_angles.x = 90.f;

	// we arrived here we can do the acutal resolve.
	if( record->m_mode == Modes::RESOLVE_WALK ) 
		ResolveWalk( data, record );

	else if( record->m_mode == Modes::RESOLVE_STAND )
		ResolveStand( data, record );

	else if( record->m_mode == Modes::RESOLVE_AIR )
		ResolveAir( data, record );

	// normalize the eye angles, doesn't really matter but its clean.
	math::NormalizeAngle( record->m_eye_angles.y );
}

void Resolver::ResolveWalk( AimPlayer* data, LagRecord* record ) {
	// velocity-direction vs body-yaw disambiguation (inspired by network-AA analysis).
	// when the player is strafing sideways (45-135° off body yaw), their body yaw is
	// lagging behind their actual movement — use the away angle as a better estimate.
	// back-pedaling (>135°) and forward (< 45°) both leave body yaw as the best guess.
	float vel_len = record->m_velocity.length_2d( );
	if( vel_len > 30.f ) {
		float vel_yaw = math::rad_to_deg( std::atan2( record->m_velocity.y, record->m_velocity.x ) );
		float diff    = std::abs( math::NormalizedAngle( vel_yaw - record->m_body ) );

		if( diff >= 45.f && diff <= 135.f )
			record->m_eye_angles.y = GetAwayAngle( record );
		else
			record->m_eye_angles.y = record->m_body;
	}
	else
		record->m_eye_angles.y = record->m_body;

	// delay body update.
	data->m_body_update = record->m_anim_time + 0.22f;

	// reset stand and body index.
	data->m_stand_index  = 0;
	data->m_stand_index2 = 0;
	data->m_body_index   = 0;

	// copy the last record that this player was walking
	// we need it later on because it gives us crucial data.
	std::memcpy( &data->m_walk_record, record, sizeof( LagRecord ) );
}

void Resolver::ResolveStand( AimPlayer* data, LagRecord* record ) {
	// for no-spread call a seperate resolver.
	if( g_menu.main.config.mode.get( ) == 1 ) {
		StandNS( data, record );
		return;
	}

	// get predicted away angle for the player.
	float away = GetAwayAngle( record );

	// pointer for easy access.
	LagRecord* move = &data->m_walk_record;

	// re-evaluate walk context every tick — do not carry stale state forward.
	data->m_moved = false;

	if( move->m_sim_time > 0.f ) {
		vec3_t delta = move->m_origin - record->m_origin;

		// only use walk context if the player stopped within 128 units of their last walk pos.
		if( delta.length( ) <= 128.f )
			data->m_moved = true;
	}

	// a valid moving context was found.
	if( data->m_moved ) {
		float delta = record->m_anim_time - move->m_anim_time;

		// it has not been time for the first LBY update yet.
		if( delta < 0.22f ) {
			record->m_eye_angles.y = move->m_body;
			record->m_mode = Modes::RESOLVE_STOPPED_MOVING;
			return;
		}

		// LBY should have updated here.
		else if( record->m_anim_time >= data->m_body_update ) {
			// try the LBY flick up to 3 times before giving up.
			if( data->m_body_index <= 3 ) {
				record->m_eye_angles.y = record->m_body;
				data->m_body_update    = record->m_anim_time + 1.1f;
				record->m_mode         = Modes::RESOLVE_BODY;
				return;
			}

			// stand1 — have walk context, LBY flick failed. brute-force the angle space.
			record->m_mode = Modes::RESOLVE_STAND1;

			switch( data->m_stand_index % 8 ) {
			case 0: record->m_eye_angles.y = away + 180.f;       break; // face-on — most common aa angle
			case 1: record->m_eye_angles.y = record->m_body;     break; // raw lby (may still be real)
			case 2: record->m_eye_angles.y = move->m_body;       break; // lby from last walk
			case 3: record->m_eye_angles.y = move->m_body + 58.f; break; // left side off-body
			case 4: record->m_eye_angles.y = move->m_body - 58.f; break; // right side off-body
			case 5: record->m_eye_angles.y = move->m_body + 180.f; break; // flip from walk lby
			case 6: record->m_eye_angles.y = away + 90.f;        break;
			case 7: record->m_eye_angles.y = away - 90.f;        break;
			}

			return;
		}
	}

	// stand2 — no walk context. brute-force from away angle outward.
	// fast-path: if body yaw has been static for 4+ consecutive records with no
	// misses recorded, the player is using fixed AA — away+180 is most likely real.
	bool static_aa = false;
	if( data->m_records.size( ) >= 4 && data->m_missed_shots == 0 ) {
		float ref = data->m_records[ 0 ]->m_body;
		static_aa = true;
		for( int si = 1; si < 4 && static_aa; ++si ) {
			auto& r = data->m_records[ si ];
			if( r->valid( ) && !r->dormant( ) )
				if( std::abs( math::NormalizedAngle( r->m_body - ref ) ) > 5.f )
					static_aa = false;
		}
	}

	record->m_mode = Modes::RESOLVE_STAND2;

	if( static_aa ) {
		record->m_eye_angles.y = away + 180.f;
		return;
	}

	switch( data->m_stand_index2 % 8 ) {
	case 0: record->m_eye_angles.y = away + 180.f;           break; // face-on
	case 1: record->m_eye_angles.y = record->m_body;         break; // raw lby
	case 2: record->m_eye_angles.y = record->m_body + 180.f; break;
	case 3: record->m_eye_angles.y = record->m_body + 90.f;  break;
	case 4: record->m_eye_angles.y = record->m_body - 90.f;  break;
	case 5: record->m_eye_angles.y = away;                   break;
	case 6: record->m_eye_angles.y = away + 90.f;            break;
	case 7: record->m_eye_angles.y = away - 90.f;            break;
	default: break;
	}
}

void Resolver::StandNS( AimPlayer* data, LagRecord* record ) {
	// get away angles.
	float away = GetAwayAngle( record );

	switch( data->m_shots % 8 ) {
	case 0:
		record->m_eye_angles.y = away + 180.f;
		break;

	case 1:
		record->m_eye_angles.y = away + 90.f;
		break;
	case 2:
		record->m_eye_angles.y = away - 90.f;
		break;

	case 3:
		record->m_eye_angles.y = away + 45.f;
		break;
	case 4:
		record->m_eye_angles.y = away - 45.f;
		break;

	case 5:
		record->m_eye_angles.y = away + 135.f;
		break;
	case 6:
		record->m_eye_angles.y = away - 135.f;
		break;

	case 7:
		record->m_eye_angles.y = away + 0.f;
		break;

	default:
		break;
	}

	// force LBY to not fuck any pose and do a true bruteforce.
	record->m_body = record->m_eye_angles.y;
}

void Resolver::ResolveAir( AimPlayer* data, LagRecord* record ) {
	// for no-spread call a seperate resolver.
	if( g_menu.main.config.mode.get( ) == 1 ) {
		AirNS( data, record );
		return;
	}

	// else run our matchmaking air resolver.

	// we have barely any speed. 
	// either we jumped in place or we just left the ground.
	// or someone is trying to fool our resolver.
	if( record->m_velocity.length_2d( ) < 60.f ) {
		// set this for completion.
		// so the shot parsing wont pick the hits / misses up.
		// and process them wrongly.
		record->m_mode = Modes::RESOLVE_STAND;

		// invoke our stand resolver.
		ResolveStand( data, record );

		// we are done.
		return;
	}

	// try to predict the direction of the player based on his velocity direction.
	// this should be a rough estimation of where he is looking.
	float velyaw = math::rad_to_deg( std::atan2( record->m_velocity.y, record->m_velocity.x ) );

	switch( data->m_shots % 5 ) {
	case 0: record->m_eye_angles.y = velyaw + 180.f; break;
	case 1: record->m_eye_angles.y = velyaw - 90.f;  break;
	case 2: record->m_eye_angles.y = velyaw + 90.f;  break;
	case 3: record->m_eye_angles.y = velyaw + 135.f; break;
	case 4: record->m_eye_angles.y = velyaw - 135.f; break;
	}
}

void Resolver::AirNS( AimPlayer* data, LagRecord* record ) {
	// get away angles.
	float away = GetAwayAngle( record );

	switch( data->m_shots % 9 ) {
	case 0:
		record->m_eye_angles.y = away + 180.f;
		break;

	case 1:
		record->m_eye_angles.y = away + 150.f;
		break;
	case 2:
		record->m_eye_angles.y = away - 150.f;
		break;

	case 3:
		record->m_eye_angles.y = away + 165.f;
		break;
	case 4:
		record->m_eye_angles.y = away - 165.f;
		break;

	case 5:
		record->m_eye_angles.y = away + 135.f;
		break;
	case 6:
		record->m_eye_angles.y = away - 135.f;
		break;

	case 7:
		record->m_eye_angles.y = away + 90.f;
		break;
	case 8:
		record->m_eye_angles.y = away - 90.f;
		break;

	default:
		break;
	}
}

void Resolver::ResolvePoses( Player* player, LagRecord* record ) {
	AimPlayer* data = &g_aimbot.m_players[ player->index( ) - 1 ];

	// only do this bs when in air.
	if( record->m_mode == Modes::RESOLVE_AIR ) {
		// ang = pose min + pose val x ( pose range )

		// lean_yaw — cycle 0..4 deterministically based on shots.
		player->m_flPoseParameter( )[ 2 ]  = ( data->m_shots % 5 ) * 0.25f;

		// body_yaw — cycle 1..3 deterministically.
		player->m_flPoseParameter( )[ 11 ] = ( ( data->m_shots % 3 ) + 1 ) * 0.25f;
	}
}